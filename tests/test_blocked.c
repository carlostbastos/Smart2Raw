/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Test of the block-wise width module (PFOR) - v3.3.3 */
#include <stdio.h>
#include <stdlib.h>
#include "smart2raw.h"

static int ok=0, fail=0;
#define CHECK(c,m) do{ if(c)ok++; else{fail++; printf("  FAIL: %s\n",(m));} }while(0)

static size_t whole_bytes(const uint64_t*a,size_t n){
    uint64_t mx=0; for(size_t i=0;i<n;i++) if(a[i]>mx)mx=a[i];
    return (size_t)(s2r_classify(mx)/8)*n;   /* single width (current approach) */
}

int main(void){
    /* 1) correctness: round-trip, sum, max on varied data */
    {
        size_t n=10000; uint64_t *a=(uint64_t*)malloc(n*sizeof(uint64_t));
        uint64_t s=0,mx=0;
        for(size_t i=0;i<n;i++){ a[i]=(i*2654435761u)%1000; s+=a[i]; if(a[i]>mx)mx=a[i]; }
        S2RBlocked b; CHECK(s2r_blocked_build(&b,a,n,256),"build");
        int rt=1; for(size_t i=0;i<n;i++) if(s2r_blocked_get(&b,i)!=a[i]){rt=0;break;}
        CHECK(rt,"get(i) == src[i] para todo i");
        CHECK(s2r_blocked_sum(&b)==s,"sum == naive sum");
        CHECK(s2r_blocked_max(&b)==mx,"max == naive max");
        s2r_blocked_free(&b); free(a);
    }
    /* 2) edges: empty, 1 element, partial last block */
    {
        S2RBlocked b; CHECK(s2r_blocked_build(&b,NULL,0,256),"empty build");
        CHECK(b.count==0 && s2r_blocked_sum(&b)==0,"empty: sum 0"); s2r_blocked_free(&b);
        uint64_t one[1]={42}; S2RBlocked c; s2r_blocked_build(&c,one,1,256);
        CHECK(s2r_blocked_get(&c,0)==42 && c.nblocks==1,"1 elemento"); s2r_blocked_free(&c);
        uint64_t p[300]; for(int i=0;i<300;i++)p[i]=(uint64_t)i; S2RBlocked d; s2r_blocked_build(&d,p,300,256);
        CHECK(d.nblocks==2 && s2r_blocked_get(&d,299)==299,"partial last block ok"); s2r_blocked_free(&d);
    }
    /* 3) memory gain: clean (no benefit) vs outliers (recovers) */
    {
        size_t n=4000000; uint64_t *a=(uint64_t*)malloc(n*sizeof(uint64_t));
        srand(7);
        /* base 0..200 (u8) */
        for(size_t i=0;i<n;i++) a[i]=(uint64_t)(rand()%201);
        S2RBlocked clean; s2r_blocked_build(&clean,a,n,256);
        double r_clean=(double)whole_bytes(a,n)/(double)s2r_blocked_bytes(&clean);
        printf("  clean:    single=%.2f MB  block=%.2f MB  -> %.2fx\n",
               whole_bytes(a,n)/1e6, s2r_blocked_bytes(&clean)/1e6, r_clean);
        CHECK(r_clean>0.95 && r_clean<1.10,"clean data: ~tie (no harm)");
        s2r_blocked_free(&clean);
        /* inject 0.01% large outliers (force 32 bits in the single width) */
        size_t k=n/10000;  /* 0,01% */
        for(size_t j=0;j<k;j++) a[(size_t)rand()%n]=(uint64_t)(70000+rand()%400000000);
        S2RBlocked out; s2r_blocked_build(&out,a,n,256);
        /* correctness under outliers: sum still exact */
        uint64_t sref=0; for(size_t i=0;i<n;i++) sref+=a[i];
        CHECK(s2r_blocked_sum(&out)==sref,"exact sum even with outliers");
        double r_out=(double)whole_bytes(a,n)/(double)s2r_blocked_bytes(&out);
        printf("  outliers: single=%.2f MB  block=%.2f MB  -> %.2fx smaller\n",
               whole_bytes(a,n)/1e6, s2r_blocked_bytes(&out)/1e6, r_out);
        CHECK(r_out>3.0,"0.01%% outliers: recovers >3x memory");
        s2r_blocked_free(&out); free(a);
    }
    printf("=== %d OK, %d FAIL ===\n",ok,fail);
    return fail?1:0;
}
