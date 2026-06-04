/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/*
 * test_completo.c - Single, complete test of Smart2Raw v3.3.5
 * ===========================================================
 *
 * Exercises ALL the library modules in a single file, with one assertion
 * per behavior. The sections follow the header's own order.
 *
 * Build (server):    gcc -O3 -march=native -o tc test_completo.c && ./tc
 * Adaptable:          -O2 | -O2 -DS2R_NO_SIMD | -std=c11 -pedantic
 * File/mmap sections only compile when S2R_HAS_STDIO / S2R_HAS_MMAP.
 */
#include <stdio.h>
#include "smart2raw.h"

static int g_ok = 0, g_fail = 0;
#define CHECK(cond, msg) do {                                   \
    if (cond) { g_ok++; }                                       \
    else { g_fail++; printf("  FAIL: %s\n", (msg)); }           \
} while (0)

int main(void) {
    printf("=== Smart2Raw v%s - full test ===\n", S2R_VERSION_STRING);

    /* ----------------------------------------------------------------
     * 1) NUCLEO + PUSH AUTO-ADAPTATIVO (a classe cresce, nunca trunca)
     * ---------------------------------------------------------------- */
    {
        S2RPool p; CHECK(s2r_pool_init(&p, S2R_8, 4), "pool_init");
        uint64_t vals[] = { 25, 30, 40, 1000, 70000, 5000000000ULL };
        for (unsigned i = 0; i < 6; i++) s2r_push_adaptive(&p, vals[i]);
        CHECK(p.count == 6, "count apos 6 pushes");
        CHECK(p.size == 64, "class grew u8 -> u64 (5e9 doesn't fit in 32 bits)");
        CHECK(s2r_sum(&p) == 5000071095ULL, "exact sum after growing the class");
        s2r_pool_free(&p);
    }

    /* ----------------------------------------------------------------
     * 2) ACESSO TIPADO DIRETO (set/get round-trip em varias classes)
     * ---------------------------------------------------------------- */
    {
        S2RPool p; s2r_pool_init(&p, S2R_16, 3);
        s2r_push(&p, 0); s2r_push(&p, 12345); s2r_push(&p, 65535);
        s2r_set(&p, 1, 777);
        CHECK(s2r_get(&p, 0) == 0 && s2r_get(&p, 1) == 777 && s2r_get(&p, 2) == 65535,
              "set/get tipado em u16");
        s2r_pool_free(&p);
    }

    /* ----------------------------------------------------------------
     * 3) AGREGACOES (unsigned) + SIMD: sum_fast deve bater com sum
     * ---------------------------------------------------------------- */
    {
        S2RPool p; s2r_pool_init(&p, S2R_8, 1000);
        uint64_t expected = 0;
        for (int i = 0; i < 1000; i++) { uint64_t v = (uint64_t)((i * 37) % 200); s2r_push(&p, v); expected += v; }
        CHECK(s2r_sum(&p) == expected, "scalar sum");
        CHECK(s2r_sum_fast(&p) == expected, "sum_fast (SIMD/dispatch) == sum");
        CHECK(s2r_max(&p) <= 199 && s2r_min(&p) <= s2r_max(&p), "min/max coerentes");
        s2r_pool_free(&p);
    }

    /* ----------------------------------------------------------------
     * 4) SIGNED-AWARE: pool com sinal e negativos -> stats corretas
     *    (the non-_signed path would read the bytes as unsigned: wrong)
     * ---------------------------------------------------------------- */
    {
        S2RPool p; s2r_pool_init(&p, S2R_I8, 5);
        int vals[] = { -100, -50, 0, 50, 100 };
        for (int i = 0; i < 5; i++) s2r_push_signed_adaptive(&p, vals[i]);
        CHECK(s2r_min_signed_val(&p) == -100, "signed min = -100");
        CHECK(s2r_max_signed_val(&p) ==  100, "signed max = +100");
        /* media 0; variancia AMOSTRAL (n-1) = 25000/4 = 6250; desvio = sqrt(6250) */
        CHECK((s2r_variance_signed(&p) > 6249.999 && s2r_variance_signed(&p) < 6250.001), "signed variance (sample) = 6250");
        CHECK((s2r_stddev_signed(&p) > 79.055 && s2r_stddev_signed(&p) < 79.058), "signed stddev ~ 79.06");
        s2r_pool_free(&p);
    }

    /* ----------------------------------------------------------------
     * 5) FILTROS / CONTAGENS por faixa
     * ---------------------------------------------------------------- */
    {
        S2RPool p; s2r_pool_init(&p, S2R_8, 10);
        uint64_t v[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
        for (int i = 0; i < 10; i++) s2r_push(&p, v[i]);
        CHECK(s2r_count_gt(&p, 50) == 5, "count_gt(50) = 5");
        CHECK(s2r_count_range(&p, 30, 60) == 4, "count_range[30,60] = 4");
        CHECK(s2r_sum_if(&p, 30, 60) == 30 + 40 + 50 + 60, "sum_if[30,60]");
        s2r_pool_free(&p);
    }

    /* ----------------------------------------------------------------
     * 6) ARITMETICA + LAZY-CARRY (sem e COM sinal)
     * ---------------------------------------------------------------- */
    {
        /* (a) unsigned lazy-carry: adding overflows u8 -> promotes by itself */
        S2RPool p; s2r_pool_init(&p, S2R_8, 2);
        s2r_push(&p, 250); s2r_push(&p, 200);
        s2r_add_scalar_safe(&p, 60);          /* 250+60=310 doesn't fit in u8 */
        CHECK(p.size == 16, "lazy-carry: u8 -> u16 ao inves de wrap");
        CHECK(s2r_get(&p, 0) == 310 && s2r_get(&p, 1) == 260, "correct values post-promotion");
        s2r_pool_free(&p);

        /* (b) SIGNED lazy-carry, deferred session: 1 promotion for the chain */
        S2RPool q; s2r_pool_init(&q, S2R_I8, 2);
        s2r_push(&q, (uint64_t)(int64_t)-50);
        s2r_push(&q, (uint64_t)(int64_t)-100);
        S2RDeferredSigned d; s2r_defer_signed_begin(&d, &q);
        s2r_defer_signed_add(&d, -200);       /* -250, -300  -> I16 */
        s2r_defer_signed_mul(&d, -500);        /* 125000, 150000 -> I32 */
        int ok = s2r_defer_signed_commit(&d);
        CHECK(ok && q.size == S2R_I32, "deferred: I8 -> I32 in a single promotion");
        CHECK(s2r_get_signed(&q, 0) == 125000 && s2r_get_signed(&q, 1) == 150000,
              "deferida aplicou as operacoes (valores corretos)");
        s2r_pool_free(&q);
    }

    /* ----------------------------------------------------------------
     * 7) SERIALIZACAO PORTAVEL (LE canonico + CRC32) - round-trip
     * ---------------------------------------------------------------- */
#if S2R_HAS_STDIO
    {
        S2RPool a; s2r_pool_init(&a, S2R_16, 256);
        for (int i = 0; i < 256; i++) s2r_push(&a, (uint64_t)(i * 211));
        CHECK(s2r_save_portable(&a, "tc.s2r") == S2R_OK, "save_portable");
        S2RPool b;
        CHECK(s2r_load_portable(&b, "tc.s2r") == S2R_OK, "load_portable (CRC valida)");
        int eq = (b.count == a.count && b.size == a.size);
        for (size_t i = 0; eq && i < a.count; i++) eq = (s2r_get(&b, i) == s2r_get(&a, i));
        CHECK(eq, "identical round-trip (disk -> memory)");
        s2r_pool_free(&b);

    /* ----------------------------------------------------------------
     * 8) mmap ZERO-COPY: opera sobre o arquivo sem copiar p/ a RAM
     * ---------------------------------------------------------------- */
#if S2R_HAS_MMAP
        {
            S2RMap m;
            CHECK(s2r_map_open(&m, "tc.s2r", 1) == S2R_OK, "map_open (verifica CRC)");
            int meq = (m.pool.count == a.count);
            for (size_t i = 0; meq && i < a.count; i++) meq = (s2r_get(&m.pool, i) == s2r_get(&a, i));
            CHECK(meq, "zero-copy read matches the original");
            CHECK(s2r_sum_fast(&m.pool) == s2r_sum(&a), "aggregation runs directly on the mapping");
            s2r_map_close(&m);
        }
#endif
        s2r_pool_free(&a);
        remove("tc.s2r");
    }
#endif

    /* ----------------------------------------------------------------
     * 9) ANALYTICS: largura bidirecional (auto-cura), tracked, group-by
     * ---------------------------------------------------------------- */
    {
        /* (a) self-healing: an outlier promotes to u32; once removed, fit_class lowers it */
        S2RPool p; s2r_pool_init(&p, S2R_8, 8);
        for (int i = 0; i < 7; i++) s2r_push_adaptive(&p, (uint64_t)(i + 1));   /* 1..7, u8 */
        s2r_push_adaptive(&p, 5000000ULL);                                       /* outlier -> u32 */
        CHECK(p.size == 32, "outlier promoted u8 -> u32");
        s2r_remove_swap(&p, p.count - 1);                                        /* remove the outlier */
        s2r_fit_class(&p);                                                       /* auto-cura */
        CHECK(p.size == 8, "fit_class lowered u32 -> u8 after removing the outlier");
        CHECK(s2r_sum(&p) == 1+2+3+4+5+6+7, "dados intactos apos a cura");
        s2r_pool_free(&p);

        /* (b) S2RTracked: min/max em O(1) batem com o rescan */
        S2RTracked t; s2r_tracked_init(&t, S2R_8, 8);
        for (int i = 0; i < 100; i++) s2r_tracked_push(&t, (uint64_t)((i * 37) % 200));
        uint64_t mn, mx; s2r_tracked_range(&t, &mn, &mx);
        CHECK(mn == s2r_min(&t.p) && mx == s2r_max(&t.p), "tracked range O(1) == rescan");
        s2r_tracked_free(&t);

        /* (c) group-by on the compact form: histogram and sum per group */
        S2RPool keys; s2r_pool_init(&keys, S2R_8, 8);
        S2RPool vals; s2r_pool_init(&vals, S2R_32, 8);
        uint8_t  k[] = { 1, 2, 1, 3, 2, 1 };
        uint32_t v2[] = { 10, 20, 30, 40, 50, 60 };
        for (int i = 0; i < 6; i++) { s2r_push(&keys, k[i]); s2r_push(&vals, v2[i]); }
        uint64_t hist[256]; s2r_histogram_u8(&keys, hist);
        CHECK(hist[1] == 3 && hist[2] == 2 && hist[3] == 1, "histogram_u8 (GROUP BY COUNT)");
        uint64_t gsum[256]; s2r_group_sum_u8u32(&keys, &vals, gsum);
        CHECK(gsum[1] == 10 + 30 + 60 && gsum[2] == 20 + 50 && gsum[3] == 40,
              "group_sum_u8u32 (GROUP BY SUM)");
        s2r_pool_free(&keys); s2r_pool_free(&vals);
    }

    /* ----------------------------------------------------------------
     * 10) BLOCK-WISE WIDTH (PFOR): an outlier inflates only its own block
     * ---------------------------------------------------------------- */
    {
        enum { N = 4096 };
        static uint64_t a[N];
        uint64_t s = 0;
        for (int i = 0; i < N; i++) a[i] = (uint64_t)(i % 200);   /* base u8 */
        a[1234] = 5000000;                                        /* 1 outlier -> u32 */
        for (int i = 0; i < N; i++) s += a[i];
        S2RBlocked b;
        CHECK(s2r_blocked_build(&b, a, N, 256), "blocked_build");
        int rt = 1; for (int i = 0; i < N; i++) if (s2r_blocked_get(&b, i) != a[i]) { rt = 0; break; }
        CHECK(rt, "blocked: get(i) == src[i]");
        CHECK(s2r_blocked_sum(&b) == s, "blocked: exact sum with outlier");
        /* single width would be u32 (4 bytes); block-wise uses much less */
        CHECK(s2r_blocked_bytes(&b) < (size_t)4 * N / 2, "blocked: <50% of the single width");
        s2r_blocked_free(&b);
    }

    printf("=== %d OK, %d FAIL ===\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
