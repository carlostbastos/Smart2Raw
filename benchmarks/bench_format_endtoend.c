/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Benchmark 2 of 3 - END-TO-END, conventional baseline vs Smart2Raw.
 * Same logical values stored two ways: int64 (conventional) and u8 (compact).
 * Shows the format-only effect (u8 vs int64, scalar) AND the compound effect
 * (compact format + SIMD). Throughput in Mvalues/s so the formats are comparable.
 * Build: gcc -O3 -march=native -I include -o bench_e benchmarks/bench_format_endtoend.c
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "smart2raw.h"
static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static uint64_t scal_u8 (const uint8_t *a,size_t n){ uint64_t s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
static uint64_t scal_i64(const int64_t *a,size_t n){ uint64_t s=0; for(size_t i=0;i<n;i++) s+=(uint64_t)a[i]; return s; }
static volatile uint64_t SINK;
static void run(const char* lbl,size_t N,long it){
    uint8_t* u8=malloc(N); int64_t* i64=malloc(N*8);
    for(size_t i=0;i<N;i++){ u8[i]=(uint8_t)(i*131+7); i64[i]=u8[i]; }
    double t,base;
    printf("\n[%s]  N=%zu valores  (int64=%zu MiB, u8=%zu MiB)\n",lbl,N,(N*8)>>20,N>>20);
    t=now(); for(long k=0;k<it;k++) SINK=scal_i64(i64,N);          t=now()-t; base=(double)N*it/t;
    printf("  CONVENCIONAL  int64 + escalar : %9.2f Mval/s   (1.00x)\n", base/1e6);
    t=now(); for(long k=0;k<it;k++) SINK=scal_u8(u8,N);           t=now()-t; double a=(double)N*it/t;
    printf("  Smart2Raw     u8    + escalar : %9.2f Mval/s   (%.1fx)  <- so o formato\n", a/1e6, a/base);
    t=now(); for(long k=0;k<it;k++) SINK=s2r__sum_u8_avx2(u8,N);   t=now()-t; double b=(double)N*it/t;
    printf("  Smart2Raw     u8    + AVX2    : %9.2f Mval/s   (%.1fx)\n", b/1e6, b/base);
    t=now(); for(long k=0;k<it;k++) SINK=s2r__sum_u8_avx512(u8,N); t=now()-t; double c=(double)N*it/t;
    printf("  Smart2Raw     u8    + AVX-512 : %9.2f Mval/s   (%.1fx)  <- formato x SIMD\n", c/1e6, c/base);
    free(u8); free(i64);
}
int main(void){
    printf("avx2=%d avx512bw=%d\n", s2r_has_avx2()!=0, s2r_has_avx512bw()!=0);
    run("cache: u8 cabe em cache, int64 transborda", 256*1024, 50000);
    run("memoria: ambos vindos da RAM",              64ull*1024*1024, 40);
    return 0;
}
