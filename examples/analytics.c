/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Columnar analytics: the class (1 byte) is a free zone-map; sum_fast + range. */
#include <stdio.h>
#include "smart2raw.h"
int main(void){
    enum { N = 2000000 };
    S2RPool c; s2r_pool_init(&c, S2R_8, N);
    for (int i = 0; i < N; i++) s2r_push_adaptive(&c, (uint64_t)(i % 256));
    printf("analytics: column of %d values\n", N);
    printf("  free zone-map: the class (1 byte) already bounds the range (< 2^%d)\n", (int)c.size);
    printf("  sum_fast = %llu | values in [100,150] = %zu\n",
           (unsigned long long)s2r_sum_fast(&c), s2r_count_range(&c, 100, 150));
    s2r_pool_free(&c);
    return 0;
}
