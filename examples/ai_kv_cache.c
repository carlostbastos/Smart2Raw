/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* AI - KV-cache with outlier tokens, per-token blocks. */
#include <stdio.h>
#include <stdlib.h>
#include "smart2raw.h"
int main(void){
    srand(2);
    const size_t TOK = 100000, D = 128, N = TOK*D;
    uint64_t *kv = (uint64_t*)malloc(N*sizeof(uint64_t));
    size_t kout = 0;
    for (size_t i = 0; i < TOK; i++) {
        int o = (rand() % 100) == 0; if (o) kout++;
        for (size_t d = 0; d < D; d++)
            kv[i*D + d] = o ? (uint64_t)(2000 + rand()%25000) : (uint64_t)(rand()%201);
    }
    S2RBlocked b; s2r_blocked_build(&b, kv, N, D);  /* 1 block per token */
    size_t unif = 2*N;
    printf("ai_kv_cache: %.1f%% outlier tokens, per-token blocks\n", 100.0*kout/TOK);
    printf("  uniform-u16 = %.1f MB | per block = %.1f MB -> %.2fx smaller\n",
           unif/1e6, s2r_blocked_bytes(&b)/1e6, (double)unif/(double)s2r_blocked_bytes(&b));
    s2r_blocked_free(&b); free(kv);
    return 0;
}
