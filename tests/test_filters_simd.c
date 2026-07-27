/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* test_filters_simd.c - the v3.4.0 predicate dispatch must be INDISTINGUISHABLE
 * from the scalar filters it accelerates.
 *
 * The whole family reduces to one range kernel:
 *      count_gt(t) = count_range(t+1, MAX)   count_lt(t) = count_range(MIN, t-1)
 *      count_eq(v) = count_range(v, v)
 * and the range test is a wrapping subtract plus one unsigned compare:
 *      v in [lo,hi]  <=>  (v - lo) mod 2^w <= hi - lo
 * The same kernel serves signed pools because two's-complement subtraction is
 * bit-identical to unsigned subtraction.
 *
 * That is a lot of reasoning resting on modular arithmetic, so this suite does
 * not sample: for the 8-bit classes it sweeps EVERY threshold and EVERY range
 * endpoint (all 256 values, all 65536 ordered pairs) against the scalar
 * reference. For 16/32/64 it sweeps boundaries plus pseudo-random probes.
 * Out-of-class thresholds are included on purpose - clamping is where a range
 * kernel usually breaks.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smart2raw.h"

static long pass = 0, fail = 0;
#define CHECK(c, ...) do{ if(c){pass++;} else { if(fail<20){printf("  [FAIL] "); printf(__VA_ARGS__); printf("\n");} fail++; } }while(0)

static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return rs; }

/* ---------------- unsigned ---------------- */

static void sweep_u8(void)
{
    enum { N = 5000 };
    S2RPool p;
    s2r_pool_init(&p, S2R_8, N);
    for (size_t i = 0; i < N; i++) s2r_set(&p, i, (uint64_t)(rnd() & 0xFF));
    p.count = N;

    /* every threshold, including the two just outside the class */
    for (int t = -1; t <= 256; t++){
        uint64_t th = (uint64_t)(t < 0 ? (uint64_t)-1 : (uint64_t)t);   /* -1 -> huge */
        CHECK(s2r_count_gt_fast(&p, th) == s2r_count_gt(&p, th), "u8 count_gt thr=%d", t);
        CHECK(s2r_count_lt_fast(&p, th) == s2r_count_lt(&p, th), "u8 count_lt thr=%d", t);
        CHECK(s2r_count_eq_fast(&p, th) == s2r_count_eq(&p, th), "u8 count_eq v=%d", t);
    }
    /* every ordered pair of endpoints */
    for (unsigned lo = 0; lo <= 255; lo++)
        for (unsigned hi = lo; hi <= 255; hi++){
            CHECK(s2r_count_range_fast(&p, lo, hi) == s2r_count_range(&p, lo, hi),
                  "u8 count_range [%u,%u]", lo, hi);
            CHECK(s2r_sum_if_fast(&p, lo, hi) == s2r_sum_if(&p, lo, hi),
                  "u8 sum_if [%u,%u]", lo, hi);
        }
    /* ranges that reach outside the class must clamp, not wrap */
    CHECK(s2r_count_range_fast(&p, 0, 1000) == s2r_count_range(&p, 0, 1000), "u8 range clamp hi");
    CHECK(s2r_count_range_fast(&p, 300, 1000) == s2r_count_range(&p, 300, 1000), "u8 range above class");
    CHECK(s2r_count_range_fast(&p, 200, 100) == 0, "u8 inverted range");
    CHECK(s2r_sum_if_fast(&p, 0, 1000) == s2r_sum_if(&p, 0, 1000), "u8 sum_if clamp");
    s2r_pool_free(&p);
}

static void sweep_wide_unsigned(int8_t cls, uint64_t mask, const char *name)
{
    enum { N = 4000 };
    S2RPool p;
    s2r_pool_init(&p, cls, N);
    for (size_t i = 0; i < N; i++) s2r_set(&p, i, rnd() & mask);
    p.count = N;

    uint64_t probes[64]; int np = 0;
    probes[np++] = 0; probes[np++] = 1; probes[np++] = mask; probes[np++] = mask - 1;
    probes[np++] = mask >> 1; probes[np++] = (mask >> 1) + 1;
    probes[np++] = ~0ull; probes[np++] = mask + 1;          /* outside the class */
    for (int i = 0; i < 24; i++) probes[np++] = rnd() & mask;
    /* a few values actually present, so eq is not always zero */
    for (int i = 0; i < 8; i++) probes[np++] = s2r_get(&p, rnd() % N);

    for (int i = 0; i < np; i++){
        uint64_t t = probes[i];
        CHECK(s2r_count_gt_fast(&p, t) == s2r_count_gt(&p, t), "%s count_gt %llu", name, (unsigned long long)t);
        CHECK(s2r_count_lt_fast(&p, t) == s2r_count_lt(&p, t), "%s count_lt %llu", name, (unsigned long long)t);
        CHECK(s2r_count_eq_fast(&p, t) == s2r_count_eq(&p, t), "%s count_eq %llu", name, (unsigned long long)t);
        for (int j = 0; j < np; j++){
            uint64_t a = t, b = probes[j];
            if (a > b) continue;
            CHECK(s2r_count_range_fast(&p, a, b) == s2r_count_range(&p, a, b),
                  "%s count_range [%llu,%llu]", name, (unsigned long long)a, (unsigned long long)b);
            CHECK(s2r_sum_if_fast(&p, a, b) == s2r_sum_if(&p, a, b),
                  "%s sum_if [%llu,%llu]", name, (unsigned long long)a, (unsigned long long)b);
        }
    }
    s2r_pool_free(&p);
}

/* ---------------- signed ---------------- */

static void sweep_i8(void)
{
    enum { N = 5000 };
    S2RPool p;
    s2r_pool_init(&p, S2R_I8, N);
    for (size_t i = 0; i < N; i++) s2r_set_signed(&p, i, (int64_t)(int8_t)(rnd() & 0xFF));
    p.count = N;

    for (int t = -200; t <= 200; t++){
        CHECK(s2r_count_gt_signed_fast(&p, t) == s2r_count_gt_signed(&p, t), "i8 count_gt %d", t);
        CHECK(s2r_count_lt_signed_fast(&p, t) == s2r_count_lt_signed(&p, t), "i8 count_lt %d", t);
        CHECK(s2r_count_eq_signed_fast(&p, t) == s2r_count_eq_signed(&p, t), "i8 count_eq %d", t);
    }
    for (int lo = -128; lo <= 127; lo++)
        for (int hi = lo; hi <= 127; hi++){
            CHECK(s2r_count_range_signed_fast(&p, lo, hi) == s2r_count_range_signed(&p, lo, hi),
                  "i8 count_range [%d,%d]", lo, hi);
            CHECK(s2r_sum_if_signed_fast(&p, lo, hi) == s2r_sum_if_signed(&p, lo, hi),
                  "i8 sum_if [%d,%d]", lo, hi);
        }
    /* ranges straddling or exceeding the class */
    CHECK(s2r_count_range_signed_fast(&p, -1000, 1000) == s2r_count_range_signed(&p, -1000, 1000), "i8 range wider than class");
    CHECK(s2r_count_range_signed_fast(&p, -1000, -500) == 0, "i8 range fully below class");
    CHECK(s2r_count_range_signed_fast(&p, 500, 1000) == 0, "i8 range fully above class");
    CHECK(s2r_sum_if_signed_fast(&p, -1000, 1000) == s2r_sum_if_signed(&p, -1000, 1000), "i8 sum_if wider than class");
    /* the extreme endpoints, where a naive span computation overflows */
    CHECK(s2r_count_range_signed_fast(&p, INT64_MIN, INT64_MAX) == s2r_count_range_signed(&p, INT64_MIN, INT64_MAX), "i8 full int64 range");
    CHECK(s2r_count_gt_signed_fast(&p, INT64_MIN) == s2r_count_gt_signed(&p, INT64_MIN), "i8 count_gt INT64_MIN");
    CHECK(s2r_count_lt_signed_fast(&p, INT64_MAX) == s2r_count_lt_signed(&p, INT64_MAX), "i8 count_lt INT64_MAX");
    s2r_pool_free(&p);
}

static void sweep_wide_signed(int8_t cls, const char *name)
{
    enum { N = 4000 };
    S2RPool p;
    s2r_pool_init(&p, cls, N);
    int64_t cmin = s2r_min_signed(cls), cmax = s2r_max_signed(cls);
    /* span in UNSIGNED arithmetic: for i64, cmax - cmin overflows int64 */
    const uint64_t span = (uint64_t)cmax - (uint64_t)cmin;
    for (size_t i = 0; i < N; i++){
        uint64_t r = rnd();
        int64_t v = (int64_t)((uint64_t)cmin + (span == ~0ull ? r : r % (span + 1)));
        s2r_set_signed(&p, i, v);
    }
    p.count = N;

    int64_t probes[48]; int np = 0;
    probes[np++] = cmin; probes[np++] = cmin + 1; probes[np++] = -1; probes[np++] = 0;
    probes[np++] = 1; probes[np++] = cmax - 1; probes[np++] = cmax;
    probes[np++] = INT64_MIN; probes[np++] = INT64_MAX;
    for (int i = 0; i < 16; i++) probes[np++] = s2r_get_signed(&p, rnd() % N);
    for (int i = 0; i < 8; i++) probes[np++] = (int64_t)rnd();

    for (int i = 0; i < np; i++){
        int64_t t = probes[i];
        CHECK(s2r_count_gt_signed_fast(&p, t) == s2r_count_gt_signed(&p, t), "%s count_gt %lld", name, (long long)t);
        CHECK(s2r_count_lt_signed_fast(&p, t) == s2r_count_lt_signed(&p, t), "%s count_lt %lld", name, (long long)t);
        CHECK(s2r_count_eq_signed_fast(&p, t) == s2r_count_eq_signed(&p, t), "%s count_eq %lld", name, (long long)t);
        for (int j = 0; j < np; j++){
            int64_t a = t, b = probes[j];
            if (a > b) continue;
            CHECK(s2r_count_range_signed_fast(&p, a, b) == s2r_count_range_signed(&p, a, b),
                  "%s count_range [%lld,%lld]", name, (long long)a, (long long)b);
            CHECK(s2r_sum_if_signed_fast(&p, a, b) == s2r_sum_if_signed(&p, a, b),
                  "%s sum_if [%lld,%lld]", name, (long long)a, (long long)b);
        }
    }
    s2r_pool_free(&p);
}

/* ---------------- edges ---------------- */

static void edges(void)
{
    S2RPool p;
    /* empty pool */
    s2r_pool_init(&p, S2R_8, 16);
    CHECK(s2r_count_gt_fast(&p, 0) == 0, "empty count_gt");
    CHECK(s2r_count_range_fast(&p, 0, 255) == 0, "empty count_range");
    CHECK(s2r_sum_if_fast(&p, 0, 255) == 0, "empty sum_if");
    s2r_pool_free(&p);

    /* NULL pool must not crash */
    CHECK(s2r_count_gt_fast(NULL, 0) == 0, "null count_gt");
    CHECK(s2r_count_range_fast(NULL, 0, 1) == 0, "null count_range");
    CHECK(s2r_sum_if_fast(NULL, 0, 1) == 0, "null sum_if");

    /* the signed entry points must refuse an unsigned pool, like the rest of the
     * signed API does - reading a u8 payload as i8 silently gives wrong answers */
    s2r_pool_init(&p, S2R_8, 4);
    for (int i = 0; i < 4; i++) s2r_push_adaptive(&p, 200);
    CHECK(s2r_count_gt_signed_fast(&p, 0) == 0, "signed fast on unsigned pool must refuse");
    s2r_pool_free(&p);

    /* every length around the 32-byte vector boundary, so the tail is exercised */
    for (size_t n = 1; n <= 100; n++){
        s2r_pool_init(&p, S2R_8, n);
        for (size_t i = 0; i < n; i++) s2r_set(&p, i, (uint64_t)((i * 37 + 11) & 0xFF));
        p.count = n;
        CHECK(s2r_count_gt_fast(&p, 100) == s2r_count_gt(&p, 100), "tail n=%zu count_gt", n);
        CHECK(s2r_count_range_fast(&p, 40, 200) == s2r_count_range(&p, 40, 200), "tail n=%zu range", n);
        CHECK(s2r_sum_if_fast(&p, 40, 200) == s2r_sum_if(&p, 40, 200), "tail n=%zu sum_if", n);
        s2r_pool_free(&p);
    }
    for (size_t n = 1; n <= 60; n++){
        s2r_pool_init(&p, S2R_16, n);
        for (size_t i = 0; i < n; i++) s2r_set(&p, i, (uint64_t)((i * 7919) & 0xFFFF));
        p.count = n;
        CHECK(s2r_count_gt_fast(&p, 30000) == s2r_count_gt(&p, 30000), "u16 tail n=%zu", n);
        CHECK(s2r_count_range_fast(&p, 100, 40000) == s2r_count_range(&p, 100, 40000), "u16 tail range n=%zu", n);
        s2r_pool_free(&p);
    }

    /* saturated u8 payload: every value 255, sum_if must not overflow its lane */
    {
        size_t n = 300000;
        s2r_pool_init(&p, S2R_8, n);
        memset(p.data, 0xFF, n);
        p.count = n;
        CHECK(s2r_sum_if_fast(&p, 0, 255) == s2r_sum_if(&p, 0, 255), "all-255 sum_if");
        CHECK(s2r_sum_if_fast(&p, 0, 255) == (uint64_t)n * 255, "all-255 sum_if value");
        s2r_pool_free(&p);
    }
}

int main(void)
{
    printf("SIMD: AVX2=%d\n", s2r_has_avx2());
    sweep_u8();
    sweep_wide_unsigned(S2R_16, 0xFFFFull,     "u16");
    sweep_wide_unsigned(S2R_32, 0xFFFFFFFFull, "u32");
    sweep_wide_unsigned(S2R_64, ~0ull,         "u64");
    sweep_i8();
    sweep_wide_signed(S2R_I16, "i16");
    sweep_wide_signed(S2R_I32, "i32");
    sweep_wide_signed(S2R_I64, "i64");
    edges();
    printf("=== %ld OK, %ld FAIL ===\n", pass, fail);
    return fail ? 1 : 0;
}
