/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* IoT/edge: sensor readings in centi-degrees (-2000..8499), signed -> i16. */
#include <stdio.h>
#include "smart2raw.h"
int main(void){
    enum { N = 4096 };
    S2RPool t; s2r_pool_init(&t, S2R_I8, N);
    for (int i = 0; i < N; i++) s2r_push_signed_adaptive(&t, (int64_t)(-2000 + (i % 10500)));
    printf("iot_edge: %d readings (centi-degrees)\n", N);
    printf("  class = %d bits (signed) | memory = %zu B vs int64 = %zu B\n",
           (int)t.size, s2r_used_bytes(&t), (size_t)N*8);
    printf("  min = %lld | max = %lld\n",
           (long long)s2r_min_signed_val(&t), (long long)s2r_max_signed_val(&t));
    s2r_pool_free(&t);
    return 0;
}
