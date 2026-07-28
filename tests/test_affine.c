/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* test_affine.c - the frame of reference gains a SCALE, and nothing else may
 * move.
 *
 * v3.4.0 removed an offset the data did not need: v = base + delta. It did not
 * remove a scale. A column of { 500, 1500, ... 11500 } spans 11000 and takes 14
 * bits, but every value is base + 1000*i with i in 0..11 - four bits of index in
 * a fourteen-bit coat.
 *
 *     v = base + stride*i          stride = gcd over the block of (v - base)
 *
 * Two properties are load-bearing and both are swept here rather than argued:
 *
 *   1. stride == 1 is EXACTLY v3.4.0. A column with no common step must classify
 *      byte-for-byte as before and serialize as fmt = 2, so a v3.4.0 reader still
 *      opens it. A "win" that quietly changes the common case is not a win.
 *   2. Every operation rewrites in CLOSED FORM. The rewrite is where an
 *      off-by-one lives - a threshold that does not sit on the lattice, a
 *      ceiling that should have been a floor - so every threshold and every
 *      ordered pair of endpoints is compared against the naive reference, not a
 *      sample of them.
 *
 * The integer division in the predicate is the whole risk surface:
 *
 *     v > t         <=>  i >  (t - base) / stride          floor
 *     v in [lo,hi]  <=>  i in [ceil((lo-base)/stride), floor((hi-base)/stride)]
 *
 * A threshold strictly between two lattice points must land on the same side in
 * both domains. That is what the sweeps below are for.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smart2raw.h"

static long pass=0, fail=0;
#define CHECK(c, ...) do{ if(c){pass++;} else { if(fail<25){printf("  [FAIL] "); printf(__VA_ARGS__); printf("\n");} fail++; } }while(0)

static uint64_t rs=0x243F6A8885A308D3ull;
static uint64_t rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return rs; }


/* Limiares candidatos: é onde um erro de arredondamento pode existir. Para cada
 * valor distinto da coluna tomamos v-1, v e v+1 - o ponto da malha e os dois
 * vizinhos imediatos, que NÃO estão na malha - mais as bordas. Varrer todo
 * inteiro de uma coluna que vai a 1,7e9 não é rigor, é desperdício: entre dois
 * pontos da malha o predicado é constante, então um representante por intervalo
 * prova o mesmo que um milhão deles. */
#define MAXC 400
static size_t cand_build(const uint64_t *a, size_t n, uint64_t *out)
{
    uint64_t d[64]; size_t nd=0;
    for(size_t i=0;i<n && nd<64;i++){
        int seen=0; for(size_t k=0;k<nd;k++) if(d[k]==a[i]){ seen=1; break; }
        if(!seen) d[nd++]=a[i];
    }
    size_t m=0;
    for(size_t k=0;k<nd;k++){
        if(d[k]>0 && m<MAXC) out[m++]=d[k]-1;
        if(m<MAXC) out[m++]=d[k];
        if(d[k]<UINT64_MAX && m<MAXC) out[m++]=d[k]+1;
    }
    /* ordena e deduplica */
    for(size_t i=0;i<m;i++) for(size_t j=i+1;j<m;j++)
        if(out[j]<out[i]){ uint64_t t=out[i]; out[i]=out[j]; out[j]=t; }
    size_t w=0;
    for(size_t i=0;i<m;i++) if(!w||out[w-1]!=out[i]) out[w++]=out[i];
    return w;
}
static size_t cand_build_s(const int64_t *a, size_t n, int64_t *out)
{
    int64_t d[64]; size_t nd=0;
    for(size_t i=0;i<n && nd<64;i++){
        int seen=0; for(size_t k=0;k<nd;k++) if(d[k]==a[i]){ seen=1; break; }
        if(!seen) d[nd++]=a[i];
    }
    size_t m=0;
    for(size_t k=0;k<nd;k++){
        if(d[k]>INT64_MIN && m<MAXC) out[m++]=d[k]-1;
        if(m<MAXC) out[m++]=d[k];
        if(d[k]<INT64_MAX && m<MAXC) out[m++]=d[k]+1;
    }
    for(size_t i=0;i<m;i++) for(size_t j=i+1;j<m;j++)
        if(out[j]<out[i]){ int64_t t=out[i]; out[i]=out[j]; out[j]=t; }
    size_t w=0;
    for(size_t i=0;i<m;i++) if(!w||out[w-1]!=out[i]) out[w++]=out[i];
    return w;
}

/* ================= 1. detecção do passo ================================== */
static void test_detect(void)
{
    printf("-- detecção do passo comum --\n");
    {   uint64_t v[12]; for(int i=0;i<12;i++) v[i]=500+(uint64_t)i*1000;
        uint64_t b; CHECK(s2r_affine_detect(v,12,&b)==1000 && b==500, "progressão aritmética: passo 1000, base 500"); }
    {   uint64_t v[5]={7,7,7,7,7}; uint64_t b;
        CHECK(s2r_affine_detect(v,5,&b)==1 && b==7, "coluna constante: passo 1 (não há passo)"); }
    {   uint64_t v[4]={0,1,2,3}; uint64_t b;
        CHECK(s2r_affine_detect(v,4,&b)==1 && b==0, "sequência densa: passo 1"); }
    {   uint64_t v[4]={10,13,19,31}; uint64_t b;    /* offsets 0,3,9,21 -> gcd 3 */
        CHECK(s2r_affine_detect(v,4,&b)==3 && b==10, "offsets 0,3,9,21: passo 3"); }
    {   uint64_t v[3]={10,13,20}; uint64_t b;       /* offsets 0,3,10 -> gcd 1  */
        CHECK(s2r_affine_detect(v,3,&b)==1 && b==10, "offsets 0,3,10: sem passo comum"); }
    {   uint64_t v[1]={42}; uint64_t b;
        CHECK(s2r_affine_detect(v,1,&b)==1 && b==42, "um elemento"); }
    {   uint64_t b; CHECK(s2r_affine_detect(NULL,0,&b)==1 && b==0, "vazio"); }
    {   int64_t v[5]; for(int i=0;i<5;i++) v[i]=-1000+(int64_t)i*250; int64_t b;
        CHECK(s2r_affine_detect_signed(v,5,&b)==250 && b==-1000, "com sinal: base -1000, passo 250"); }
    {   /* passo enorme, perto do topo */
        uint64_t v[3]={0, 1ull<<62, 1ull<<63}; uint64_t b;
        CHECK(s2r_affine_detect(v,3,&b)==(1ull<<62) && b==0, "passo 2^62"); }
}

/* ================= 2. pool afim: varredura exaustiva ===================== */
/* Cada coluna é varrida em TODO limiar de lo-2 a hi+2, e em TODO par ordenado
 * de extremos dessa faixa. É onde mora o erro de arredondamento. */
static void sweep_affine_u(const char *nm, const uint64_t *a, size_t n,
                           uint64_t exp_stride)
{
    S2RAffine af;
    CHECK(s2r_affine_build(&af,a,n), "%s: build", nm);
    CHECK(af.stride==exp_stride, "%s: passo detectado %llu (esperado %llu)",
          nm, (unsigned long long)af.stride, (unsigned long long)exp_stride);

    int rt=1; size_t bad=0;
    for(size_t i=0;i<n;i++) if(s2r_affine_get(&af,i)!=a[i]){ rt=0; bad=i; break; }
    CHECK(rt, "%s: round trip (índice %zu)", nm, bad);

    uint64_t mn=a[0], mx=a[0], sum=0;
    for(size_t i=0;i<n;i++){ if(a[i]<mn)mn=a[i]; if(a[i]>mx)mx=a[i]; sum+=a[i]; }
    CHECK(s2r_affine_sum(&af)==sum, "%s: sum", nm);

    static uint64_t cd[MAXC]; size_t nc=cand_build(a,n,cd);
    int okg=1, okl=1, oke=1; uint64_t bt=0;
    for(size_t k=0;k<nc;k++){
        uint64_t t=cd[k]; size_t cg=0, cl=0, ce=0;
        for(size_t i=0;i<n;i++){ cg+=(a[i]>t); cl+=(a[i]<t); ce+=(a[i]==t); }
        if(s2r_affine_count_gt(&af,(int64_t)t)!=cg){ okg=0; bt=t; break; }
        if(s2r_affine_count_lt(&af,(int64_t)t)!=cl){ okl=0; bt=t; break; }
        if(s2r_affine_count_eq(&af,(int64_t)t)!=ce){ oke=0; bt=t; break; }
    }
    CHECK(okg, "%s: count_gt em %zu limiares (falhou em %llu)", nm, nc, (unsigned long long)bt);
    CHECK(okl, "%s: count_lt em %zu limiares (falhou em %llu)", nm, nc, (unsigned long long)bt);
    CHECK(oke, "%s: count_eq em %zu limiares (falhou em %llu)", nm, nc, (unsigned long long)bt);

    /* todo par ordenado dos limiares candidatos */
    int okr=1, oks=1; uint64_t bl=0,bh=0;
    for(size_t x=0;x<nc && okr && oks;x++)
    for(size_t y=x;y<nc && okr && oks;y++){
        uint64_t l=cd[x], h=cd[y];
        size_t cr=0; uint64_t sr=0;
        for(size_t i=0;i<n;i++) if(a[i]>=l && a[i]<=h){ cr++; sr+=a[i]; }
        if(s2r_affine_count_range(&af,(int64_t)l,(int64_t)h)!=cr){ okr=0; bl=l; bh=h; }
        else if(s2r_affine_sum_if(&af,(int64_t)l,(int64_t)h)!=sr){ oks=0; bl=l; bh=h; }
    }
    CHECK(okr, "%s: count_range em %zu pares (falhou em [%llu,%llu])", nm, nc*(nc+1)/2,
          (unsigned long long)bl,(unsigned long long)bh);
    CHECK(oks, "%s: sum_if em %zu pares (falhou em [%llu,%llu])", nm, nc*(nc+1)/2,
          (unsigned long long)bl,(unsigned long long)bh);

    s2r_affine_free(&af);
}

static void sweep_affine_s(const char *nm, const int64_t *a, size_t n, uint64_t exp_stride)
{
    S2RAffine af;
    CHECK(s2r_affine_build_signed(&af,a,n), "%s: build", nm);
    CHECK(af.stride==exp_stride, "%s: passo %llu (esperado %llu)", nm,
          (unsigned long long)af.stride, (unsigned long long)exp_stride);

    int rt=1;
    for(size_t i=0;i<n;i++) if(s2r_affine_get_signed(&af,i)!=a[i]){ rt=0; break; }
    CHECK(rt, "%s: round trip com sinal", nm);

    int64_t mn=a[0], mx=a[0]; uint64_t sum=0;
    for(size_t i=0;i<n;i++){ if(a[i]<mn)mn=a[i]; if(a[i]>mx)mx=a[i]; sum+=(uint64_t)a[i]; }
    CHECK(s2r_affine_sum_signed(&af)==(int64_t)sum, "%s: sum com sinal", nm);

    static int64_t cs[MAXC]; size_t nc=cand_build_s(a,n,cs);
    int okg=1, okl=1, oke=1, okr=1; int64_t bt=0;
    for(size_t k=0;k<nc;k++){
        int64_t t=cs[k]; size_t cg=0, cl=0, ce=0;
        for(size_t i=0;i<n;i++){ cg+=(a[i]>t); cl+=(a[i]<t); ce+=(a[i]==t); }
        if(s2r_affine_count_gt(&af,t)!=cg){ okg=0; bt=t; break; }
        if(s2r_affine_count_lt(&af,t)!=cl){ okl=0; bt=t; break; }
        if(s2r_affine_count_eq(&af,t)!=ce){ oke=0; bt=t; break; }
    }
    CHECK(okg, "%s: count_gt com sinal (falhou em %lld)", nm, (long long)bt);
    CHECK(okl, "%s: count_lt com sinal (falhou em %lld)", nm, (long long)bt);
    CHECK(oke, "%s: count_eq com sinal (falhou em %lld)", nm, (long long)bt);
    for(size_t x=0;x<nc && okr;x++)
    for(size_t y=x;y<nc && okr;y++){
        int64_t l=cs[x], h=cs[y];
        size_t cr=0; for(size_t i=0;i<n;i++) if(a[i]>=l && a[i]<=h) cr++;
        if(s2r_affine_count_range(&af,l,h)!=cr) okr=0;
    }
    CHECK(okr, "%s: count_range com sinal em %zu pares", nm, nc*(nc+1)/2);
    s2r_affine_free(&af);
}

/* ================= 3. camada em blocos com passo ========================= */
static void verify_blocked_u(const char *nm, const uint64_t *a, size_t n, size_t blk,
                             int expect_stride)
{
    S2RBlocked b;
    CHECK(s2r_blocked_build(&b,a,n,blk), "%s/blk%zu: build", nm, blk);

    /* um bloco de um elemento tem amplitude 0: não existe passo a fatorar, e
     * essa é a resposta certa, não uma falha de detecção */
    int exp_any = expect_stride && blk>1;
    int any=0; for(size_t bi=0;bi<b.nblocks;bi++) if(s2r_blocked_stride(&b,bi)>1) any=1;
    CHECK(any==exp_any, "%s/blk%zu: passo presente = %d (esperado %d)", nm, blk, any, exp_any);

    int rt=1; size_t bad=0;
    for(size_t i=0;i<n;i++) if(s2r_blocked_get(&b,i)!=a[i]){ rt=0; bad=i; break; }
    CHECK(rt, "%s/blk%zu: round trip (índice %zu)", nm, blk, bad);

    uint64_t sum=0, mx=0, mn=a[0];
    for(size_t i=0;i<n;i++){ sum+=a[i]; if(a[i]>mx)mx=a[i]; if(a[i]<mn)mn=a[i]; }
    CHECK(s2r_blocked_sum(&b)==sum,       "%s/blk%zu: sum por metadados", nm, blk);
    CHECK(s2r_blocked_sum_fast(&b)==sum,  "%s/blk%zu: sum pelo payload", nm, blk);
    CHECK(s2r_blocked_max(&b)==mx,        "%s/blk%zu: max", nm, blk);
    CHECK(s2r_blocked_min(&b)==mn,        "%s/blk%zu: min", nm, blk);

    /* limiares candidatos, incluindo os que não caem na malha */
    static uint64_t cd[MAXC]; size_t nc=cand_build(a,n,cd);
    int okg=1; uint64_t bt=0;
    for(size_t k=0;k<nc;k++){
        uint64_t t=cd[k]; size_t c=0;
        for(size_t i=0;i<n;i++) c+=(a[i]>t);
        if(s2r_blocked_count_gt(&b,t)!=c){ okg=0; bt=t; break; }
    }
    CHECK(okg, "%s/blk%zu: count_gt em %zu limiares (falhou em %llu)", nm, blk, nc,
          (unsigned long long)bt);
    s2r_blocked_free(&b);
}

static void verify_blocked_s(const char *nm, const int64_t *a, size_t n, size_t blk)
{
    S2RBlocked b;
    CHECK(s2r_blocked_build_signed(&b,a,n,blk), "%s/blk%zu: build com sinal", nm, blk);
    int rt=1;
    for(size_t i=0;i<n;i++) if(s2r_blocked_get_signed(&b,i)!=a[i]){ rt=0; break; }
    CHECK(rt, "%s/blk%zu: round trip com sinal", nm, blk);
    uint64_t sum=0; for(size_t i=0;i<n;i++) sum+=(uint64_t)a[i];
    CHECK(s2r_blocked_sum_signed(&b)==(int64_t)sum, "%s/blk%zu: sum com sinal", nm, blk);
    s2r_blocked_free(&b);
}

int main(void)
{
    printf("=== test_affine: fatoração afim (base + passo) ===\n");

    test_detect();

    printf("-- pool afim, varredura exaustiva --\n");
    {   /* o dado do regime B: 12 distintos em progressão, embaralhado */
        uint64_t a[600]; for(size_t i=0;i<600;i++) a[i]=500+(rnd()%12)*1000;
        sweep_affine_u("regime B (passo 1000)", a, 600, 1000); }
    {   /* passo 2: o menor passo não trivial */
        uint64_t a[300]; for(size_t i=0;i<300;i++) a[i]=10+(rnd()%40)*2;
        sweep_affine_u("passo 2", a, 300, 2); }
    {   /* passo 7: primo, e o limiar quase nunca cai na malha */
        uint64_t a[300]; for(size_t i=0;i<300;i++) a[i]=3+(rnd()%20)*7;
        sweep_affine_u("passo 7 (limiar fora da malha)", a, 300, 7); }
    {   /* sem passo comum: precisa se comportar exatamente como antes */
        uint64_t a[300]; for(size_t i=0;i<300;i++) a[i]=100+(rnd()%97);
        sweep_affine_u("sem passo comum", a, 300, 1); }
    {   /* base zero e passo um: o caso que já era ótimo */
        uint64_t a[200]; for(size_t i=0;i<200;i++) a[i]=rnd()%64;
        sweep_affine_u("base 0, passo 1", a, 200, 1); }
    {   /* coluna constante */
        uint64_t a[100]; for(size_t i=0;i<100;i++) a[i]=12345;
        sweep_affine_u("coluna constante", a, 100, 1); }
    {   /* dois valores distintos, passo grande */
        uint64_t a[100]; for(size_t i=0;i<100;i++) a[i]=(rnd()&1)?1000000:1000500;
        sweep_affine_u("dois valores, passo 500", a, 100, 500); }
    {   int64_t a[300]; for(size_t i=0;i<300;i++) a[i]=-500+(int64_t)(rnd()%25)*25;
        sweep_affine_s("com sinal, base -500, passo 25", a, 300, 25); }
    {   int64_t a[200]; for(size_t i=0;i<200;i++) a[i]=-100+(int64_t)(rnd()%77);
        sweep_affine_s("com sinal, sem passo", a, 200, 1); }

    printf("-- camada em blocos com passo --\n");
    {   uint64_t a[4000]; for(size_t i=0;i<4000;i++) a[i]=500+(rnd()%12)*1000;
        for(size_t blk=1; blk<=4096; blk*=4) verify_blocked_u("regime B", a, 4000, blk, 1); }
    {   /* timestamps a cada 60 s: o caso real mais comum */
        uint64_t a[2000]; for(size_t i=0;i<2000;i++) a[i]=1700000000ull+(uint64_t)i*60;
        for(size_t blk=64; blk<=2048; blk*=4) verify_blocked_u("timestamps 60 s", a, 2000, blk, 1); }
    {   /* sem passo: o comportamento de 3.4.0 precisa ficar intacto */
        uint64_t a[2000]; for(size_t i=0;i<2000;i++) a[i]=9000000000ull+(rnd()%1000);
        for(size_t blk=64; blk<=2048; blk*=4) verify_blocked_u("banda em 9e9, sem passo", a, 2000, blk, 0); }
    {   /* blocos ordenados com passo: exercita a busca binária no domínio do índice */
        uint64_t a[4096]; for(size_t i=0;i<4096;i++) a[i]=1000+(uint64_t)(i%512)*8;
        for(size_t blk=512; blk<=4096; blk*=2) verify_blocked_u("blocos ordenados, passo 8", a, 4096, blk, 1); }
    {   int64_t a[2000]; for(size_t i=0;i<2000;i++) a[i]=-1000000+(int64_t)(rnd()%500)*400;
        for(size_t blk=64; blk<=1024; blk*=4) verify_blocked_s("com sinal, passo 400", a, 2000, blk); }
    {   /* um elemento por bloco, e bloco maior que a coluna */
        uint64_t a[7]={5,15,25,35,45,55,65};
        verify_blocked_u("7 elementos", a, 7, 1, 1);
        verify_blocked_u("7 elementos", a, 7, 100, 1); }
    {   /* extremos de u64 */
        uint64_t a[4]={0, 1ull<<62, 1ull<<63, (3ull<<62)};
        verify_blocked_u("extremos u64, passo 2^62", a, 4, 4, 1); }
    {   int64_t a[4]={INT64_MIN, INT64_MIN+2, INT64_MIN+4, INT64_MIN+6};
        verify_blocked_s("perto de INT64_MIN, passo 2", a, 4, 4); }

#if S2R_HAS_STDIO
    printf("-- serialização: fmt 2 sem passo, fmt 3 com passo --\n");
    {
        struct { const char *nm; int stride; } cs[2] = {
            {"col_sem_passo.s2r",0}, {"col_com_passo.s2r",1} };
        for(int k=0;k<2;k++){
            uint64_t *a=(uint64_t*)malloc(3000*sizeof(uint64_t));
            for(size_t i=0;i<3000;i++)
                a[i]= cs[k].stride ? 500+(rnd()%12)*1000 : 500+(rnd()%997);
            S2RBlocked b, c;
            CHECK(s2r_blocked_build(&b,a,3000,256), "%s: build", cs[k].nm);
            CHECK(s2r_blocked_save(&b,cs[k].nm)==S2R_OK, "%s: save", cs[k].nm);

            /* o byte de fmt precisa dizer a verdade */
            FILE *f=fopen(cs[k].nm,"rb"); uint8_t h[8]={0};
            size_t got = f? fread(h,1,8,f):0; if(f) fclose(f);
            CHECK(got==8 && h[6]==(cs[k].stride?3:2), "%s: fmt = %u (esperado %d)",
                  cs[k].nm, h[6], cs[k].stride?3:2);

            CHECK(s2r_blocked_load(&c,cs[k].nm)==S2R_OK, "%s: load", cs[k].nm);
            int same=1;
            for(size_t i=0;i<3000;i++) if(s2r_blocked_get(&b,i)!=s2r_blocked_get(&c,i)){ same=0; break; }
            CHECK(same, "%s: round trip idêntico", cs[k].nm);
            CHECK(s2r_blocked_sum(&c)==s2r_blocked_sum(&b), "%s: sum preservada", cs[k].nm);
            CHECK(s2r_blocked_count_gt(&c,5500)==s2r_blocked_count_gt(&b,5500),
                  "%s: count_gt preservado", cs[k].nm);
            CHECK(c.has_stride==(cs[k].stride?1:0), "%s: has_stride preservado", cs[k].nm);
            s2r_blocked_free(&b); s2r_blocked_free(&c); free(a);
            remove(cs[k].nm);
        }
    }
    {   /* um passo zero em disco divide por zero: precisa ser recusado */
        uint64_t a[512]; for(size_t i=0;i<512;i++) a[i]=100+(rnd()%8)*50;
        S2RBlocked b, c;
        CHECK(s2r_blocked_build(&b,a,512,128), "passo zero: build");
        CHECK(b.has_stride==1, "passo zero: a coluna tem passo");
        CHECK(s2r_blocked_save(&b,"z.s2r")==S2R_OK, "passo zero: save");
        s2r_blocked_free(&b);
        /* zera o primeiro byte do vetor de passos: ele vem depois de
         * bclass+bflags+bbase+bspan+boff+bsum, então localiza-se por varredura:
         * qualquer byte alterado tem de ser pego pelo CRC de qualquer forma */
        FILE *f=fopen("z.s2r","r+b");
        int rejected=1;
        if(f){
            fseek(f,0,SEEK_END); long sz=ftell(f);
            for(long p=45; p<sz-4 && rejected; p+=17){
                fseek(f,p,SEEK_SET); int old=fgetc(f);
                fseek(f,p,SEEK_SET); fputc(old^0xFF,f); fflush(f);
                S2RError e=s2r_blocked_load(&c,"z.s2r");
                if(e==S2R_OK){ s2r_blocked_free(&c); rejected=0; }
                fseek(f,p,SEEK_SET); fputc(old,f); fflush(f);
            }
            fclose(f);
        }
        CHECK(rejected, "todo byte alterado do corpo é recusado (CRC ou validação)");
        remove("z.s2r");
    }
    {   /* leitor plano e leitor em blocos continuam recusando o arquivo um do outro */
        uint64_t a[300]; for(size_t i=0;i<300;i++) a[i]=7+(rnd()%9)*3;
        S2RBlocked b; S2RPool p;
        CHECK(s2r_blocked_build(&b,a,300,64), "recusa cruzada: build");
        CHECK(s2r_blocked_save(&b,"x.s2r")==S2R_OK, "recusa cruzada: save");
        CHECK(s2r_load_portable(&p,"x.s2r")!=S2R_OK, "leitor plano recusa fmt 3");
        s2r_blocked_free(&b); remove("x.s2r");
    }
#endif

    printf("------------------------------------------------------------\n");
    printf("  %ld OK, %ld FAIL\n", pass, fail);
    return fail? 1:0;
}
