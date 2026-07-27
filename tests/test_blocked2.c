/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "smart2raw.h"
static int ok=0,fail=0;
#define CHECK(c,m) do{ if(c)ok++; else{fail++;printf("  FAIL: %s\n",(m));} }while(0)
static double ms(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec*1e-6; }

int main(void){
    /* 1) signed PFOR: round-trip, sum, and recovery under outliers */
    {
        size_t n=100000; int64_t *a=(int64_t*)malloc(n*sizeof(int64_t));
        int64_t s=0;
        for(size_t i=0;i<n;i++){ a[i]=((int64_t)(i%200))-100; s+=a[i]; }  /* -100..99 fits in I8 */
        S2RBlocked b; CHECK(s2r_blocked_build_signed(&b,a,n,256),"build_signed");
        int rt=1; for(size_t i=0;i<n;i++) if(s2r_blocked_get_signed(&b,i)!=a[i]){rt=0;break;}
        CHECK(rt,"get_signed(i)==src[i]");
        CHECK(s2r_blocked_sum_signed(&b)==s,"sum_signed exact");
        size_t b_clean=s2r_blocked_bytes(&b);
        s2r_blocked_free(&b);
        /* inject large negative outliers -> single width would go to I32 */
        for(size_t j=0;j<n/10000;j++) a[(size_t)rand()%n] = -(int64_t)(100000+rand()%300000000);
        S2RBlocked o; s2r_blocked_build_signed(&o,a,n,256);
        int64_t sref=0; for(size_t i=0;i<n;i++) sref+=a[i];
        CHECK(s2r_blocked_sum_signed(&o)==sref,"sum_signed exact with outliers");
        size_t single_w=4*n; /* I32 at the single width */
        printf("  signed: clean=%zu B  outliers: single=%zu B  block=%zu B -> %.2fx\n",
               b_clean, single_w, s2r_blocked_bytes(&o), (double)single_w/s2r_blocked_bytes(&o));
        CHECK(s2r_blocked_bytes(&o) < single_w/2,"signed PFOR <50% of the single width under outliers");
        s2r_blocked_free(&o); free(a);
    }
    /* 2) accelerated block-wise sum: == scalar sum, and measures throughput */
    {
        size_t n=8000000; uint64_t *a=(uint64_t*)malloc(n*sizeof(uint64_t));
        uint64_t s=0;
        for(size_t i=0;i<n;i++){ a[i]=(uint64_t)(i%200); s+=a[i]; }      /* tudo u8 */
        a[12345]=70000;  /* one block becomes u32 -> tests the mixed path */
        s=0; for(size_t i=0;i<n;i++) s+=a[i];
        S2RBlocked b; s2r_blocked_build(&b,a,n,1024);
        CHECK(s2r_blocked_sum_fast(&b)==s,"sum_fast == exact sum (mixed u8/u32 blocks)");
        CHECK(s2r_blocked_sum(&b)==s,"scalar sum == exact sum");
        double bf=1e18,bs=1e18; volatile uint64_t sink=0;
        for(int r=0;r<15;r++){ double t=ms(); sink+=s2r_blocked_sum_fast(&b); double d=ms()-t; if(d<bf)bf=d; }
        for(int r=0;r<15;r++){ double t=ms(); sink+=s2r_blocked_sum(&b);      double d=ms()-t; if(d<bs)bs=d; }
        printf("  block-wise sum: SIMD=%.2f ms (%.2f G/s)  scalar=%.2f ms (%.2f G/s)  %.2fx\n",
               bf, n/1e6/bf, bs, n/1e6/bs, bs/bf);
        s2r_blocked_free(&b); free(a); (void)sink;
    }
    printf("=== %d OK, %d FAIL ===\n",ok,fail);
    return fail?1:0;
}
