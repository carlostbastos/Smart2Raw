/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Telemetry: latencies in ms (0..1999). Memory vs int64 + filters. */
#include <stdio.h>
#include "smart2raw.h"
int main(void){
    enum { N = 1000000 };
    S2RPool lat; s2r_pool_init(&lat, S2R_8, N);
    for (int i = 0; i < N; i++) s2r_push_adaptive(&lat, (uint64_t)(i % 2000));
    printf("telemetry: %d latencies\n", N);
    printf("  class = %d bits | memory = %zu KB (int64 = %zu KB) -> %.0f%% less\n",
           (int)lat.size, s2r_used_bytes(&lat)/1024, (size_t)N*8/1024,
           100.0*(1.0 - (double)s2r_used_bytes(&lat)/((double)N*8)));
    printf("  max = %llu ms | latencies > 1500ms = %zu\n",
           (unsigned long long)s2r_max(&lat), s2r_count_gt(&lat, 1500));
    s2r_pool_free(&lat);
    return 0;
}
