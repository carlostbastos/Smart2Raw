/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "smart2raw.h"
static double ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static int pass=0,fail=0;
#define CHECK(c,m) do{ if(c){printf("  [OK]   %s\n",m);pass++;} else {printf("  [FAIL] %s\n",m);fail++;} }while(0)

int main(void){
    printf("=== #1 BIDIRECTIONAL WIDTH (self-healing) ===\n");
    {
        S2RPool p; s2r_pool_init(&p,S2R_8,8);
        for(int i=0;i<7;i++) s2r_push_adaptive(&p,(uint64_t)(i*10)); /* 0..60, fits u8 */
        s2r_push_adaptive(&p, 5000000000ULL);                        /* outlier -> u64 */
        CHECK(s2r_abs_size(p.size)==64, "outlier promoveu u8->u64");
        size_t bytes_u64 = p.byte_cap;
        /* remove the outlier (it's at the end) */
        s2r_remove_swap(&p, p.count-1);
        CHECK(s2r_max(&p)==60, "after removal, max is 60 again");
        /* self-healing: demote */
        s2r_fit_class(&p);
        CHECK(s2r_abs_size(p.size)==8, "fit_class rebaixou u64->u8");
        CHECK(p.byte_cap < bytes_u64, "memory recycled (byte_cap dropped)");
        int ok=1; for(int i=0;i<7;i++) if(s2r_get(&p,i)!=(uint64_t)(i*10)){ok=0;break;}
        CHECK(ok, "values 0..60 intact after up-and-down");
        printf("    trajectory: u8 -> (outlier) u64 -> (remove+fit) u8 ; bytes %zu->%zu\n", bytes_u64, p.byte_cap);
        s2r_pool_free(&p);
    }
    {   /* signed bidirecional */
        S2RPool s; s2r_pool_init(&s,S2R_I8,8);
        s2r_push_signed_adaptive(&s,-50); s2r_push_signed_adaptive(&s,40);
        s2r_push_signed_adaptive(&s,-2000000000); /* -> I32 */
        CHECK(s.size==S2R_I32, "signed: I8->I32 with outlier");
        s2r_remove_swap(&s, s.count-1);
        s2r_fit_class(&s);
        CHECK(s.size==S2R_I8, "signed: fit_class I32->I8");
        CHECK(s2r_get_signed(&s,0)==-50 && s2r_get_signed(&s,1)==40, "signed values intact");
        s2r_pool_free(&s);
    }
    {   /* tracked: range O(1) */
        S2RTracked t; s2r_tracked_init(&t,S2R_8,8);
        for(int i=0;i<100;i++) s2r_tracked_push(&t,(uint64_t)((i*37)%200));
        uint64_t mn,mx; s2r_tracked_range(&t,&mn,&mx);
        CHECK(mn==s2r_min(&t.p)&&mx==s2r_max(&t.p), "tracked range O(1) matches rescan");
        s2r_remove_swap(&t.p,0); t.dirty=1;
        s2r_tracked_range(&t,&mn,&mx);
        CHECK(mn==s2r_min(&t.p)&&mx==s2r_max(&t.p), "tracked range rescaneia quando sujo");
        s2r_tracked_free(&t);
    }

    printf("\n=== #3 GROUP-BY directly on the compact format ===\n");
    {
        size_t N=10000000; /* 10M chaves u8 */
        S2RPool keys; s2r_pool_init(&keys,S2R_8,N);
        uint8_t*k=(uint8_t*)keys.data; for(size_t i=0;i<N;i++) k[i]=(uint8_t)((i*2654435761u>>24)%200); keys.count=N;

        /* COUNT(*) : histograma; valida contra referencia ingenua e mede */
        uint64_t h[256], ref[256]; memset(ref,0,sizeof ref);
        for(size_t i=0;i<N;i++) ref[k[i]]++;
        s2r_histogram_u8(&keys,h);
        CHECK(memcmp(h,ref,sizeof h)==0, "histogram_u8 == referencia");
        uint64_t tot=0; for(int i=0;i<256;i++) tot+=h[i];
        CHECK(tot==N, "sum of bins == N");

        double bs=1e18,bf=1e18;
        for(int r=0;r<20;r++){ double t=ms(); memset(ref,0,sizeof ref); for(size_t i=0;i<N;i++) ref[k[i]]++; double d=ms()-t; if(d<bs)bs=d; }
        for(int r=0;r<20;r++){ double t=ms(); s2r_histogram_u8(&keys,h); double d=ms()-t; if(d<bf)bf=d; }
        printf("  histogram 10M u8: ingenuo=%.2f ms (%.0f M/s)  4-way=%.2f ms (%.0f M/s)  %.1fx\n",
               bs,N/1000.0/bs, bf,N/1000.0/bf, bs/bf);

        /* GROUP BY key SUM(val) */
        S2RPool vals; s2r_pool_init(&vals,S2R_32,N);
        uint32_t*v=(uint32_t*)vals.data; for(size_t i=0;i<N;i++) v[i]=(uint32_t)(i&0xFFFF); vals.count=N;
        uint64_t g[256], gref[256]; memset(gref,0,sizeof gref);
        for(size_t i=0;i<N;i++) gref[k[i]]+=v[i];
        s2r_group_sum_u8u32(&keys,&vals,g);
        CHECK(memcmp(g,gref,sizeof g)==0, "group_sum == referencia");
        double bg=1e18; for(int r=0;r<20;r++){ double t=ms(); s2r_group_sum_u8u32(&keys,&vals,g); double d=ms()-t; if(d<bg)bg=d; }
        printf("  group_sum 10M (u8 key, u32 val): %.2f ms (%.0f M linhas/s)\n", bg, N/1000.0/bg);

        s2r_pool_free(&keys); s2r_pool_free(&vals);
    }

    printf("\n=== %d OK, %d FAIL ===\n", pass, fail);
    return fail?1:0;
}
