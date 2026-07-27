/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* AI - quantized activations with outlier channels, per-channel layout (PFOR).
 * Honest: byte-granular doesn't beat flat int8; the gain is vs the uniform width
 * (u16, forced by the outliers) WHEN the outlier is localized. */
#include <stdio.h>
#include <stdlib.h>
#include "smart2raw.h"
int main(void){
    srand(1);
    const size_t T = 2048, C = 512, N = T*C;
    uint64_t *a = (uint64_t*)malloc(N*sizeof(uint64_t));
    size_t nout = 0;
    for (size_t c = 0; c < C; c++) {
        int o = (rand() % 100) == 0; if (o) nout++;
        for (size_t t = 0; t < T; t++)
            a[c*T + t] = o ? (uint64_t)(2000 + rand()%25000) : (uint64_t)(rand()%201);
    }
    S2RBlocked b; s2r_blocked_build(&b, a, N, T);  /* 1 block per channel */
    size_t unif = 2*N;                              /* safe u16 */
    printf("ai_activations: %.1f%% outlier channels, per-channel layout\n", 100.0*nout/C);
    printf("  uniform u16 = %.1f MB | per block = %.1f MB -> %.2fx smaller (exact naive sum)\n",
           unif/1e6, s2r_blocked_bytes(&b)/1e6, (double)unif/(double)s2r_blocked_bytes(&b));
    s2r_blocked_free(&b); free(a);
    return 0;
}
