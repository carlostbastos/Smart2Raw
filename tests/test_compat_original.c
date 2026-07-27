/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/*
 * Smart2Raw v3.2.0 - Test Suite
 * =============================
 * 
 * Tests all new and fixed features:
 * - s2r_push_checked / s2r_push_saturate
 * - Signed integers (S2R_I8, I16, I32, I64)
 * - s2r_promote / s2r_demote
 * - Statistics (mean, variance, stddev)
 * - Range queries
 * - Iterators
 * - Performance comparisons
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define S2R_DEBUG 1
#include "smart2raw.h"

/* ============================================================================
 * TEST FRAMEWORK
 * ============================================================================ */

#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_RESET   "\x1b[0m"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf(ANSI_RED "    ✗ FAIL: %s" ANSI_RESET "\n", msg); \
        tests_failed++; \
        return 0; \
    } \
} while(0)

#define TEST_PASS(name) do { \
    printf(ANSI_GREEN "    ✓ %s" ANSI_RESET "\n", name); \
    tests_passed++; \
} while(0)

#define TEST_SECTION(name) printf("\n" ANSI_CYAN "═══ %s ═══" ANSI_RESET "\n", name)

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* ============================================================================
 * TEST 1: PUSH VARIANTS
 * ============================================================================ */

static int test_push_variants(void) {
    TEST_SECTION("TEST 1: Push Variants");
    
    /* Test s2r_push_checked */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_8, 10);
        
        /* Valid push */
        S2RError err = s2r_push_checked(&p, 100);
        TEST_ASSERT(err == S2R_OK, "push_checked should accept 100 for S2R_8");
        TEST_ASSERT(s2r_get(&p, 0) == 100, "value should be 100");
        
        /* Push at boundary */
        err = s2r_push_checked(&p, 255);
        TEST_ASSERT(err == S2R_OK, "push_checked should accept 255 for S2R_8");
        
        /* Push too large */
        err = s2r_push_checked(&p, 256);
        TEST_ASSERT(err == S2R_ERR_VALUE_TOO_LARGE, "push_checked should reject 256 for S2R_8");
        TEST_ASSERT(p.count == 2, "count should still be 2");
        
        err = s2r_push_checked(&p, 1000);
        TEST_ASSERT(err == S2R_ERR_VALUE_TOO_LARGE, "push_checked should reject 1000 for S2R_8");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_push_checked");
    }
    
    /* Test s2r_push_saturate */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_8, 10);
        
        s2r_push_saturate(&p, 100);
        TEST_ASSERT(s2r_get(&p, 0) == 100, "saturate should keep 100");
        
        s2r_push_saturate(&p, 300);
        TEST_ASSERT(s2r_get(&p, 1) == 255, "saturate should clamp 300 to 255");
        
        s2r_push_saturate(&p, 1000000);
        TEST_ASSERT(s2r_get(&p, 2) == 255, "saturate should clamp large value to 255");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_push_saturate");
    }
    
    /* Test s2r_push_many */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_16, 0);  /* Start empty, test auto-grow */
        
        uint64_t values[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
        size_t pushed = s2r_push_many(&p, values, 10);
        
        TEST_ASSERT(pushed == 10, "should push all 10 values");
        TEST_ASSERT(p.count == 10, "count should be 10");
        TEST_ASSERT(s2r_get(&p, 4) == 50, "value at index 4 should be 50");
        TEST_ASSERT(s2r_sum(&p) == 550, "sum should be 550");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_push_many");
    }
    
    return 1;
}

/* ============================================================================
 * TEST 2: SIGNED INTEGERS
 * ============================================================================ */

static int test_signed_integers(void) {
    TEST_SECTION("TEST 2: Signed Integers");
    
    /* Test S2R_I8 */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_I8, 10);
        
        TEST_ASSERT(s2r_is_signed(&p), "S2R_I8 should be signed");
        
        /* Push positive */
        s2r_push(&p, (uint64_t)(int64_t)50);
        TEST_ASSERT(s2r_get_signed(&p, 0) == 50, "should store 50");
        
        /* Push negative (cast through uint64_t) */
        s2r_push(&p, (uint64_t)(int64_t)-30);
        TEST_ASSERT(s2r_get_signed(&p, 1) == -30, "should store -30");
        
        /* Push boundary */
        s2r_push(&p, (uint64_t)(int64_t)127);
        s2r_push(&p, (uint64_t)(int64_t)-128);
        TEST_ASSERT(s2r_get_signed(&p, 2) == 127, "should store 127");
        TEST_ASSERT(s2r_get_signed(&p, 3) == -128, "should store -128");
        
        s2r_pool_free(&p);
        TEST_PASS("S2R_I8 basic operations");
    }
    
    /* Test signed sum */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_I16, 10);
        
        s2r_push(&p, (uint64_t)(int64_t)100);
        s2r_push(&p, (uint64_t)(int64_t)-50);
        s2r_push(&p, (uint64_t)(int64_t)25);
        s2r_push(&p, (uint64_t)(int64_t)-75);
        
        int64_t sum = s2r_sum_signed(&p);
        TEST_ASSERT(sum == 0, "sum of 100, -50, 25, -75 should be 0");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_sum_signed");
    }
    
    /* Test s2r_push_signed_checked */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_I8, 10);
        
        S2RError err = s2r_push_signed_checked(&p, 50);
        TEST_ASSERT(err == S2R_OK, "should accept 50");
        
        err = s2r_push_signed_checked(&p, -50);
        TEST_ASSERT(err == S2R_OK, "should accept -50");
        
        err = s2r_push_signed_checked(&p, 128);
        TEST_ASSERT(err == S2R_ERR_VALUE_TOO_LARGE, "should reject 128");
        
        err = s2r_push_signed_checked(&p, -129);
        TEST_ASSERT(err == S2R_ERR_VALUE_TOO_SMALL, "should reject -129");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_push_signed_checked");
    }
    
    /* Test from_array_signed_auto */
    {
        int64_t data[] = {-100, 50, -25, 75, 0, -128, 127};
        S2RPool p;
        
        s2r_from_array_signed_auto(&p, data, 7);
        
        TEST_ASSERT(p.size == S2R_I8, "should auto-classify as S2R_I8");
        TEST_ASSERT(s2r_get_signed(&p, 0) == -100, "value 0 should be -100");
        TEST_ASSERT(s2r_get_signed(&p, 5) == -128, "value 5 should be -128");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_from_array_signed_auto");
    }
    
    /* Test larger signed range */
    {
        int64_t data[] = {-1000, 500, -30000, 30000};
        S2RPool p;
        
        s2r_from_array_signed_auto(&p, data, 4);
        
        TEST_ASSERT(p.size == S2R_I16, "should auto-classify as S2R_I16");
        TEST_ASSERT(s2r_get_signed(&p, 2) == -30000, "value 2 should be -30000");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_classify_signed_array with I16 range");
    }
    
    return 1;
}

/* ============================================================================
 * TEST 3: PROMOTE / DEMOTE
 * ============================================================================ */

static int test_promote_demote(void) {
    TEST_SECTION("TEST 3: Promote / Demote");
    
    /* Test promote */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_8, 10);
        
        for (int i = 0; i < 5; i++) s2r_push(&p, i * 50);  /* 0, 50, 100, 150, 200 */
        
        TEST_ASSERT(s2r_pool_bytes(&p) == 5, "should use 5 bytes");
        
        int result = s2r_promote(&p, S2R_16);
        TEST_ASSERT(result == 1, "promote should succeed");
        TEST_ASSERT(p.size == S2R_16, "should now be S2R_16");
        TEST_ASSERT(s2r_pool_bytes(&p) == 10, "should use 10 bytes");
        
        /* Verify values preserved */
        TEST_ASSERT(s2r_get(&p, 0) == 0, "value 0 should be preserved");
        TEST_ASSERT(s2r_get(&p, 2) == 100, "value 2 should be preserved");
        TEST_ASSERT(s2r_get(&p, 4) == 200, "value 4 should be preserved");
        
        /* Can now store larger values */
        s2r_push(&p, 5000);
        TEST_ASSERT(s2r_get(&p, 5) == 5000, "should store 5000");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_promote");
    }
    
    /* Test demote (success) */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_32, 10);
        
        for (int i = 0; i < 5; i++) s2r_push(&p, i * 10);  /* 0, 10, 20, 30, 40 */
        
        TEST_ASSERT(s2r_pool_bytes(&p) == 20, "should use 20 bytes");
        
        int result = s2r_demote(&p, S2R_8);
        TEST_ASSERT(result == 1, "demote should succeed");
        TEST_ASSERT(p.size == S2R_8, "should now be S2R_8");
        TEST_ASSERT(s2r_pool_bytes(&p) == 5, "should use 5 bytes");
        
        /* Verify values preserved */
        TEST_ASSERT(s2r_get(&p, 3) == 30, "value 3 should be preserved");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_demote (success)");
    }
    
    /* Test demote (fail - values don't fit) */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_16, 10);
        
        s2r_push(&p, 100);
        s2r_push(&p, 5000);  /* This won't fit in S2R_8 */
        s2r_push(&p, 200);
        
        int result = s2r_demote(&p, S2R_8);
        TEST_ASSERT(result == 0, "demote should fail");
        TEST_ASSERT(p.size == S2R_16, "should remain S2R_16");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_demote (fail - values too large)");
    }
    
    return 1;
}

/* ============================================================================
 * TEST 4: STATISTICS
 * ============================================================================ */

static int test_statistics(void) {
    TEST_SECTION("TEST 4: Statistics");
    
    /* Test mean */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_16, 10);
        
        s2r_push(&p, 10);
        s2r_push(&p, 20);
        s2r_push(&p, 30);
        s2r_push(&p, 40);
        
        double mean = s2r_mean(&p);
        TEST_ASSERT(mean >= 24.9 && mean <= 25.1, "mean of 10,20,30,40 should be 25");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_mean");
    }
    
    /* Test variance and stddev */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_16, 10);
        
        /* Dataset: 2, 4, 4, 4, 5, 5, 7, 9 */
        uint64_t data[] = {2, 4, 4, 4, 5, 5, 7, 9};
        s2r_push_many(&p, data, 8);
        
        double mean = s2r_mean(&p);
        /* Mean = 40/8 = 5 */
        TEST_ASSERT(mean >= 4.9 && mean <= 5.1, "mean should be 5");
        
        double var = s2r_variance(&p);
        /* Sample variance = Σ(x-μ)² / (n-1) = 32/7 ≈ 4.57 */
        TEST_ASSERT(var >= 4.0 && var <= 5.0, "variance should be around 4.57");
        
        double std = s2r_stddev(&p);
        /* Stddev ≈ sqrt(4.57) ≈ 2.14 */
        TEST_ASSERT(std >= 2.0 && std <= 2.5, "stddev should be around 2.14");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_variance and s2r_stddev");
    }
    
    /* Test mean_signed */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_I16, 10);
        
        s2r_push(&p, (uint64_t)(int64_t)100);
        s2r_push(&p, (uint64_t)(int64_t)-100);
        s2r_push(&p, (uint64_t)(int64_t)50);
        s2r_push(&p, (uint64_t)(int64_t)-50);
        
        double mean = s2r_mean_signed(&p);
        TEST_ASSERT(mean >= -0.1 && mean <= 0.1, "mean of 100,-100,50,-50 should be 0");
        
        s2r_pool_free(&p);
        TEST_PASS("s2r_mean_signed");
    }
    
    return 1;
}

/* ============================================================================
 * TEST 5: RANGE QUERIES
 * ============================================================================ */

static int test_range_queries(void) {
    TEST_SECTION("TEST 5: Range Queries");
    
    S2RPool p;
    s2r_pool_init(&p, S2R_16, 100);
    
    /* Insert 0, 10, 20, ..., 90 */
    for (int i = 0; i < 10; i++) s2r_push(&p, i * 10);
    
    /* Test count_range */
    {
        size_t cnt = s2r_count_range(&p, 20, 60);
        TEST_ASSERT(cnt == 5, "count_range(20,60) should be 5 (20,30,40,50,60)");
        
        cnt = s2r_count_range(&p, 0, 0);
        TEST_ASSERT(cnt == 1, "count_range(0,0) should be 1");
        
        cnt = s2r_count_range(&p, 100, 200);
        TEST_ASSERT(cnt == 0, "count_range(100,200) should be 0");
        
        TEST_PASS("s2r_count_range");
    }
    
    /* Test sum_if */
    {
        uint64_t sum = s2r_sum_if(&p, 30, 70);
        /* 30 + 40 + 50 + 60 + 70 = 250 */
        TEST_ASSERT(sum == 250, "sum_if(30,70) should be 250");
        
        sum = s2r_sum_if(&p, 0, 1000);
        TEST_ASSERT(sum == 450, "sum_if(0,1000) should be 450 (total sum)");
        
        TEST_PASS("s2r_sum_if");
    }
    
    s2r_pool_free(&p);
    return 1;
}

/* ============================================================================
 * TEST 6: ITERATOR MACRO
 * ============================================================================ */

static int test_iterator(void) {
    TEST_SECTION("TEST 6: Iterator Macro");
    
    S2RPool p;
    s2r_pool_init(&p, S2R_16, 10);
    
    s2r_push(&p, 100);
    s2r_push(&p, 200);
    s2r_push(&p, 300);
    
    uint64_t sum = 0;
    size_t count = 0;
    uint64_t val;
    
    S2R_FOREACH(&p, i, val) {
        sum += val;
        count++;
    }
    
    TEST_ASSERT(sum == 600, "S2R_FOREACH sum should be 600");
    TEST_ASSERT(count == 3, "S2R_FOREACH should iterate 3 times");
    
    s2r_pool_free(&p);
    TEST_PASS("S2R_FOREACH macro");
    
    /* Test signed iterator */
    {
        S2RPool p2;
        s2r_pool_init(&p2, S2R_I16, 10);
        
        s2r_push(&p2, (uint64_t)(int64_t)100);
        s2r_push(&p2, (uint64_t)(int64_t)-50);
        s2r_push(&p2, (uint64_t)(int64_t)25);
        
        int64_t sum_s = 0;
        int64_t sval;
        
        S2R_FOREACH_SIGNED(&p2, j, sval) {
            sum_s += sval;
        }
        
        TEST_ASSERT(sum_s == 75, "S2R_FOREACH_SIGNED sum should be 75");
        
        s2r_pool_free(&p2);
        TEST_PASS("S2R_FOREACH_SIGNED macro");
    }
    
    return 1;
}

/* ============================================================================
 * TEST 7: DIAGNOSTICS
 * ============================================================================ */

static int test_diagnostics(void) {
    TEST_SECTION("TEST 7: Diagnostics");
    
    S2RPool p;
    s2r_pool_init(&p, S2R_16, 1000);
    
    for (int i = 0; i < 500; i++) s2r_push(&p, i);
    
    S2RInfo info = s2r_info(&p);
    
    TEST_ASSERT(info.count == 500, "info.count should be 500");
    TEST_ASSERT(info.capacity == 1000, "info.capacity should be 1000");
    TEST_ASSERT(info.bits_per_element == 16, "info.bits_per_element should be 16");
    TEST_ASSERT(info.is_signed == 0, "info.is_signed should be 0");
    TEST_ASSERT(info.fill_ratio >= 0.49 && info.fill_ratio <= 0.51, "fill_ratio should be ~0.5");
    
    /* Memory efficiency: 500 * 2 bytes vs 500 * 8 bytes = 75% saving */
    TEST_ASSERT(info.memory_efficiency >= 0.74 && info.memory_efficiency <= 0.76, 
                "memory_efficiency should be ~0.75");
    
    s2r_pool_free(&p);
    TEST_PASS("s2r_info");
    
    return 1;
}

/* ============================================================================
 * TEST 8: SHRINK TO FIT
 * ============================================================================ */

static int test_shrink_to_fit(void) {
    TEST_SECTION("TEST 8: Shrink to Fit");
    
    S2RPool p;
    s2r_pool_init(&p, S2R_16, 10000);  /* Large initial capacity */
    
    for (int i = 0; i < 100; i++) s2r_push(&p, i);
    
    size_t before_cap = p.capacity;
    size_t before_bytes = p.byte_cap;
    
    TEST_ASSERT(before_cap == 10000, "initial capacity should be 10000");
    
    int result = s2r_shrink_to_fit(&p);
    TEST_ASSERT(result == 1, "shrink_to_fit should succeed");
    TEST_ASSERT(p.capacity == 100, "capacity should now be 100");
    TEST_ASSERT(p.byte_cap < before_bytes, "byte_cap should be smaller");
    
    /* Verify data intact */
    TEST_ASSERT(s2r_get(&p, 50) == 50, "data should be preserved");
    TEST_ASSERT(s2r_sum(&p) == 4950, "sum should be preserved");
    
    s2r_pool_free(&p);
    TEST_PASS("s2r_shrink_to_fit");
    
    return 1;
}

/* ============================================================================
 * TEST 9: SERIALIZATION
 * ============================================================================ */

static int test_serialization(void) {
    TEST_SECTION("TEST 9: Serialization (v3.2 format)");
    
    const char *filename = "/tmp/test_s2r_v32.bin";
    
    /* Test unsigned */
    {
        S2RPool original, loaded;
        s2r_pool_init(&original, S2R_16, 100);
        
        for (int i = 0; i < 50; i++) s2r_push(&original, i * 100);
        
        int result = s2r_save(&original, filename);
        TEST_ASSERT(result == 1, "save should succeed");
        
        result = s2r_load(&loaded, filename);
        TEST_ASSERT(result == 1, "load should succeed");
        TEST_ASSERT(loaded.count == original.count, "count should match");
        TEST_ASSERT(loaded.size == original.size, "size should match");
        
        for (size_t i = 0; i < original.count; i++) {
            TEST_ASSERT(s2r_get(&loaded, i) == s2r_get(&original, i), "data should match");
        }
        
        s2r_pool_free(&original);
        s2r_pool_free(&loaded);
        remove(filename);
        TEST_PASS("save/load unsigned");
    }
    
    /* Test signed */
    {
        S2RPool original, loaded;
        s2r_pool_init(&original, S2R_I16, 100);
        
        for (int i = 0; i < 50; i++) {
            s2r_push(&original, (uint64_t)(int64_t)(i * 100 - 2500));
        }
        
        int result = s2r_save(&original, filename);
        TEST_ASSERT(result == 1, "save signed should succeed");
        
        result = s2r_load(&loaded, filename);
        TEST_ASSERT(result == 1, "load signed should succeed");
        TEST_ASSERT(s2r_is_signed(&loaded), "loaded should be signed");
        
        for (size_t i = 0; i < original.count; i++) {
            TEST_ASSERT(s2r_get_signed(&loaded, i) == s2r_get_signed(&original, i), 
                       "signed data should match");
        }
        
        s2r_pool_free(&original);
        s2r_pool_free(&loaded);
        remove(filename);
        TEST_PASS("save/load signed");
    }
    
    return 1;
}

/* ============================================================================
 * TEST 10: TRANSFORM
 * ============================================================================ */

static uint64_t double_value(uint64_t v, void *ctx) {
    (void)ctx;
    return v * 2;
}

static uint64_t add_offset(uint64_t v, void *ctx) {
    uint64_t offset = *(uint64_t*)ctx;
    return v + offset;
}

static int test_transform(void) {
    TEST_SECTION("TEST 10: Transform");
    
    S2RPool p;
    s2r_pool_init(&p, S2R_16, 10);
    
    s2r_push(&p, 10);
    s2r_push(&p, 20);
    s2r_push(&p, 30);
    
    /* Transform: double all values */
    s2r_transform(&p, double_value, NULL);
    
    TEST_ASSERT(s2r_get(&p, 0) == 20, "10 should become 20");
    TEST_ASSERT(s2r_get(&p, 1) == 40, "20 should become 40");
    TEST_ASSERT(s2r_get(&p, 2) == 60, "30 should become 60");
    TEST_PASS("s2r_transform (double)");
    
    /* Transform with context */
    uint64_t offset = 100;
    s2r_transform(&p, add_offset, &offset);
    
    TEST_ASSERT(s2r_get(&p, 0) == 120, "20+100 should be 120");
    TEST_ASSERT(s2r_get(&p, 1) == 140, "40+100 should be 140");
    TEST_PASS("s2r_transform (with context)");
    
    s2r_pool_free(&p);
    return 1;
}

/* ============================================================================
 * TEST 11: BACKWARD COMPATIBILITY
 * ============================================================================ */

static int test_backward_compat(void) {
    TEST_SECTION("TEST 11: Backward Compatibility");
    
    /* All v3.1.1 operations should still work */
    S2RPool p;
    
    TEST_ASSERT(s2r_pool_init(&p, S2R_16, 100), "pool_init should work");
    
    for (int i = 0; i < 50; i++) {
        TEST_ASSERT(s2r_push(&p, i * 100), "push should work");
    }
    
    TEST_ASSERT(s2r_sum(&p) == 122500, "sum should work");
    TEST_ASSERT(s2r_min(&p) == 0, "min should work");
    TEST_ASSERT(s2r_max(&p) == 4900, "max should work");
    TEST_ASSERT(s2r_count_gt(&p, 2000) == 29, "count_gt should work");
    TEST_ASSERT(s2r_count_lt(&p, 1000) == 10, "count_lt should work");
    TEST_ASSERT(s2r_count_eq(&p, 2500) == 1, "count_eq should work");
    TEST_ASSERT(s2r_find(&p, 2500) == 25, "find should work");
    
    s2r_add_scalar(&p, 10);
    TEST_ASSERT(s2r_get(&p, 0) == 10, "add_scalar should work");
    
    s2r_xor_scalar(&p, 0xFF);
    s2r_xor_scalar(&p, 0xFF);  /* Restore */
    TEST_ASSERT(s2r_get(&p, 0) == 10, "xor_scalar should work");
    
    s2r_pool_free(&p);
    TEST_PASS("All v3.1.1 operations");
    
    return 1;
}

/* ============================================================================
 * TEST 12: PERFORMANCE COMPARISON
 * ============================================================================ */

static int test_performance(void) {
    TEST_SECTION("TEST 12: Performance Comparison (v3.1 vs v3.2)");
    
    const size_t N = 5000000;
    
    /* Test auto-grow performance (improved in v3.2) */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_16, 0);
        
        double t0 = get_time_ms();
        for (size_t i = 0; i < N; i++) {
            s2r_push(&p, i % 10000);
        }
        double t = get_time_ms() - t0;
        
        printf("    Auto-grow %zuM elements: %.1f ms (%.1f M/s)\n", 
               N/1000000, t, N/1000/t);
        printf("    Final capacity: %zu (growth factor 1.5x)\n", p.capacity);
        
        s2r_pool_free(&p);
    }
    
    /* Test push_many (bulk) */
    {
        uint64_t *data = (uint64_t*)malloc(N * sizeof(uint64_t));
        for (size_t i = 0; i < N; i++) data[i] = i % 10000;
        
        S2RPool p;
        s2r_pool_init(&p, S2R_16, 0);
        
        double t0 = get_time_ms();
        s2r_push_many(&p, data, N);
        double t = get_time_ms() - t0;
        
        printf("    push_many %zuM elements: %.1f ms (%.1f M/s)\n", 
               N/1000000, t, N/1000/t);
        
        free(data);
        s2r_pool_free(&p);
    }
    
    /* Test statistics */
    {
        S2RPool p;
        uint64_t *data = (uint64_t*)malloc(N * sizeof(uint64_t));
        for (size_t i = 0; i < N; i++) data[i] = i % 10000;
        
        s2r_from_array_auto(&p, data, N);
        
        double t0 = get_time_ms();
        volatile double mean = s2r_mean(&p);
        double t_mean = get_time_ms() - t0;
        (void)mean;
        
        t0 = get_time_ms();
        volatile double var = s2r_variance(&p);
        double t_var = get_time_ms() - t0;
        (void)var;
        
        printf("    mean() %zuM: %.2f ms\n", N/1000000, t_mean);
        printf("    variance() %zuM: %.2f ms\n", N/1000000, t_var);
        
        free(data);
        s2r_pool_free(&p);
    }
    
    TEST_PASS("Performance benchmarks completed");
    return 1;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf(ANSI_BLUE "╔══════════════════════════════════════════════════════════════╗\n");
    printf("║          SMART2RAW v3.2.0 - TEST SUITE                       ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n" ANSI_RESET);
    printf("  Version: %s\n", S2R_VERSION_STRING);
    
    test_push_variants();
    test_signed_integers();
    test_promote_demote();
    test_statistics();
    test_range_queries();
    test_iterator();
    test_diagnostics();
    test_shrink_to_fit();
    test_serialization();
    test_transform();
    test_backward_compat();
    test_performance();
    
    printf("\n");
    printf(ANSI_BLUE "╔══════════════════════════════════════════════════════════════╗\n" ANSI_RESET);
    printf(ANSI_BLUE "║                      FINAL RESULT                         ║\n" ANSI_RESET);
    printf(ANSI_BLUE "╠══════════════════════════════════════════════════════════════╣\n" ANSI_RESET);
    printf("║  Tests passed:    " ANSI_GREEN "%3d" ANSI_RESET "                                        ║\n", tests_passed);
    printf("║  Tests failed:    " ANSI_RED "%3d" ANSI_RESET "                                        ║\n", tests_failed);
    printf(ANSI_BLUE "╠══════════════════════════════════════════════════════════════╣\n" ANSI_RESET);
    
    if (tests_failed == 0) {
        printf("║  " ANSI_GREEN "STATUS: PASSED - Smart2Raw v3.2.0 working!" ANSI_RESET "                ║\n");
    } else {
        printf("║  " ANSI_RED "STATUS: FAILED - there are failures to fix" ANSI_RESET "               ║\n");
    }
    
    printf(ANSI_BLUE "╚══════════════════════════════════════════════════════════════╝\n" ANSI_RESET);
    printf("\n");
    
    return tests_failed > 0 ? 1 : 0;
}
