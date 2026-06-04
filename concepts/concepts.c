/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/*
 * concepts.c - The core idea of Smart2Raw in one file.
 * Compile:  gcc -O2 -I ../include concepts.c -o concepts && ./concepts
 *
 * Shows, in four steps, what the library does:
 *   (1) classify the smallest type that fits;
 *   (2) grow the class automatically on insert (no truncation);
 *   (3) operate DIRECTLY on the compact form (no decompression);
 *   (4) block-wise width (PFOR): an outlier inflates only its own block.
 */
#include <stdio.h>
#include "smart2raw.h"

int main(void) {
    printf("Smart2Raw v%s - concepts\n\n", S2R_VERSION_STRING);

    /* (1) Classify: the smallest class (in bits) that holds the value. */
    printf("(1) classify: 200 -> %d bits | 70000 -> %d bits | 5e9 -> %d bits\n",
           (int)s2r_classify(200), (int)s2r_classify(70000),
           (int)s2r_classify(5000000000ULL));

    /* (2) Store in the smallest type and GROW automatically when needed (no truncation) */
    S2RPool p;
    s2r_pool_init(&p, S2R_8, 4);
    const uint64_t vals[] = { 25, 30, 40, 1000, 70000, 5000000000ULL };
    for (unsigned i = 0; i < 6; i++) s2r_push_adaptive(&p, vals[i]);
    printf("(2) after 6 pushes: class = %d bits, count = %zu  (u8 grew up to u64)\n",
           (int)p.size, (size_t)p.count);

    /* (3) Operate DIRECTLY on the compact form, no decompression */
    printf("(3) operate on the compact form: sum=%llu  min=%llu  max=%llu\n",
           (unsigned long long)s2r_sum(&p),
           (unsigned long long)s2r_min(&p),
           (unsigned long long)s2r_max(&p));
    s2r_pool_free(&p);

    /* (4) Block-wise width (PFOR): a single outlier inflates only its own block */
    enum { N = 4096 };
    static uint64_t a[N];
    uint64_t total = 0;
    for (int i = 0; i < N; i++) { a[i] = (uint64_t)(i % 200); total += a[i]; }  /* base u8 */
    a[1234] = 5000000; total += 5000000 - (1234 % 200);                        /* 1 outlier -> u32 */
    S2RBlocked b;
    s2r_blocked_build(&b, a, N, 256);
    const size_t single_w = 4u * N;   /* a single width would be u32 because of the outlier */
    printf("(4) PFOR: single width = %zu B | per block = %zu B  (%.1fx smaller) | sum %s\n",
           single_w, s2r_blocked_bytes(&b),
           (double)single_w / (double)s2r_blocked_bytes(&b),
           (s2r_blocked_sum(&b) == total) ? "exact" : "WRONG");
    s2r_blocked_free(&b);

    printf("\nSummary: classify once, always operate on the most compact form.\n");
    return 0;
}
