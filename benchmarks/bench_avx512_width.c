/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Benchmark 1 of 3 - INSTRUCTION WIDTH, format held constant (all u8).
 * Isolates the AVX-512-vs-AVX2 gain (more lanes per instruction). Same u8 array
 * is summed by scalar, AVX2 and AVX-512 kernels. Throughput in GB/s of u8 read.
 * Build: gcc -O3 -march=native -I include -o bench_w benchmarks/bench_avx512_width.c
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "smart2raw.h"
static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static uint64_t scal_u8(const uint8_t*a,size_t n){ uint64_t s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
static volatile uint64_t SINK;
int main(void){
    printf("avx2=%d avx512bw=%d\n", s2r_has_avx2()!=0, s2r_has_avx512bw()!=0);
    size_t sizes[]={ 32*1024, 256ull*1024*1024 };
    const char* lbl[]={"cache-resident 32 KiB","memory-bound 256 MiB"};
    long it[]={200000,30};
    for(int s=0;s<2;s++){
        size_t n=sizes[s];
        uint8_t* a=malloc(n); for(size_t i=0;i<n;i++) a[i]=(uint8_t)(i*131+7);
        uint64_t r1=scal_u8(a,n), r2=s2r__sum_u8_avx2(a,n), r3=s2r__sum_u8_avx512(a,n);
        printf("\n[%s]  correto? scal=avx2:%d scal=avx512:%d\n", lbl[s], r1==r2, r1==r3);
        double bytes=(double)n*it[s], t;
        t=now(); for(long k=0;k<it[s];k++) SINK=scal_u8(a,n);            t=now()-t; printf("  escalar : %.2f GB/s\n", bytes/t/1e9);
        t=now(); for(long k=0;k<it[s];k++) SINK=s2r__sum_u8_avx2(a,n);   t=now()-t; double av2=bytes/t/1e9; printf("  AVX2    : %.2f GB/s\n", av2);
        t=now(); for(long k=0;k<it[s];k++) SINK=s2r__sum_u8_avx512(a,n); t=now()-t; double av5=bytes/t/1e9; printf("  AVX-512 : %.2f GB/s   (AVX-512 vs AVX2: %.2fx)\n", av5, av5/av2);
        free(a);
    }
    return 0;
}
