/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Indices/IDs: 2x when the vocabulary fits in 16 bits; honestly ~1x above. */
#include <stdio.h>
#include "smart2raw.h"
int main(void){
    enum { N = 1000000 };
    S2RPool small, big;
    s2r_pool_init(&small, S2R_8, N); s2r_pool_init(&big, S2R_8, N);
    for (int i = 0; i < N; i++) {
        s2r_push_adaptive(&small, (uint64_t)((unsigned)i % 50000u));   /* vocab <=16b -> u16 */
        s2r_push_adaptive(&big,   (uint64_t)((unsigned)i % 200000u));  /* vocab  >16b -> u32 */
    }
    printf("indices: %d IDs\n", N);
    printf("  vocab <=16b: class=%d bits -> %zu KB (int32 = %zu KB)  => 2x\n",
           (int)small.size, s2r_used_bytes(&small)/1024, (size_t)N*4/1024);
    printf("  vocab  >16b: class=%d bits -> %zu KB  (honest: ~1x vs int32)\n",
           (int)big.size, s2r_used_bytes(&big)/1024);
    s2r_pool_free(&small); s2r_pool_free(&big);
    return 0;
}
