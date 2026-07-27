/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* test_pfor_frame.c - the block-wise layer must store values RELATIVE to each
 * block's own minimum, and every operation must be indistinguishable from the
 * naive one that does not.
 *
 * Until v3.4.0 a block's class came from its maximum alone, so a block of
 * { 9000000000, 9000000001, ... } was stored as u64 even though it spans 1. The
 * frame of reference makes that base + u8 deltas. The old behaviour is the
 * special case base == 0, which is why data starting at zero must come out
 * byte-identical - that is checked below too, because a "win" that quietly
 * regresses the common case is not a win.
 *
 * A block whose values are all equal now has delta width 0 and stores NO payload.
 * That path is exercised on purpose: constant blocks are where an off-by-one in
 * the width logic hides.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smart2raw.h"

static long pass=0, fail=0;
#define CHECK(c, ...) do{ if(c){pass++;} else { if(fail<20){printf("  [FAIL] "); printf(__VA_ARGS__); printf("\n");} fail++; } }while(0)

static uint64_t rs=0x9E3779B97F4A7C15ull;
static uint64_t rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return rs; }

/* every accessor and aggregate against the naive reference */
static void verify_u(const char *nm, const uint64_t *a, size_t n, size_t blk)
{
    S2RBlocked b;
    CHECK(s2r_blocked_build(&b,a,n,blk), "%s: build", nm);
    if(!b.count && n){ return; }

    uint64_t sum=0, mx=0;
    for(size_t i=0;i<n;i++){ sum+=a[i]; if(a[i]>mx) mx=a[i]; }

    int rt=1; size_t bad=0;
    for(size_t i=0;i<n;i++) if(s2r_blocked_get(&b,i)!=a[i]){ rt=0; bad=i; break; }
    CHECK(rt, "%s: get(%zu) mismatch", nm, bad);
    CHECK(s2r_blocked_sum(&b)==sum, "%s: sum", nm);
    CHECK(s2r_blocked_sum_fast(&b)==sum, "%s: sum_fast", nm);
    CHECK(s2r_blocked_max(&b)==mx, "%s: max", nm);

    /* the predicate must agree at every interesting threshold, including the two
     * that make a whole block fall on one side (the no-payload-access paths) */
    uint64_t thr[8]={0, 1, mx/2, mx?mx-1:0, mx, mx+1, sum/(n?n:1), ~0ull};
    for(int t=0;t<8;t++){
        uint64_t ref=0; for(size_t i=0;i<n;i++) ref += (a[i]>thr[t]);
        CHECK(s2r_blocked_count_gt(&b,thr[t])==ref,
              "%s: count_gt(%llu) got %zu want %llu", nm,
              (unsigned long long)thr[t], s2r_blocked_count_gt(&b,thr[t]),
              (unsigned long long)ref);
    }
    s2r_blocked_free(&b);
}

static void verify_s(const char *nm, const int64_t *a, size_t n, size_t blk)
{
    S2RBlocked b;
    CHECK(s2r_blocked_build_signed(&b,a,n,blk), "%s: build_signed", nm);
    uint64_t sum=0;                                   /* unsigned: no UB */
    for(size_t i=0;i<n;i++) sum += (uint64_t)a[i];
    int rt=1; size_t bad=0;
    for(size_t i=0;i<n;i++) if(s2r_blocked_get_signed(&b,i)!=a[i]){ rt=0; bad=i; break; }
    CHECK(rt, "%s: get_signed(%zu) mismatch", nm, bad);
    CHECK(s2r_blocked_sum_signed(&b)==(int64_t)sum, "%s: sum_signed", nm);
    s2r_blocked_free(&b);
}

int main(void)
{
    enum { N = 60000 };
    static uint64_t u[N];
    static int64_t  s[N];

    /* ---- the case the frame of reference exists for: a narrow band far from 0 ---- */
    for(size_t i=0;i<N;i++) u[i] = 9000000000ull + (rnd()%50);
    verify_u("band at 9e9", u, N, 256);
    {   /* and it must actually be SMALL, not merely correct */
        S2RBlocked b; s2r_blocked_build(&b,u,N,256);
        CHECK(s2r_blocked_bytes(&b) < N*2, "band at 9e9: payload should be ~1 byte/elem, got %.2f",
              (double)s2r_blocked_bytes(&b)/N);
        s2r_blocked_free(&b);
    }

    /* ---- the common case must not regress: base is already 0 ---- */
    for(size_t i=0;i<N;i++) u[i] = rnd()%201;
    verify_u("uniform 0..200", u, N, 256);
    {
        S2RBlocked b; s2r_blocked_build(&b,u,N,256);
        /* one byte per element plus bookkeeping; anything above 1.10x is a regression */
        double ratio = (double)s2r_blocked_bytes(&b) / (double)N;
        CHECK(ratio < 1.10, "uniform 0..200: %.3f bytes/elem, expected ~1.0", ratio);
        s2r_blocked_free(&b);
    }

    /* ---- constant blocks: delta width 0, no payload at all ---- */
    for(size_t i=0;i<N;i++) u[i] = 777;
    verify_u("constant column", u, N, 256);
    {
        S2RBlocked b; s2r_blocked_build(&b,u,N,256);
        CHECK(b.bytes == 0, "constant column must store zero payload bytes, got %zu", b.bytes);
        int allz=1; for(size_t i=0;i<b.nblocks;i++) if(b.bclass[i]!=0) allz=0;
        CHECK(allz, "constant column: every block must have delta width 0");
        s2r_blocked_free(&b);
    }
    /* half constant, half not - the mixed path */
    for(size_t i=0;i<N;i++) u[i] = (i/256)%2 ? 500 : 500 + (rnd()%300);
    verify_u("alternating constant/varying blocks", u, N, 256);

    /* ---- extremes: a block spanning the whole u64 range ---- */
    for(size_t i=0;i<N;i++) u[i] = (i%2) ? ~0ull : 0ull;
    verify_u("u64 full span per block", u, N, 256);
    for(size_t i=0;i<N;i++) u[i] = ~0ull;
    verify_u("all UINT64_MAX", u, N, 256);
    for(size_t i=0;i<N;i++) u[i] = rnd();
    verify_u("random u64 (no structure)", u, N, 256);

    /* ---- outliers: the reason PFOR exists ---- */
    for(size_t i=0;i<N;i++) u[i] = rnd()%200;
    for(size_t i=0;i<N;i+=5000) u[i] = 4000000000ull;
    verify_u("sparse outliers", u, N, 256);

    /* ---- block sizes, and n not a multiple of the block ---- */
    for(size_t i=0;i<N;i++) u[i] = 100000 + rnd()%61;
    for(size_t blk=1; blk<=4096; blk*=2) verify_u("varied block size", u, N-1, blk);
    verify_u("block larger than n", u, 100, 65536);

    /* ---- tiny inputs ---- */
    verify_u("n=1", u, 1, 256);
    verify_u("n=2", u, 2, 1);
    {   S2RBlocked b;
        CHECK(s2r_blocked_build(&b,NULL,0,256), "empty build");
        CHECK(b.count==0 && s2r_blocked_sum(&b)==0 && s2r_blocked_max(&b)==0, "empty aggregates");
        CHECK(s2r_blocked_count_gt(&b,0)==0, "empty count_gt");
        CHECK(s2r_blocked_get(&b,0)==0, "empty get");
        s2r_blocked_free(&b);
    }

    /* ---- signed: the frame of reference must sign-extend correctly ---- */
    for(size_t i=0;i<N;i++) s[i] = -9000000000ll + (int64_t)(rnd()%50);
    verify_s("signed band at -9e9", s, N, 256);
    for(size_t i=0;i<N;i++) s[i] = (int64_t)(rnd()%2000) - 1000;
    verify_s("signed mixed", s, N, 256);
    for(size_t i=0;i<N;i++) s[i] = (i%2) ? INT64_MAX : INT64_MIN;
    verify_s("signed full span per block", s, N, 256);
    for(size_t i=0;i<N;i++) s[i] = -42;
    verify_s("signed constant", s, N, 256);
    for(size_t i=0;i<N;i++) s[i] = (int64_t)rnd();
    verify_s("signed random", s, N, 256);

    /* ---- the accelerated sum must equal the scalar one on mixed-width blocks ---- */
    for(size_t i=0;i<N;i++) u[i] = (i/256)%3==0 ? rnd()%200
                                 : (i/256)%3==1 ? 70000 + rnd()%1000
                                                : 5000000000ull + rnd()%10;
    {
        S2RBlocked b; s2r_blocked_build(&b,u,N,256);
        CHECK(s2r_blocked_sum_fast(&b)==s2r_blocked_sum(&b), "mixed widths: sum_fast == sum");
        uint64_t ref=0; for(size_t i=0;i<N;i++) ref+=u[i];
        CHECK(s2r_blocked_sum(&b)==ref, "mixed widths: sum exact");
        s2r_blocked_free(&b);
    }

    /* ================= v3.4.0: zone stats, sorted blocks, serialization ======= */

    /* ---- zone sums: s2r_blocked_sum must never touch the payload, and must
     *      still equal the payload-scanning path exactly ---- */
    {
        const char *nm[] = {"random","sorted","constant","outliers","wide"};
        for(int k=0;k<5;k++){
            for(size_t i=0;i<N;i++){
                switch(k){
                    case 0: u[i]=rnd()%100000; break;
                    case 1: u[i]=1000000+i; break;
                    case 2: u[i]=999; break;
                    case 3: u[i]=rnd()%200; break;
                    case 4: u[i]=rnd(); break;
                }
            }
            if(k==3) for(size_t i=0;i<N;i+=3000) u[i]=3000000000ull;
            uint64_t ref=0; for(size_t i=0;i<N;i++) ref+=u[i];
            S2RBlocked b; s2r_blocked_build(&b,u,N,256);
            CHECK(s2r_blocked_sum(&b)==ref, "zone sum (%s)", nm[k]);
            CHECK(s2r_blocked_sum_fast(&b)==ref, "payload sum (%s) must agree", nm[k]);
            uint64_t rmax=0, rmin=~0ull;
            for(size_t i=0;i<N;i++){ if(u[i]>rmax) rmax=u[i]; if(u[i]<rmin) rmin=u[i]; }
            CHECK(s2r_blocked_max(&b)==rmax, "zone max (%s)", nm[k]);
            CHECK(s2r_blocked_min(&b)==rmin, "zone min (%s)", nm[k]);
            s2r_blocked_free(&b);
        }
    }
    /* signed zone sums, including a column whose total wraps int64 */
    {
        for(size_t i=0;i<N;i++) s[i]=(int64_t)(rnd()%2000)-1000;
        uint64_t ref=0; for(size_t i=0;i<N;i++) ref+=(uint64_t)s[i];
        S2RBlocked b; s2r_blocked_build_signed(&b,s,N,256);
        CHECK(s2r_blocked_sum_signed(&b)==(int64_t)ref, "signed zone sum");
        CHECK(s2r_blocked_sum_fast(&b)==ref, "signed payload sum must agree");
        s2r_blocked_free(&b);
    }

    /* ---- sorted blocks: the flag must be set exactly when it is true, and the
     *      binary-search path must agree with the scanning path everywhere ---- */
    {
        for(size_t i=0;i<N;i++) u[i]=1000000+i;          /* every block sorted */
        S2RBlocked b; s2r_blocked_build(&b,u,N,256);
        int all=1; for(size_t i=0;i<b.nblocks;i++) if(!s2r_blocked_is_sorted(&b,i)) all=0;
        CHECK(all, "ascending column: every block must be flagged sorted");
        for(int t=0;t<40;t++){
            uint64_t thr = 999000 + (rnd()% (N+4000));
            uint64_t ref=0; for(size_t i=0;i<N;i++) ref += (u[i]>thr);
            CHECK(s2r_blocked_count_gt(&b,thr)==ref,
                  "sorted count_gt(%llu)", (unsigned long long)thr);
        }
        s2r_blocked_free(&b);
    }
    {   /* the flag must be exactly right per block, not approximately: compute the
         * expected value from the source and compare block by block. The last
         * block is partial, so a pattern that dips every 256 elements may miss it -
         * asserting "no block is sorted" would be testing the pattern, not the code. */
        for(size_t i=0;i<N;i++) u[i]=1000+i;
        for(size_t i=128;i<N;i+=256) u[i]=0;
        S2RBlocked b; s2r_blocked_build(&b,u,N,256);
        int agree=1; size_t whichb=0;
        for(size_t bi=0; bi<b.nblocks; bi++){
            size_t st=bi*256, ln=(st+256<=N)?256:(N-st), want=1;
            for(size_t i=1;i<ln;i++) if(u[st+i]<u[st+i-1]){ want=0; break; }
            if((size_t)s2r_blocked_is_sorted(&b,bi)!=want){ agree=0; whichb=bi; break; }
        }
        CHECK(agree, "sorted flag wrong on block %zu", whichb);
        for(int t=0;t<20;t++){
            uint64_t thr=rnd()%(N+2000);
            uint64_t ref=0; for(size_t i=0;i<N;i++) ref += (u[i]>thr);
            CHECK(s2r_blocked_count_gt(&b,thr)==ref, "unsorted count_gt");
        }
        s2r_blocked_free(&b);
    }
    {   /* plateaus: non-decreasing with repeats is still sorted, and the binary
         * search must land past the LAST equal element, not the first */
        for(size_t i=0;i<N;i++) u[i]=(i/64)*10;
        S2RBlocked b; s2r_blocked_build(&b,u,N,256);
        for(uint64_t thr=0; thr<(N/64)*10+20; thr+=5){
            uint64_t ref=0; for(size_t i=0;i<N;i++) ref += (u[i]>thr);
            CHECK(s2r_blocked_count_gt(&b,thr)==ref,
                  "plateau count_gt(%llu)", (unsigned long long)thr);
        }
        s2r_blocked_free(&b);
    }

    /* ---- the binary-search path must be REACHED and must agree with the scan.
     *      With the default gate (512 payload bytes) a 256-element block of u8
     *      deltas takes the scan, so a test at the default block size never runs
     *      the binary search at all. This uses blocks large enough to cross the
     *      gate, and cross-checks against a build where the gate is impossible to
     *      cross - the two must produce identical answers for every threshold. ---- */
    {
        for(size_t i=0;i<N;i++) u[i] = (i%4096)*4;      /* sawtooth: every block straddles */
        S2RBlocked b; s2r_blocked_build(&b,u,N,4096);
        size_t big=0;
        for(size_t bi=0; bi<b.nblocks; bi++){
            size_t ln=(bi*4096+4096<=N)?4096:(N-bi*4096), w=b.bclass[bi]>>3;
            if(s2r_blocked_is_sorted(&b,bi) && ln*w>=S2R_BLK_BSEARCH_MIN_BYTES) big++;
        }
        /* only meaningful when the gate is set low enough for this test to cross
         * it; the suite is also built with the gate raised out of reach, and that
         * build must still agree on every threshold below */
        if(S2R_BLK_BSEARCH_MIN_BYTES <= 4096u)
            CHECK(big>0, "the binary-search gate must actually be crossed by this test");
        for(uint64_t thr=0; thr<4096*4+8; thr+=7){
            uint64_t ref=0; for(size_t i=0;i<N;i++) ref += (u[i]>thr);
            CHECK(s2r_blocked_count_gt(&b,thr)==ref,
                  "large sorted block count_gt(%llu)", (unsigned long long)thr);
        }
        /* plateaus inside a large sorted block: the search must land past the LAST
         * equal element */
        for(size_t i=0;i<N;i++) u[i] = ((i%4096)/16)*3;
        S2RBlocked c; s2r_blocked_build(&c,u,N,4096);
        for(uint64_t thr=0; thr<(4096/16)*3+6; thr+=1){
            uint64_t ref=0; for(size_t i=0;i<N;i++) ref += (u[i]>thr);
            CHECK(s2r_blocked_count_gt(&c,thr)==ref,
                  "large sorted plateau count_gt(%llu)", (unsigned long long)thr);
        }
        s2r_blocked_free(&b); s2r_blocked_free(&c);
    }

    /* ---- serialization round trip ---- */
    {
        const char *path="/tmp/s2r_blk_rt.s2r";
        struct { const char *nm; int sgn; } cases[] = {
            {"unsigned random",0},{"sorted",0},{"constant",0},{"outliers",0},{"signed",1}};
        for(int k=0;k<5;k++){
            for(size_t i=0;i<N;i++){
                switch(k){ case 0: u[i]=rnd()%70000; break;
                           case 1: u[i]=9000000000ull+i; break;
                           case 2: u[i]=7; break;
                           case 3: u[i]=rnd()%200; break;
                           default: s[i]=(int64_t)(rnd()%4000)-2000; break; }
            }
            if(k==3) for(size_t i=0;i<N;i+=2000) u[i]=4000000000ull;
            S2RBlocked a,c;
            if(cases[k].sgn) s2r_blocked_build_signed(&a,s,N,256);
            else             s2r_blocked_build(&a,u,N,256);
            CHECK(s2r_blocked_save(&a,path)==S2R_OK, "save (%s)", cases[k].nm);
            CHECK(s2r_blocked_load(&c,path)==S2R_OK, "load (%s)", cases[k].nm);
            CHECK(a.count==c.count && a.block==c.block && a.nblocks==c.nblocks
                  && a.bytes==c.bytes && a.is_signed==c.is_signed, "header (%s)", cases[k].nm);
            CHECK(s2r_blocked_bytes(&a)==s2r_blocked_bytes(&c), "footprint (%s)", cases[k].nm);
            CHECK(s2r_blocked_sum(&a)==s2r_blocked_sum(&c), "sum (%s)", cases[k].nm);
            CHECK(s2r_blocked_sum_fast(&c)==s2r_blocked_sum(&c),
                  "loaded payload agrees with loaded zone sums (%s)", cases[k].nm);
            int same=1;
            for(size_t i=0;i<N;i++){
                if(cases[k].sgn){ if(s2r_blocked_get_signed(&a,i)!=s2r_blocked_get_signed(&c,i)){same=0;break;} }
                else            { if(s2r_blocked_get(&a,i)!=s2r_blocked_get(&c,i)){same=0;break;} }
            }
            CHECK(same, "every element survives the round trip (%s)", cases[k].nm);
            int fl=1; for(size_t i=0;i<a.nblocks;i++)
                if(s2r_blocked_is_sorted(&a,i)!=s2r_blocked_is_sorted(&c,i)) fl=0;
            CHECK(fl, "sorted flags survive (%s)", cases[k].nm);
            CHECK(s2r_blocked_count_gt(&a,100)==s2r_blocked_count_gt(&c,100),
                  "predicate agrees after reload (%s)", cases[k].nm);
            s2r_blocked_free(&a); s2r_blocked_free(&c);
        }
    }
    /* a v3.3 reader must refuse a blocked file, and vice versa */
    {
        const char *bp="/tmp/s2r_blk_x.s2r", *fp="/tmp/s2r_flat_x.s2r";
        for(size_t i=0;i<1000;i++) u[i]=i%250;
        S2RBlocked a; s2r_blocked_build(&a,u,1000,64); s2r_blocked_save(&a,bp);
        S2RPool p; CHECK(s2r_load_portable(&p,bp)!=S2R_OK, "flat reader must reject a blocked file");
#if S2R_HAS_MMAP
        S2RMap m; CHECK(s2r_map_open(&m,bp,1)!=S2R_OK, "mmap reader must reject a blocked file");
#endif
        s2r_pool_init(&p,S2R_8,4); s2r_push_adaptive(&p,9); s2r_save_portable(&p,fp); s2r_pool_free(&p);
        S2RBlocked c; CHECK(s2r_blocked_load(&c,fp)!=S2R_OK, "blocked reader must reject a flat file");
        s2r_blocked_free(&a);
    }
    /* corruption anywhere in metadata or payload must be caught */
    {
        const char *path="/tmp/s2r_blk_c.s2r";
        for(size_t i=0;i<4000;i++) u[i]=rnd()%50000;
        S2RBlocked a; s2r_blocked_build(&a,u,4000,64);
        s2r_blocked_save(&a,path); s2r_blocked_free(&a);
        long sz; { FILE*f=fopen(path,"rb"); fseek(f,0,SEEK_END); sz=ftell(f); fclose(f); }
        long spots[3] = { 50, sz/2, sz-6 };          /* metadata, middle, payload */
        for(int k=0;k<3;k++){
            char cmd[256]; snprintf(cmd,sizeof cmd,"cp %s /tmp/s2r_blk_d.s2r",path);
            if(system(cmd)!=0) continue;
            FILE*f=fopen("/tmp/s2r_blk_d.s2r","r+b");
            fseek(f,spots[k],SEEK_SET); int ch=fgetc(f);
            fseek(f,spots[k],SEEK_SET); fputc(ch^0xFF,f); fclose(f);
            S2RBlocked c;
            CHECK(s2r_blocked_load(&c,"/tmp/s2r_blk_d.s2r")!=S2R_OK,
                  "CRC must catch a flipped byte at offset %ld", spots[k]);
        }
        { S2RBlocked c;
          if(system("head -c 40 /tmp/s2r_blk_c.s2r > /tmp/s2r_blk_t.s2r")==0)
            CHECK(s2r_blocked_load(&c,"/tmp/s2r_blk_t.s2r")!=S2R_OK, "truncated file rejected");
          if(system("cp /tmp/s2r_blk_c.s2r /tmp/s2r_blk_e.s2r && printf ZZZZ >> /tmp/s2r_blk_e.s2r")==0)
            CHECK(s2r_blocked_load(&c,"/tmp/s2r_blk_e.s2r")!=S2R_OK, "trailing bytes rejected"); }
    }
    {   /* an empty column must serialize and reload */
        S2RBlocked a,c;
        s2r_blocked_build(&a,NULL,0,256);
        CHECK(s2r_blocked_save(&a,"/tmp/s2r_blk_0.s2r")==S2R_OK, "save empty");
        CHECK(s2r_blocked_load(&c,"/tmp/s2r_blk_0.s2r")==S2R_OK, "load empty");
        CHECK(c.count==0 && s2r_blocked_sum(&c)==0, "empty round trip");
        s2r_blocked_free(&a); s2r_blocked_free(&c);
    }

    printf("=== %ld OK, %ld FAIL ===\n", pass, fail);
    return fail ? 1 : 0;
}
