/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* AI - zero-point correction (row-sum) for int8 GEMM, summed block-wise (SIMD).
 * Here we validate CORRECTNESS; the speedup (~6.7x) is in benchmarks/. */
#include <stdio.h>
#include <stdlib.h>
#include "smart2raw.h"
int main(void){
    srand(3);
    const size_t M = 2048, K = 2048, N = M*K;
    uint64_t *w = (uint64_t*)malloc(N*sizeof(uint64_t));
    for (size_t i = 0; i < N; i++) w[i] = (uint64_t)(rand() % 256);  /* u8 weights */
    S2RBlocked b; s2r_blocked_build(&b, w, N, K);                    /* 1 block per row */
    uint64_t fast = s2r_blocked_sum_fast(&b), slow = s2r_blocked_sum(&b);
    printf("ai_zeropoint: int8 weights %zux%zu, naive sum per block (vpsadbw per block)\n", M, K);
    printf("  sum_fast = %llu | scalar = %llu -> %s (speedup measured in benchmarks/)\n",
           (unsigned long long)fast, (unsigned long long)slow, fast == slow ? "exact" : "ERROR");
    s2r_blocked_free(&b); free(w);
    return 0;
}
