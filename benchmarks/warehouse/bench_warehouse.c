/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* bench_warehouse.c - Smart2Raw against the warehouse encoding family
 * (dictionary + bit-packing + RLE: the stack Capacitor and Parquet are built on).
 *
 * Every number in WAREHOUSE_FORMAT_BENCH.md comes from this one program.
 *
 * Two rules this harness enforces on itself, because the previous round violated
 * both and produced results that did not survive review:
 *
 *   1. THE PEER IS IMPLEMENTED AT ITS BEST, NOT AT ITS MOST CONVENIENT.
 *      A dictionary is built over SORTED distinct values, so `dict[d] > k` is
 *      monotone in d and the predicate collapses to `code > d_k` - a comparison
 *      on the codes themselves, needing no decode at all. And sub-byte codes get
 *      a SIMD unpacker. Both are what a real reader does; measuring the peer
 *      without them measures our vectoriser against their scalar loop and calls
 *      the difference a format property.
 *
 *   2. OUR SIDE USES THE SHIPPED LIBRARY, NOT A RESEARCH KERNEL.
 *      Every Smart2Raw figure below calls s2r_count_gt_fast / s2r_sum_fast from
 *      include/smart2raw.h, so the documented numbers are numbers a user gets.
 *      Where the representation carries more than a flat pool - regime C - the
 *      segment layer is applied, because measuring a classical layout and
 *      attributing the result to this project's theory produces false defeats.
 *
 * Every result is cross-checked against a brute-force reference before it is
 * timed; a mismatch aborts rather than printing a fast wrong answer.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <immintrin.h>
#include "smart2raw.h"

/* the segment layer (research layer, same folder) */
typedef struct Col2 Col2;
Col2    *s2r2_build(const int64_t*, size_t, uint32_t);
void     s2r2_free(Col2*);
uint64_t s2r2_count_gt(const Col2*, int64_t, uint64_t*);
int64_t  s2r2_sum(const Col2*);
uint64_t s2r2_bytes(const Col2*), s2r2_meta_bytes(const Col2*);
uint32_t s2r2_nseg(const Col2*), s2r2_nconst(const Col2*), s2r2_nsorted(const Col2*);
void     s2r2_meta_classes(const Col2*, int*, int*, int*);

static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
                         return t.tv_sec + t.tv_nsec*1e-9; }
#define REPS 15
#define BEST(dst, expr) do{ double b=1e30; for(int r_=0;r_<REPS;r_++){ \
    double t0_=now(); (dst)=(expr); double d_=now()-t0_; if(d_<b) b=d_; } best_=b; }while(0)
static double best_;

static uint64_t rs = 0x243F6A8885A308D3ull;
static uint64_t rnd(void){ rs = rs*6364136223846793005ull + 1442695040888963407ull; return rs>>33; }

/* ================================================================== */
/* the peer: dictionary + bit-packed codes                            */
/* ================================================================== */

typedef struct {
    int64_t *dict;     /* SORTED distinct values */
    int      ndict;
    int      cbits;    /* bits per code */
    uint8_t *codes;    /* bit-packed stream */
    size_t   n;
    size_t   bytes;
} Dict;

static int cmp_i64(const void*a,const void*b){
    int64_t x=*(const int64_t*)a, y=*(const int64_t*)b; return (x>y)-(x<y);
}
static Dict *dict_build(const int64_t *v, size_t n){
    Dict *d = calloc(1,sizeof(Dict));
    int64_t *tmp = malloc(n*sizeof(int64_t));
    memcpy(tmp,v,n*sizeof(int64_t));
    qsort(tmp,n,sizeof(int64_t),cmp_i64);
    d->dict = malloc(n*sizeof(int64_t)); d->ndict=0;
    for(size_t i=0;i<n;i++) if(!i||tmp[i]!=tmp[i-1]) d->dict[d->ndict++]=tmp[i];
    free(tmp);
    d->cbits = 1; while((1<<d->cbits) < d->ndict) d->cbits++;
    if(d->ndict<=1) d->cbits=1;
    d->n=n;
    d->bytes = (n*(size_t)d->cbits + 7)/8;
    d->codes = calloc(d->bytes+8,1);
    for(size_t i=0;i<n;i++){
        int lo=0, hi=d->ndict-1, code=0;             /* binary search in the dict */
        while(lo<=hi){ int m=(lo+hi)/2;
            if(d->dict[m]==v[i]){ code=m; break; }
            if(d->dict[m]<v[i]) lo=m+1; else hi=m-1; }
        size_t bit=i*(size_t)d->cbits;
        for(int b=0;b<d->cbits;b++)
            if(code>>b & 1) d->codes[(bit+b)>>3] |= (uint8_t)(1u<<((bit+b)&7));
    }
    return d;
}
static void dict_free(Dict*d){ if(!d)return; free(d->dict); free(d->codes); free(d); }
/* Unpack ONE code. The generic bit loop is only for widths a real reader would
 * not meet; 8 and 4 bits get the direct forms, otherwise the "textbook" peer
 * would be a strawman and the decode tax it shows would be my bit extractor
 * rather than the format. */
static inline unsigned dict_unpack(const Dict *d, size_t i){
    if(d->cbits==8) return d->codes[i];
    if(d->cbits==4){ uint8_t b=d->codes[i>>1]; return (i&1)?(unsigned)(b>>4):(unsigned)(b&0x0F); }
    if(d->cbits==16) return (unsigned)(d->codes[2*i] | ((unsigned)d->codes[2*i+1]<<8));
    size_t bit=i*(size_t)d->cbits; unsigned c=0;
    for(int b=0;b<d->cbits;b++) c |= ((d->codes[(bit+b)>>3]>>((bit+b)&7))&1u)<<b;
    return c;
}
/* threshold code: the largest d with dict[d] <= k. Exists because the dictionary
 * is sorted, which is what makes the predicate a plain comparison on codes. */
static int dict_threshold(const Dict *d, int64_t k){
    int t=-1; for(int i=0;i<d->ndict;i++) if(d->dict[i]<=k) t=i; return t;
}

/* (a) the textbook form: unpack every code, index a hit table */
static uint64_t peer_count_lut(const Dict *d, const uint8_t *hit){
    uint64_t c=0; for(size_t i=0;i<d->n;i++) c += hit[dict_unpack(d,i)];
    return c;
}
/* (b) byte-aligned codes (cbits==8): predicate is `code > dk` on the bytes */
__attribute__((target("avx2")))
static uint64_t peer_count_gt8(const uint8_t *a, size_t n, uint8_t dk){
    const __m256i vt=_mm256_set1_epi8((char)dk), z=_mm256_setzero_si256();
    uint64_t c=0; size_t i=0;
    for(; i+32<=n; i+=32){
        __m256i v=_mm256_loadu_si256((const __m256i*)(a+i));
        __m256i nz=_mm256_cmpeq_epi8(_mm256_subs_epu8(v,vt),z);
        c += 32 - (uint64_t)__builtin_popcount((unsigned)_mm256_movemask_epi8(nz));
    }
    for(; i<n; i++) c += (a[i]>dk);
    return c;
}
/* (c) 4-bit codes: two nibbles per byte, unpacked with two vector ops and
 * compared against the threshold code. This is the SIMD unpacker the previous
 * document listed as untested future work. */
__attribute__((target("avx2")))
static uint64_t peer_count_gt4(const uint8_t *pk, size_t n, uint8_t dk){
    const __m256i mask=_mm256_set1_epi8(0x0F), vt=_mm256_set1_epi8((char)dk);
    const __m256i z=_mm256_setzero_si256();
    uint64_t c=0; size_t nb=n>>1, i=0;
    for(; i+32<=nb; i+=32){
        __m256i b=_mm256_loadu_si256((const __m256i*)(pk+i));
        __m256i lo=_mm256_and_si256(b,mask);
        __m256i hi=_mm256_and_si256(_mm256_srli_epi16(b,4),mask);
        __m256i nl=_mm256_cmpeq_epi8(_mm256_subs_epu8(lo,vt),z);
        __m256i nh=_mm256_cmpeq_epi8(_mm256_subs_epu8(hi,vt),z);
        c += 64 - (uint64_t)__builtin_popcount((unsigned)_mm256_movemask_epi8(nl))
                - (uint64_t)__builtin_popcount((unsigned)_mm256_movemask_epi8(nh));
    }
    for(size_t j=i*2;j<n;j++){ uint8_t b=pk[j>>1];
        uint8_t code=(j&1)?(uint8_t)(b>>4):(uint8_t)(b&0x0F); c += (code>dk); }
    return c;
}
/* SUM: histogram over codes, then fold the dictionary. The unpack per value is
 * unavoidable here - a code is not an addable operand. */
static int64_t peer_sum_hist(const Dict *d){
    static uint64_t h[65536];
    memset(h,0,sizeof(uint64_t)*(size_t)(1u<<d->cbits));
    for(size_t i=0;i<d->n;i++) h[dict_unpack(d,i)]++;
    int64_t s=0; for(int i=0;i<d->ndict;i++) s += d->dict[i]*(int64_t)h[i];
    return s;
}
/* materialisation: what any non-SQL kernel must pay before it can start */
static void peer_materialize_u8(const Dict *d, uint8_t *out){
    for(size_t i=0;i<d->n;i++) out[i]=(uint8_t)d->dict[dict_unpack(d,i)];
}

/* RLE over the codes, for the run-shaped regime */
typedef struct { uint32_t *code, *len; size_t nruns, bytes; int64_t *dict; int ndict; } Rle;
static Rle *rle_build(const int64_t *v, size_t n, const Dict *d){
    Rle *r=calloc(1,sizeof(Rle));
    r->code=malloc(n*4); r->len=malloc(n*4); r->dict=d->dict; r->ndict=d->ndict;
    size_t k=0;
    for(size_t i=0;i<n;i++){
        unsigned c=dict_unpack(d,i);
        if(k && r->code[k-1]==c) r->len[k-1]++;
        else { r->code[k]=c; r->len[k]=1; k++; }
    }
    r->nruns=k;
    /* a run is (code, length); code needs cbits, length a varint - count 4+4 as
     * an upper bound and note it in the document */
    r->bytes = k*8;
    (void)v;
    return r;
}
static void rle_free(Rle*r){ if(!r)return; free(r->code); free(r->len); free(r); }
static uint64_t rle_count_gt(const Rle *r, int64_t k){
    uint64_t c=0;
    for(size_t i=0;i<r->nruns;i++) if(r->dict[r->code[i]]>k) c += r->len[i];
    return c;
}
static int64_t rle_sum(const Rle *r){
    int64_t s=0; for(size_t i=0;i<r->nruns;i++) s += r->dict[r->code[i]]*(int64_t)r->len[i];
    return s;
}

/* ================================================================== */

static size_t N = 12000000;

static void hdr(const char *t){ printf("\n\033[1m%s\033[0m\n", t); }

int main(int argc, char **argv){
    if(argc>1) N=(size_t)strtoull(argv[1],NULL,10);
    printf("bench_warehouse - N=%zu, single core, AVX2=%d\n", N, s2r_has_avx2());
    printf("Smart2Raw figures call the SHIPPED library (v%s); the peer is given a\n"
           "sorted dictionary, predicate pushdown onto the codes, a SIMD unpacker\n"
           "and whole-run skipping.\n", S2R_VERSION_STRING);

    int64_t *v = malloc(N*sizeof(int64_t));

    /* ---------------- REGIME A: uniform, high cardinality ---------------- */
    hdr("REGIME A - uniform integers 0..200 (the telemetry case), k=100");
    {
        for(size_t i=0;i<N;i++) v[i]=(int64_t)(rnd()%201);
        S2RPool p; s2r_pool_init(&p,S2R_8,N);
        for(size_t i=0;i<N;i++) s2r_set(&p,i,(uint64_t)v[i]);
        p.count=N;
        Dict *d = dict_build(v,N);
        uint8_t hit[256]={0}; for(int i=0;i<d->ndict;i++) hit[i]= d->dict[i]>100;
        int dk = dict_threshold(d,100);

        uint64_t brute=0; int64_t bsum=0;
        for(size_t i=0;i<N;i++){ brute += (v[i]>100); bsum += v[i]; }

        printf("  cardinality %d -> %d-bit codes\n", d->ndict, d->cbits);
        printf("  size   Smart2Raw u8 %.2f MB | peer codes+dict %.2f MB\n",
               N/1048576.0, (d->bytes + d->ndict*8.0)/1048576.0);
        if(d->cbits==8)
            printf("  the codes are byte-aligned, so the peer's payload is byte-for-byte\n"
                   "  the same array as our u8 pool - any gap here is implementation\n");

        uint64_t r; int64_t s;
        BEST(r, s2r_count_gt_fast(&p,100));        double a_s2r_c=best_; assert(r==brute);
        BEST(r, peer_count_lut(d,hit));            double a_lut=best_;   assert(r==brute);
        BEST(r, peer_count_gt8(d->codes,N,(uint8_t)dk)); double a_thr=best_; assert(r==brute);
        BEST(s, (int64_t)s2r_sum_fast(&p));        double a_s2r_s=best_; assert(s==bsum);
        BEST(s, peer_sum_hist(d));                 double a_hist=best_;  assert(s==bsum);

        printf("  COUNT  Smart2Raw(shipped) %7.2f ms | peer LUT %7.2f ms (%.2fx) | peer sorted-dict SIMD %7.2f ms (%.2fx)\n",
               a_s2r_c*1e3, a_lut*1e3, a_lut/a_s2r_c, a_thr*1e3, a_thr/a_s2r_c);
        printf("  SUM    Smart2Raw(shipped) %7.2f ms | peer histogram %7.2f ms (%.2fx)\n",
               a_s2r_s*1e3, a_hist*1e3, a_hist/a_s2r_s);

        /* cross-domain: what a non-SQL kernel must pay to reach its starting line */
        uint8_t *out = malloc(N);
        double t0=now(); peer_materialize_u8(d,out); double mat=now()-t0;
        for(int r2=0;r2<3;r2++){ t0=now(); peer_materialize_u8(d,out); double dd=now()-t0; if(dd<mat)mat=dd; }
        printf("  MATERIALISE to a contiguous u8 buffer: peer %7.2f ms | Smart2Raw 0.00 ms (the pool IS the buffer)\n",
               mat*1e3);
        free(out);
        s2r_pool_free(&p); dict_free(d);
    }

    /* ---------------- REGIME B: low cardinality, wide range ---------------- */
    hdr("REGIME B - 12 distinct values spread over 500..11500, shuffled, k=5500");
    {
        int64_t dv[12]; for(int i=0;i<12;i++) dv[i]=500+i*1000;
        for(size_t i=0;i<N;i++) v[i]=dv[rnd()%12];
        S2RPool p; s2r_pool_init(&p,S2R_16,N);
        for(size_t i=0;i<N;i++) s2r_set(&p,i,(uint64_t)v[i]);
        p.count=N;
        Dict *d = dict_build(v,N);
        uint8_t hit[16]={0}; for(int i=0;i<d->ndict;i++) hit[i]= d->dict[i]>5500;
        int dk = dict_threshold(d,5500);
        /* pack the 4-bit codes into nibbles for the SIMD unpacker */
        uint8_t *pk = calloc((N+1)/2,1);
        for(size_t i=0;i<N;i++){ unsigned c=dict_unpack(d,i);
            if(i&1) pk[i>>1] |= (uint8_t)(c<<4); else pk[i>>1] |= (uint8_t)c; }

        uint64_t brute=0; for(size_t i=0;i<N;i++) brute += (v[i]>5500);
        printf("  cardinality %d -> %d-bit codes (sub-byte: a width we deliberately refuse)\n",
               d->ndict, d->cbits);
        printf("  size   Smart2Raw u16 %.2f MB | peer %.2f MB  (peer %.1fx smaller)\n",
               N*2/1048576.0, (d->bytes+d->ndict*8.0)/1048576.0,
               (N*2.0)/(d->bytes+d->ndict*8.0));
        uint64_t r;
        BEST(r, s2r_count_gt_fast(&p,5500));        double b_s2r=best_; assert(r==brute);
        BEST(r, peer_count_lut(d,hit));             double b_lut=best_; assert(r==brute);
        BEST(r, peer_count_gt4(pk,N,(uint8_t)dk));  double b_nib=best_; assert(r==brute);
        printf("  COUNT  Smart2Raw(shipped) %7.2f ms | peer unpack+LUT %7.2f ms (%.2fx) | peer nibble SIMD %7.2f ms (%.2fx)\n",
               b_s2r*1e3, b_lut*1e3, b_lut/b_s2r, b_nib*1e3, b_nib/b_s2r);
        printf("  effective bandwidth: Smart2Raw %.1f GB/s | peer %.1f GB/s  (both memory bound)\n",
               (N*2.0)/b_s2r/1e9, ((N+1)/2.0)/b_nib/1e9);
        free(pk); s2r_pool_free(&p); dict_free(d);
    }

    /* ---------------- REGIME C: sorted, run-shaped ---------------- */
    hdr("REGIME C - the same column, SORTED, k=5500");
    {
        for(size_t i=0;i<N;i++) v[i]=500+(int64_t)(i*12/N)*1000;
        S2RPool p; s2r_pool_init(&p,S2R_16,N);
        for(size_t i=0;i<N;i++) s2r_set(&p,i,(uint64_t)v[i]);
        p.count=N;
        Dict *d = dict_build(v,N);
        Rle *rl = rle_build(v,N,d);
        uint64_t brute=0; int64_t bsum=0;
        for(size_t i=0;i<N;i++){ brute += (v[i]>5500); bsum += v[i]; }

        /* The segment size is a real knob and the previous document never turned
         * it: payload shrinks with smaller segments (more of them become constant)
         * while metadata grows linearly with the segment count. The total has a
         * minimum, and it is not at either end. */
        printf("  segment-size sweep (payload + metadata, and the predicate cost):\n");
        printf("    seg_size   segs  const   payload      meta     total     COUNT      touched\n");
        for(uint32_t ss=64; ss<=262144; ss*=2){
            Col2 *c = s2r2_build(v,N,ss);
            uint64_t tb=0, rr=s2r2_count_gt(c,5500,&tb); assert(rr==brute);
            uint64_t rc; BEST(rc, s2r2_count_gt(c,5500,NULL)); (void)rc;
            double tot=(s2r2_bytes(c)+s2r2_meta_bytes(c))/1024.0;
            printf("    %8u  %5u  %5u  %8.2f KB %8.2f KB %8.2f KB %8.3f us %8.2f KB\n",
                   ss, s2r2_nseg(c), s2r2_nconst(c), s2r2_bytes(c)/1024.0,
                   s2r2_meta_bytes(c)/1024.0, tot, best_*1e6, tb/1024.0);
            s2r2_free(c);
        }
        uint32_t sizes[2] = {65536, 256};
        for(int q=0;q<2;q++){
            Col2 *c = s2r2_build(v,N,sizes[q]);
            int zb,sb,ob; s2r2_meta_classes(c,&zb,&sb,&ob);
            uint64_t touched=0, rr = s2r2_count_gt(c,5500,&touched);
            assert(rr==brute); assert(s2r2_sum(c)==bsum);
            uint64_t rc;
            BEST(rc, s2r2_count_gt(c,5500,NULL));  double t_seg=best_;
            (void)rc;
            printf("  segments(seg=%-5u) %u segs, %u constant, %u sorted | payload %8.2f KB + meta %7.2f KB [i%d/i%d/u%d]\n",
                   sizes[q], s2r2_nseg(c), s2r2_nconst(c), s2r2_nsorted(c),
                   s2r2_bytes(c)/1024.0, s2r2_meta_bytes(c)/1024.0, zb,sb,ob);
            printf("                       COUNT %8.3f us   bytes touched by the predicate %8.2f KB (counted, not modelled)\n",
                   t_seg*1e6, touched/1024.0);
            s2r2_free(c);
        }
        uint64_t r; int64_t s;
        BEST(r, s2r_count_gt_fast(&p,5500)); double c_flat=best_; assert(r==brute);
        BEST(r, rle_count_gt(rl,5500));      double c_rle=best_;  assert(r==brute);
        BEST(s, rle_sum(rl));                double c_rles=best_; assert(s==bsum);
        BEST(s, (int64_t)s2r_sum_fast(&p));  double c_flats=best_; assert(s==bsum);
        printf("  flat pool (classical)  %8.2f MB   COUNT %8.3f us   SUM %8.3f us\n",
               N*2/1048576.0, c_flat*1e6, c_flats*1e6);
        printf("  peer RLE               %8.2f KB (%zu runs)   COUNT %8.3f us   SUM %8.3f us\n",
               rl->bytes/1024.0, rl->nruns, c_rle*1e6, c_rles*1e6);
        rle_free(rl); dict_free(d); s2r_pool_free(&p);
    }

    free(v);
    printf("\nall counts and sums verified against brute force before timing.\n");
    return 0;
}
