/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Feature store: each column at its own width (age u8, flag u8, price u16, id u32). */
#include <stdio.h>
#include "smart2raw.h"
int main(void){
    enum { N = 100000 };
    S2RPool age, flag, price, id;
    s2r_pool_init(&age, S2R_8, N);  s2r_pool_init(&flag, S2R_8, N);
    s2r_pool_init(&price, S2R_8, N); s2r_pool_init(&id, S2R_8, N);
    for (int i = 0; i < N; i++) {
        s2r_push_adaptive(&age,   (uint64_t)(i % 120));
        s2r_push_adaptive(&flag,  (uint64_t)(i & 1));
        s2r_push_adaptive(&price, (uint64_t)(i % 50000));
        s2r_push_adaptive(&id,    (uint64_t)((unsigned)i * 37u % 4000000u));
    }
    size_t total = s2r_used_bytes(&age)+s2r_used_bytes(&flag)+s2r_used_bytes(&price)+s2r_used_bytes(&id);
    size_t uniform = (size_t)N*8*4;
    printf("feature_store: 4 columns x %d rows\n", N);
    printf("  widths: age=%d flag=%d price=%d id=%d bits\n",
           (int)age.size, (int)flag.size, (int)price.size, (int)id.size);
    printf("  total = %zu KB vs int64 uniform = %zu KB -> %.0f%% menos\n",
           total/1024, uniform/1024, 100.0*(1.0 - (double)total/(double)uniform));
    s2r_pool_free(&age); s2r_pool_free(&flag); s2r_pool_free(&price); s2r_pool_free(&id);
    return 0;
}
