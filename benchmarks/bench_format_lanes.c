/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Benchmark 3 of 3 - FORMAT UNLOCKS LANES, SIMD held constant (AVX-512 both).
 * Same logical values: int64 summed with AVX-512 (8 values/instr) vs u8 summed
 * with AVX-512 (64 values/instr). Isolates the "compact format gives more lanes
 * per instruction" effect at equal SIMD sophistication.
 * Build: gcc -O3 -march=native -I include -o bench_l benchmarks/bench_format_lanes.c
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <immintrin.h>
#include "smart2raw.h"
static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
__attribute__((target("avx512f")))
static uint64_t sum_i64_av512(const int64_t* a,size_t n){
  __m512i acc=_mm512_setzero_si512(); size_t i=0;
  for(;i+8<=n;i+=8) acc=_mm512_add_epi64(acc,_mm512_loadu_si512((const void*)(a+i)));
  uint64_t s=(uint64_t)_mm512_reduce_add_epi64(acc); for(;i<n;i++) s+=(uint64_t)a[i]; return s;
}
static volatile uint64_t SINK;
static void run(const char* lbl,size_t N,long it){
  uint8_t* u8=malloc(N); int64_t* i64=malloc(N*8);
  for(size_t i=0;i<N;i++){ u8[i]=(uint8_t)(i*131+7); i64[i]=u8[i]; }
  double t;
  printf("\n[%s] N=%zu  (mesmos valores; int64=%zuMiB, u8=%zuMiB)\n",lbl,N,(N*8)>>20,N>>20);
  t=now(); for(long k=0;k<it;k++) SINK=sum_i64_av512(i64,N);     t=now()-t; double i=(double)N*it/t;
  printf("  int64 + AVX-512 ( 8 val/instr): %9.2f Mval/s   (1.00x)\n", i/1e6);
  t=now(); for(long k=0;k<it;k++) SINK=s2r__sum_u8_avx512(u8,N); t=now()-t; double u=(double)N*it/t;
  printf("  u8    + AVX-512 (64 val/instr): %9.2f Mval/s   (%.1fx)  <- so o formato, mesmo SIMD\n", u/1e6, u/i);
  free(u8); free(i64);
}
int main(void){
  printf("avx512bw=%d\n", s2r_has_avx512bw()!=0);
  run("cache",256*1024,50000);
  run("memoria",64ull*1024*1024,40);
  return 0;
}
