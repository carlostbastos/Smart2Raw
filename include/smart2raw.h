/*
 * Smart2Raw v3.5.1 - Adaptive numeric storage (header-only)
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * =======================================================================
 *
 * A SINGLE-FILE C library with zero external dependencies. It stores
 * integer arrays in the SMALLEST native class that fits the real range
 * (+-8/16/32/64 bits, signed or unsigned) and operates directly on the compact format.
 *
 * Principle: classify once, always operate in the most compact format.
 *
 * --- CONTENTS (sections of this file, in order) ---
 *   VERSION  ·  CONFIGURATION  ·  ENDIANNESS  ·  PLATFORM/FEATURE DETECTION
 *   ERROR CODES  ·  TYPES  ·  ALIGNED ALLOC  ·  HELPERS  ·  VALUE LIMITS
 *   CLASSIFICATION  ·  POOL MANAGEMENT  ·  TYPED ACCESS  ·  PUSH VARIANTS
 *   CLASS PROMOTION/DEMOTION  ·  AGGREGATIONS  ·  STATISTICS
 *   SIGNED-AWARE AGGREGATIONS/FILTERS/STATS  ·  FILTERS AND COUNTS
 *   ARITHMETIC (includes lazy-carry, unsigned/SIGNED)  ·  BITWISE
 *   STREAMING/CONVERSION  ·  UTILITY  ·  DIAGNOSTICS  ·  ITERATOR MACRO
 *   SERIALIZATION (LE canonico + CRC32)  ·  mmap zero-copy (COW big-endian)
 *   SIMD (dispatch em runtime: AVX2 vpsadbw / NEON)  ·  ERROR HANDLING
 *   ANALYTICS MODULE: bidirectional width (self-healing), S2RTracked,
 *   group-by, sort, unique, nunique and value_counts
 *   BLOCK-WISE WIDTH MODULE (PFOR): S2RBlocked - per-block class
 *
 * --- ADAPTABILIDADE (gates de compilacao) ---
 *   -DS2R_NO_STDIO   removes file serialization
 *   -DS2R_NO_MMAP    removes memory mapping
 *   -DS2R_NO_SIMD    removes SIMD dispatch (uses the scalar path)
 *   MCU footprint (all gates on): ~3.4 KB of code.
 *
 * --- CHANGELOG (most recent first) ---
 * v3.4.0: correctness, contract, and the two layers that were leaving work on
 *         the table. Five parts:
 *
 *         (1) .s2r CONTRACT. The reader adopted ownership flags from disk, so a
 *         crafted EXTERNAL bit leaked every loaded pool and READONLY froze it;
 *         flags are now masked on both read and write. fmt, the reserved byte,
 *         class/flag agreement and exact file length are enforced, per
 *         SPEC_s2r_format.md - all of them were accepted by C and rejected by at
 *         least one port, so .s2r was not the portable contract it claimed.
 *
 *         (2) UNDEFINED BEHAVIOUR. s2r_mul_scalar at u16 promoted both operands
 *         to int, overflowing INT_MAX on the very path whose correctness proof
 *         requires DEFINED wraparound. s2r_sum_signed, s2r_sum_if_signed and
 *         s2r_blocked_sum_signed accumulated in int64_t. Both classes fixed.
 *
 *         (3) PREDICATE DISPATCH. s2r_sum_fast was the only operation with a
 *         runtime SIMD dispatch; every filter ran scalar, the signed ones through
 *         the accessor so the width switch sat inside the loop. The whole family
 *         reduces to one range kernel - count_gt/lt/eq ARE ranges - and a range
 *         test is a wrapping subtract plus one unsigned compare, which also makes
 *         the SIGNED case fall out of the same kernel by two's complement.
 *         Measured 4.5-13.4x. count_gt keeps a dedicated kernel, by measurement.
 *
 *         (4) PFOR FRAME OF REFERENCE. A block's class came from its MAXIMUM
 *         alone, so {9000000000, 9000000001, ...} was stored as u64 despite
 *         spanning 1. Blocks now store values relative to their own minimum:
 *         3.9x on unix timestamps, 7.7x on sequential IDs, 170x on a constant
 *         column (delta width 0 stores no payload), parity where the baseline is
 *         already zero - the old behaviour is the special case base = 0. Plus
 *         zone statistics (SUM/MAX/MIN in O(nblocks), never touching the payload;
 *         SUM measured 114x faster), sorted blocks answering count_gt by binary
 *         search above a MEASURED size gate (below it the search LOSES 0.67x to a
 *         vectorised scan), and s2r_blocked_save/load - fmt=2, canonical LE,
 *         CRC32 over metadata and payload.
 *
 *         (5) SIMD REACH. SVE2 was unreachable dead code behind NEON and 8x too
 *         narrow: rewritten around UDOT, dispatched first behind svcntb()>16.
 *         RVV widened the same way (u64m8 <- u8m1/u16m2). Both stay experimental.
 *
 *         Tooling: sign-aware CLI aggregations, legacy header layout in `info`,
 *         the ctypes binding no longer drops values on a signedness flip.
 *         25 test suites; CI gained ASan/UBSan, the three ports and conformance,
 *         and can now actually fail.
 * v3.3.7: AVX-512 u8 sum path (measured ~1.17-1.30x over AVX2; u16 stays on
 *         AVX2 by measurement). EXPERIMENTAL RISC-V RVV and ARM SVE2 paths,
 *         logic-validated via emulation (tests/rvv_emu, tests/sve2_emu),
 *         pending hardware. New benchmarks/ programs. Test suite: 17 suites.
 * v3.3.6: Analytics v2 - sort/is_sorted, unique_sorted, nunique e
 *         value_counts for compact integer arrays.
 * v3.3.5: fix - class promotion with an empty pool (count==0) did not
 *         readjust capacity to the already-allocated buffer (could overflow if the
 *         first value required a larger class). Found via ASan; regression test added.
 * v3.3.4: signed PFOR (s2r_blocked_build/get/sum_signed) and block-wise sum
 *         accelerated by SIMD (s2r_blocked_sum_fast: each block reuses the
 *         vpsadbw/NEON dispatch in its own native type).
 * v3.3.3: block-wise width (PFOR) - S2RBlocked: each block chooses its
 *         class; an outlier inflates only its own block (recovers ~3.7x
 *         memory under localized outliers). API: s2r_blocked_build/get/
 *         sum/max/bytes/free. Scope: unsigned.
 * v3.3.2: Analytics module merged into the single header - bidirectional width
 *         (s2r_remove_swap, s2r_fit_class/self-healing), S2RTracked (min/max in
 *         O(1) on push) and group-by on the compact data (s2r_histogram_u8,
 *         s2r_group_sum_u8u32).
 * v3.3.1: SIGNED lazy-carry arithmetic (s2r_add/mul_scalar_signed_safe,
 *         S2RDeferredSigned); NEON path (ARM); big-endian mmap via
 *         copy-on-write (on-disk file left intact).
 * v3.3.0: auto-adaptive push (s2r_push_adaptive); SIMD with dispatch at
 *         runtime (AVX2 vpsadbw, scalar fallback; s2r_sum_fast); mmap
 *         zero-copy (s2r_map_open/close); portable I/O (canonical LE + CRC32).
 * v3.2.1: s2r_stddev() fixed (robust s2r_sqrt, no math.h); aligned
 *         allocation via aligned_alloc (C11); aggregations/filters/statistics
 *         made SIGNED-AWARE (the versions without _signed read bytes as unsigned and
 *         returned wrong results on signed pools).
 * v3.2.0: signed integers (S2R_I8..I64); promote/demote; statistics;
 *         range queries; push_many/transform; S2R_FOREACH; s2r_info.
 *
 * Nota honesta: os caminhos NEON e big-endian sao validados em ambiente
 * EMULATED faithfully to ACLE (they exercise the real code on x86), not on physical silicon.
 * A final build on real ARM and, ideally, on a big-endian host is recommended.
 * The RISC-V Vector (RVV) and ARM SVE2 paths are EXPERIMENTAL: written to the
 * respective intrinsics and validated for LOGIC via emulation (tests/rvv_emu,
 * tests/sve2_emu) on x86, but not yet compiled or run on RISC-V/SVE hardware. Both
 * are gated off by default. The AVX-512 u8 path, by contrast, is compiled and run on
 * x86 here (bit-identical to scalar; ~1.17-1.30x over AVX2 for u8, measured).
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Dual-licensed: AGPL-3.0-or-later OR commercial. See LICENSING.md.
 */

#ifndef SMART2RAW_V3_3_6_H
#define SMART2RAW_V3_3_6_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VERSION
 * ============================================================================ */

#define S2R_VERSION_MAJOR 3
#define S2R_VERSION_MINOR 5
#define S2R_VERSION_PATCH 1
#define S2R_VERSION_STRING "3.5.1"

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

#ifndef S2R_DEBUG
  #define S2R_DEBUG 0
#endif

#if S2R_DEBUG
  #include <assert.h>
  #define S2R_ASSERT(x) assert(x)
#else
  #define S2R_ASSERT(x) ((void)0)
#endif

#ifndef S2R_ALIGNMENT
  #define S2R_ALIGNMENT 64
#endif

/* Growth factor: 1.5x is more memory-efficient than 2x */
#ifndef S2R_GROWTH_FACTOR_NUM
  #define S2R_GROWTH_FACTOR_NUM 3
#endif
#ifndef S2R_GROWTH_FACTOR_DEN
  #define S2R_GROWTH_FACTOR_DEN 2
#endif

/* Initial capacity for auto-grow */
#ifndef S2R_INITIAL_CAPACITY
  #define S2R_INITIAL_CAPACITY 64
#endif

/* ============================================================================
 * ENDIANNESS
 * ============================================================================ */

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
  #define S2R_LITTLE_ENDIAN 1
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  #define S2R_LITTLE_ENDIAN 0
#elif defined(_WIN32) || defined(__x86_64__) || defined(__i386__) || \
      defined(_M_X64) || defined(_M_IX86) || defined(__aarch64__) || \
      defined(__ARMEL__) || defined(__AARCH64EL__)
  #define S2R_LITTLE_ENDIAN 1
#else
  #if !defined(S2R_LITTLE_ENDIAN)
    #warning "Cannot detect endianness, assuming little-endian"
    #define S2R_LITTLE_ENDIAN 1
  #endif
#endif

/* ============================================================================
 * PLATFORM / FEATURE DETECTION (NEW in v3.3)
 * ----------------------------------------------------------------------------
 * Enables lean builds for edge/MCU: define S2R_NO_STDIO (no files)
 * and/or S2R_NO_SIMD (no SIMD dispatch). mmap on POSIX only.
 * ============================================================================ */

#ifndef S2R_NO_STDIO
  #define S2R_HAS_STDIO 1
#else
  #define S2R_HAS_STDIO 0
#endif

#if !defined(S2R_NO_MMAP) && (defined(__linux__) || defined(__unix__) || \
    defined(__APPLE__) || defined(_POSIX_VERSION))
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <unistd.h>
  #define S2R_HAS_MMAP 1
#else
  #define S2R_HAS_MMAP 0
#endif

/* Runtime SIMD dispatch (x86 gcc/clang). On other ISAs it falls back to scalar. */
#if !defined(S2R_NO_SIMD) && defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
  #include <immintrin.h>
  #define S2R_X86_SIMD 1
#else
  #define S2R_X86_SIMD 0
#endif

/* NEON on ARM (aarch64 always has NEON; armv7 only with -mfpu=neon). */
#if !defined(S2R_NO_SIMD) && (defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__))
  #include <arm_neon.h>
  #define S2R_ARM_NEON 1
#else
  #define S2R_ARM_NEON 0
#endif

/* RISC-V Vector (RVV 1.0). EXPERIMENTAL. Enabled when the target provides the
 * RVV C intrinsics (__riscv_v_intrinsic, set by -march=rv64gcv on gcc/clang),
 * or forced for the emulation test (S2R_FORCE_RVV). On any other target this is
 * 0 and the scalar path is used. */
#if !defined(S2R_NO_SIMD) && (defined(__riscv_v_intrinsic) || defined(S2R_FORCE_RVV))
  #include <riscv_vector.h>
  #define S2R_RISCV_RVV 1
#else
  #define S2R_RISCV_RVV 0
#endif

/* ARM SVE2. EXPERIMENTAL (same status as RVV). Enabled with __ARM_FEATURE_SVE2
 * (e.g. -march=armv9-a+sve2) or forced for the emulation test (S2R_FORCE_SVE2). */
#if !defined(S2R_NO_SIMD) && (defined(__ARM_FEATURE_SVE2) || defined(S2R_FORCE_SVE2))
  #include <arm_sve.h>
  #define S2R_ARM_SVE2 1
#else
  #define S2R_ARM_SVE2 0
#endif

/* ============================================================================
 * ERROR CODES (NEW in v3.2)
 * ============================================================================ */

typedef enum {
    S2R_OK = 0,
    S2R_ERR_NULL = -1,
    S2R_ERR_OOM = -2,
    S2R_ERR_OVERFLOW = -3,
    S2R_ERR_INVALID_SIZE = -4,
    S2R_ERR_VALUE_TOO_LARGE = -5,
    S2R_ERR_VALUE_TOO_SMALL = -6,
    S2R_ERR_EMPTY = -7,
    S2R_ERR_IO = -8,
    S2R_ERR_CORRUPT = -9
} S2RError;

/* ============================================================================
 * TYPES
 * ============================================================================ */

/* Unsigned types (original) */
typedef enum {
    S2R_8  = 8,
    S2R_16 = 16,
    S2R_32 = 32,
    S2R_64 = 64
} S2RSize;

/* Signed types (NEW in v3.2) */
typedef enum {
    S2R_I8  = -8,
    S2R_I16 = -16,
    S2R_I32 = -32,
    S2R_I64 = -64
} S2RSizeSigned;

/* Pool flags (NEW in v3.2) */
typedef enum {
    S2R_FLAG_NONE     = 0,
    S2R_FLAG_SIGNED   = 1 << 0,  /* Pool contains signed integers */
    S2R_FLAG_READONLY = 1 << 1,  /* Pool is read-only (mmap'd file) */
    S2R_FLAG_EXTERNAL = 1 << 2,  /* Data owned externally, don't free */
    S2R_FLAG_SORTED   = 1 << 3,  /* KNOWN non-decreasing; cleared by every write */
    S2R_FLAG_SUMMARY  = 1 << 4   /* smin/smax/ssum are current; cleared by every write */
} S2RFlags;

typedef struct {
    uint8_t  *data;      /* alias-safe buffer */
    size_t    byte_cap;  /* allocated bytes */
    size_t    count;     /* used elements */
    size_t    capacity;  /* capacity in elements */
    int8_t    size;      /* bits per element: ±8/16/32/64 (negative = signed) */
    uint8_t   flags;     /* S2RFlags */
    uint16_t  _reserved; /* padding for alignment */
    /* ---- zone map of a single zone (v3.5.0) ----
     * The block-wise layer has carried a per-block minimum, span and sum since
     * v3.4.0 and answers from them without touching the payload. A flat pool
     * carried nothing, so every predicate paid a full scan even when the class
     * bound alone could not answer but the DATA's own bound could:
     * count_gt(220) on a column that stops at 200 read every byte to return 0.
     * Valid only while S2R_FLAG_SUMMARY is set - see the flag's comment. */
    uint64_t  smin, smax, ssum;
    uint32_t  epoch;     /* bumped by every write; an index built at another
                          * epoch is stale and refuses to answer */
} S2RPool;

/* Pool info structure (NEW in v3.2) */
typedef struct {
    size_t count;
    size_t capacity;
    size_t bytes_used;
    size_t bytes_allocated;
    int    bits_per_element;
    int    is_signed;
    double fill_ratio;
    double memory_efficiency;  /* vs int64_t baseline */
} S2RInfo;

/* ============================================================================
 * ALIGNED ALLOC/FREE
 * ============================================================================ */

#if defined(_WIN32)
  #include <malloc.h>
  static inline void* s2r_aligned_alloc_impl(size_t align, size_t size) {
      return _aligned_malloc(size, align);
  }
  static inline void s2r_aligned_free_impl(void* p) {
      _aligned_free(p);
  }
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__APPLE__)
  /* C11 aligned_alloc: declared by <stdlib.h> with no feature-test macro,
   * compiles cleanly under strict -std=c11. Requires size to be a multiple of align,
   * which this library always guarantees (s2r_align_up for S2R_ALIGNMENT). */
  static inline void* s2r_aligned_alloc_impl(size_t align, size_t size) {
      if (align < sizeof(void*)) align = sizeof(void*);
      size = (size + align - 1) & ~(size_t)(align - 1); /* defensivo */
      return aligned_alloc(align, size);
  }
  static inline void s2r_aligned_free_impl(void* p) {
      free(p);
  }
#else
  static inline void* s2r_aligned_alloc_impl(size_t align, size_t size) {
      void* p = NULL;
      #if defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
        if (posix_memalign(&p, align, size) != 0) return NULL;
        return p;
      #else
        if (align < sizeof(void*)) align = sizeof(void*);
        uintptr_t raw = (uintptr_t)malloc(size + align - 1 + sizeof(void*));
        if (!raw) return NULL;
        uintptr_t aligned = (raw + sizeof(void*) + align - 1) & ~(uintptr_t)(align - 1);
        ((void**)aligned)[-1] = (void*)raw;
        return (void*)aligned;
      #endif
  }
  static inline void s2r_aligned_free_impl(void* p) {
      #if defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
        free(p);
      #else
        if (p) free(((void**)p)[-1]);
      #endif
  }
#endif

#define S2R_ALIGNED_ALLOC(sz, al) s2r_aligned_alloc_impl((al), (sz))
#define S2R_ALIGNED_FREE(p)       s2r_aligned_free_impl((p))

/* ============================================================================
 * HELPERS
 * ============================================================================ */

static inline size_t s2r_abs_size(int8_t size) {
    return (size_t)(size < 0 ? -size : size);
}

static inline size_t s2r_elem_bytes(const S2RPool *p) {
    return s2r_abs_size(p->size) >> 3;
}

static inline size_t s2r_used_bytes(const S2RPool *p) {
    return p->count * s2r_elem_bytes(p);
}

static inline int s2r_is_signed(const S2RPool *p) {
    return p->size < 0 || (p->flags & S2R_FLAG_SIGNED);
}

static inline size_t s2r_align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

/* ============================================================================
 * VALUE LIMITS (ENHANCED in v3.2)
 * ============================================================================ */

static inline uint64_t s2r_max_value(int8_t size) {
    switch (size) {
        case S2R_8:  case -S2R_8:  return 0xFFULL;
        case S2R_16: case -S2R_16: return 0xFFFFULL;
        case S2R_32: case -S2R_32: return 0xFFFFFFFFULL;
        case S2R_64: case -S2R_64: return ~0ULL;
        default: return 0;
    }
}

static inline int64_t s2r_min_signed(int8_t size) {
    switch (size) {
        case S2R_I8:  return INT8_MIN;
        case S2R_I16: return INT16_MIN;
        case S2R_I32: return INT32_MIN;
        case S2R_I64: return INT64_MIN;
        default: return 0;
    }
}

static inline int64_t s2r_max_signed(int8_t size) {
    switch (size) {
        case S2R_I8:  return INT8_MAX;
        case S2R_I16: return INT16_MAX;
        case S2R_I32: return INT32_MAX;
        case S2R_I64: return INT64_MAX;
        default: return (int64_t)s2r_max_value(size);
    }
}

/* ============================================================================
 * CLASSIFICATION (ENHANCED in v3.2)
 * ============================================================================ */

static inline S2RSize s2r_classify(uint64_t v) {
    if (v <= 0xFFULL)       return S2R_8;
    if (v <= 0xFFFFULL)     return S2R_16;
    if (v <= 0xFFFFFFFFULL) return S2R_32;
    return S2R_64;
}

/* NEW: Classify signed value */
static inline int8_t s2r_classify_signed(int64_t v) {
    if (v >= INT8_MIN  && v <= INT8_MAX)  return S2R_I8;
    if (v >= INT16_MIN && v <= INT16_MAX) return S2R_I16;
    if (v >= INT32_MIN && v <= INT32_MAX) return S2R_I32;
    return S2R_I64;
}

/* NEW: Classify array (find minimum class needed) */
static inline S2RSize s2r_classify_array(const uint64_t *arr, size_t count) {
    if (count == 0) return S2R_8;
    uint64_t max_v = arr[0];
    for (size_t i = 1; i < count; i++) {
        if (arr[i] > max_v) max_v = arr[i];
    }
    return s2r_classify(max_v);
}

/* NEW: Classify signed array */
static inline int8_t s2r_classify_signed_array(const int64_t *arr, size_t count) {
    if (count == 0) return S2R_I8;
    int64_t min_v = arr[0], max_v = arr[0];
    for (size_t i = 1; i < count; i++) {
        if (arr[i] < min_v) min_v = arr[i];
        if (arr[i] > max_v) max_v = arr[i];
    }
    /* Return the larger class needed */
    int8_t cls_min = s2r_classify_signed(min_v);
    int8_t cls_max = s2r_classify_signed(max_v);
    return (cls_min < cls_max) ? cls_min : cls_max;  /* more negative = larger */
}

/* ============================================================================
 * POOL MANAGEMENT (ENHANCED in v3.2)
 * ============================================================================ */

static inline int s2r_pool_init(S2RPool *p, int8_t size, size_t capacity) {
    S2R_ASSERT(p != NULL);
    
    size_t abs_size = s2r_abs_size(size);
    if (!(abs_size == 8 || abs_size == 16 || abs_size == 32 || abs_size == 64)) {
        return 0;
    }
    
    p->size = size;
    p->count = 0;
    p->capacity = capacity;
    p->data = NULL;
    p->byte_cap = 0;
    p->flags = (size < 0) ? S2R_FLAG_SIGNED : S2R_FLAG_NONE;
    p->_reserved = 0;
    p->smin = 0; p->smax = 0; p->ssum = 0; p->epoch = 0;
    
    if (capacity == 0) return 1;
    
    const size_t elem_bytes = abs_size >> 3;
    if (capacity > SIZE_MAX / elem_bytes) return 0;
    
    const size_t raw_bytes = capacity * elem_bytes;
    const size_t total_bytes = s2r_align_up(raw_bytes, S2R_ALIGNMENT);
    
    p->data = (uint8_t*)S2R_ALIGNED_ALLOC(total_bytes, S2R_ALIGNMENT);
    if (!p->data) return 0;
    
    memset(p->data, 0, total_bytes);
    p->byte_cap = total_bytes;
    return 1;
}

static inline void s2r_pool_free(S2RPool *p) {
    if (!p) return;
    if (p->data && !(p->flags & S2R_FLAG_EXTERNAL)) {
        S2R_ALIGNED_FREE(p->data);
    }
    p->data = NULL;
    p->count = 0;
    p->capacity = 0;
    p->byte_cap = 0;
    p->size = 0;
    p->flags = 0;
}

static inline size_t s2r_pool_bytes(const S2RPool *p) {
    return s2r_used_bytes(p);
}

/* ENHANCED: Smarter growth strategy */
static inline int s2r_reserve(S2RPool *p, size_t new_cap) {
    S2R_ASSERT(p != NULL);
    if (new_cap <= p->capacity) return 1;
    if (p->flags & S2R_FLAG_READONLY) return 0;
    
    const size_t elem_bytes = s2r_elem_bytes(p);
    if (elem_bytes == 0) return 0;
    if (new_cap > SIZE_MAX / elem_bytes) return 0;
    
    const size_t new_raw = new_cap * elem_bytes;
    const size_t new_bytes = s2r_align_up(new_raw, S2R_ALIGNMENT);
    
    uint8_t *nd = (uint8_t*)S2R_ALIGNED_ALLOC(new_bytes, S2R_ALIGNMENT);
    if (!nd) return 0;
    
    const size_t old_used = s2r_used_bytes(p);
    if (old_used && p->data) memcpy(nd, p->data, old_used);
    if (new_bytes > old_used) memset(nd + old_used, 0, new_bytes - old_used);
    
    if (p->data && !(p->flags & S2R_FLAG_EXTERNAL)) {
        S2R_ALIGNED_FREE(p->data);
    }
    p->data = nd;
    p->byte_cap = new_bytes;
    p->capacity = new_cap;
    p->flags &= ~S2R_FLAG_EXTERNAL;
    return 1;
}

/* NEW: Shrink to fit */
static inline int s2r_shrink_to_fit(S2RPool *p) {
    if (!p || p->count == 0) return 1;
    if (p->count == p->capacity) return 1;
    if (p->flags & S2R_FLAG_READONLY) return 0;
    
    const size_t elem_bytes = s2r_elem_bytes(p);
    const size_t new_bytes = s2r_align_up(p->count * elem_bytes, S2R_ALIGNMENT);
    
    uint8_t *nd = (uint8_t*)S2R_ALIGNED_ALLOC(new_bytes, S2R_ALIGNMENT);
    if (!nd) return 0;
    
    memcpy(nd, p->data, p->count * elem_bytes);
    
    if (p->data && !(p->flags & S2R_FLAG_EXTERNAL)) {
        S2R_ALIGNED_FREE(p->data);
    }
    p->data = nd;
    p->byte_cap = new_bytes;
    p->capacity = p->count;
    p->flags &= ~S2R_FLAG_EXTERNAL;
    return 1;
}

/* ============================================================================
 * TYPED ACCESS
 * ============================================================================ */

static inline uint64_t s2r_get(const S2RPool *p, size_t i) {
    S2R_ASSERT(p && i < p->count);
    switch (s2r_abs_size(p->size)) {
        case 8:  return ((const uint8_t*) p->data)[i];
        case 16: return ((const uint16_t*)p->data)[i];
        case 32: return ((const uint32_t*)p->data)[i];
        case 64: return ((const uint64_t*)p->data)[i];
        default: return 0;
    }
}

/* NEW: Get signed value */
static inline int64_t s2r_get_signed(const S2RPool *p, size_t i) {
    S2R_ASSERT(p && i < p->count);
    switch (p->size) {
        case S2R_I8:  return ((const int8_t*)  p->data)[i];
        case S2R_I16: return ((const int16_t*) p->data)[i];
        case S2R_I32: return ((const int32_t*) p->data)[i];
        case S2R_I64: return ((const int64_t*) p->data)[i];
        default:      return (int64_t)s2r_get(p, i);
    }
}

static inline void s2r_set(S2RPool *p, size_t i, uint64_t v) {
    S2R_ASSERT(p && i < p->count);
    S2R_ASSERT(!(p->flags & S2R_FLAG_READONLY));
    p->flags &= (uint8_t)~(S2R_FLAG_SORTED|S2R_FLAG_SUMMARY); p->epoch++;   /* an arbitrary write can break order */
    switch (s2r_abs_size(p->size)) {
        case 8:  ((uint8_t*) p->data)[i] = (uint8_t)v;  break;
        case 16: ((uint16_t*)p->data)[i] = (uint16_t)v; break;
        case 32: ((uint32_t*)p->data)[i] = (uint32_t)v; break;
        case 64: ((uint64_t*)p->data)[i] = (uint64_t)v; break;
    }
}

/* NEW: Set signed value */
static inline void s2r_set_signed(S2RPool *p, size_t i, int64_t v) {
    S2R_ASSERT(p && i < p->count);
    p->flags &= (uint8_t)~(S2R_FLAG_SORTED|S2R_FLAG_SUMMARY); p->epoch++;
    switch (p->size) {
        case S2R_I8:  ((int8_t*)  p->data)[i] = (int8_t)v;  break;
        case S2R_I16: ((int16_t*) p->data)[i] = (int16_t)v; break;
        case S2R_I32: ((int32_t*) p->data)[i] = (int32_t)v; break;
        case S2R_I64: ((int64_t*) p->data)[i] = (int64_t)v; break;
        default:      s2r_set(p, i, (uint64_t)v); break;
    }
}

/* ============================================================================
 * PUSH VARIANTS (ENHANCED in v3.2)
 * ============================================================================ */

/* Original push (may truncate) - kept for compatibility */
static inline int s2r_push(S2RPool *p, uint64_t v) {
    S2R_ASSERT(p != NULL);
    if (p->flags & S2R_FLAG_READONLY) return 0;
    /* An APPEND is the one write that can preserve what we know, and it is the
     * ingest pattern that matters: timestamps and monotonic ids arrive in order,
     * so both facts survive a whole load instead of being thrown away per row.
     *   - order survives if the new value is not smaller than the last;
     *   - the summary survives always, because a new element can only widen a
     *     min/max and only add to a sum.
     * Any other write invalidates both, because it can move a value in a
     * direction no incremental rule can follow. */
    p->epoch++;
    if (p->flags & S2R_FLAG_SORTED) {
        int keep = (p->count == 0);
        if (!keep) {
            if (s2r_is_signed(p)) keep = ((int64_t)v >= s2r_get_signed(p, p->count-1));
            else                  keep = (v >= s2r_get(p, p->count-1));
        }
        if (!keep) p->flags &= (uint8_t)~S2R_FLAG_SORTED;
    }
    if (p->flags & S2R_FLAG_SUMMARY) {
        if (p->count == 0) { p->smin = v; p->smax = v; p->ssum = v; }
        else {
            if (s2r_is_signed(p)) {
                if ((int64_t)v < (int64_t)p->smin) p->smin = v;
                if ((int64_t)v > (int64_t)p->smax) p->smax = v;
            } else {
                if (v < p->smin) p->smin = v;
                if (v > p->smax) p->smax = v;
            }
            p->ssum += v;
        }
    }
    
    if (p->count >= p->capacity) {
        size_t new_cap;
        if (p->capacity == 0) {
            new_cap = S2R_INITIAL_CAPACITY;
        } else {
            /* Growth factor 1.5x */
            if (p->capacity > SIZE_MAX / S2R_GROWTH_FACTOR_NUM) return 0;
            new_cap = (p->capacity * S2R_GROWTH_FACTOR_NUM) / S2R_GROWTH_FACTOR_DEN;
            if (new_cap <= p->capacity) new_cap = p->capacity + 1;
        }
        if (!s2r_reserve(p, new_cap)) return 0;
    }
    
    switch (s2r_abs_size(p->size)) {
        case 8:  ((uint8_t*) p->data)[p->count] = (uint8_t)v;  break;
        case 16: ((uint16_t*)p->data)[p->count] = (uint16_t)v; break;
        case 32: ((uint32_t*)p->data)[p->count] = (uint32_t)v; break;
        case 64: ((uint64_t*)p->data)[p->count] = (uint64_t)v; break;
        default: return 0;
    }
    p->count++;
    return 1;
}

/* Empty pool: when switching class, reinterpret the capacity (in elements)
 * for the ALREADY-allocated buffer, avoiding overflow when the first value needs a
 * class larger than the initial one. (fixes the promotion bug with count==0) */
static inline void s2r__recap_empty(S2RPool *p){
    size_t eb = s2r_abs_size(p->size)/8;
    p->capacity = (eb && p->byte_cap) ? (p->byte_cap/eb) : 0;
}

/* NEW: Push with validation (returns error code) */
static inline S2RError s2r_push_checked(S2RPool *p, uint64_t v) {
    if (!p) return S2R_ERR_NULL;
    if (p->flags & S2R_FLAG_READONLY) return S2R_ERR_INVALID_SIZE;
    
    uint64_t max_v = s2r_max_value(p->size);
    if (v > max_v) return S2R_ERR_VALUE_TOO_LARGE;
    
    if (!s2r_push(p, v)) return S2R_ERR_OOM;
    return S2R_OK;
}

/* NEW: Push signed with validation */
static inline S2RError s2r_push_signed_checked(S2RPool *p, int64_t v) {
    if (!p) return S2R_ERR_NULL;
    if (!s2r_is_signed(p)) return S2R_ERR_INVALID_SIZE;
    
    int64_t min_v = s2r_min_signed(p->size);
    int64_t max_v = s2r_max_signed(p->size);
    if (v < min_v) return S2R_ERR_VALUE_TOO_SMALL;
    if (v > max_v) return S2R_ERR_VALUE_TOO_LARGE;
    
    if (!s2r_push(p, (uint64_t)v)) return S2R_ERR_OOM;
    return S2R_OK;
}

/* NEW: Push with saturation */
static inline int s2r_push_saturate(S2RPool *p, uint64_t v) {
    uint64_t max_v = s2r_max_value(p->size);
    return s2r_push(p, v > max_v ? max_v : v);
}

/* NEW: Push many values at once (bulk) */
static inline size_t s2r_push_many(S2RPool *p, const uint64_t *values, size_t count) {
    if (!p || !values || count == 0) return 0;
    
    /* Pre-reserve for efficiency */
    size_t needed = p->count + count;
    if (needed > p->capacity) {
        if (!s2r_reserve(p, needed)) return 0;
    }
    
    size_t pushed = 0;
    for (size_t i = 0; i < count; i++) {
        if (s2r_push(p, values[i])) pushed++;
    }
    return pushed;
}

/* ============================================================================
 * CLASS PROMOTION/DEMOTION (NEW in v3.2)
 * ============================================================================ */

/* Promote to larger class (always safe) */
static inline int s2r_promote(S2RPool *p, int8_t new_size) {
    if (!p || p->count == 0) return 0;
    
    size_t old_abs = s2r_abs_size(p->size);
    size_t new_abs = s2r_abs_size(new_size);
    
    /* Can only promote to larger */
    if (new_abs <= old_abs) return 0;
    
    /* Allocate new buffer */
    size_t new_elem_bytes = new_abs >> 3;
    size_t new_raw = p->count * new_elem_bytes;
    size_t new_bytes = s2r_align_up(new_raw, S2R_ALIGNMENT);
    
    uint8_t *nd = (uint8_t*)S2R_ALIGNED_ALLOC(new_bytes, S2R_ALIGNMENT);
    if (!nd) return 0;
    
    /* Convert elements */
    size_t n = p->count;
    
    if (s2r_is_signed(p)) {
        /* Signed promotion */
        switch (new_size) {
            case S2R_I16:
                for (size_t i = 0; i < n; i++)
                    ((int16_t*)nd)[i] = (int16_t)s2r_get_signed(p, i);
                break;
            case S2R_I32:
                for (size_t i = 0; i < n; i++)
                    ((int32_t*)nd)[i] = (int32_t)s2r_get_signed(p, i);
                break;
            case S2R_I64:
                for (size_t i = 0; i < n; i++)
                    ((int64_t*)nd)[i] = s2r_get_signed(p, i);
                break;
            default:
                S2R_ALIGNED_FREE(nd);
                return 0;
        }
    } else {
        /* Unsigned promotion */
        switch (new_size) {
            case S2R_16:
                for (size_t i = 0; i < n; i++)
                    ((uint16_t*)nd)[i] = (uint16_t)s2r_get(p, i);
                break;
            case S2R_32:
                for (size_t i = 0; i < n; i++)
                    ((uint32_t*)nd)[i] = (uint32_t)s2r_get(p, i);
                break;
            case S2R_64:
                for (size_t i = 0; i < n; i++)
                    ((uint64_t*)nd)[i] = s2r_get(p, i);
                break;
            default:
                S2R_ALIGNED_FREE(nd);
                return 0;
        }
    }
    
    /* Replace buffer */
    if (p->data && !(p->flags & S2R_FLAG_EXTERNAL)) {
        S2R_ALIGNED_FREE(p->data);
    }
    p->data = nd;
    p->byte_cap = new_bytes;
    p->capacity = p->count;
    p->size = new_size;
    p->flags = (new_size < 0) ? S2R_FLAG_SIGNED : S2R_FLAG_NONE;
    
    return 1;
}

/* Demote to smaller class (validates all values fit) */
static inline int s2r_demote(S2RPool *p, int8_t new_size) {
    if (!p || p->count == 0) return 0;
    
    size_t old_abs = s2r_abs_size(p->size);
    size_t new_abs = s2r_abs_size(new_size);
    
    /* Can only demote to smaller */
    if (new_abs >= old_abs) return 0;
    
    /* Validate all values fit */
    uint64_t new_max = s2r_max_value(new_size);
    for (size_t i = 0; i < p->count; i++) {
        if (s2r_get(p, i) > new_max) return 0;  /* Value doesn't fit */
    }
    
    /* Allocate new buffer */
    size_t new_elem_bytes = new_abs >> 3;
    size_t new_raw = p->count * new_elem_bytes;
    size_t new_bytes = s2r_align_up(new_raw, S2R_ALIGNMENT);
    
    uint8_t *nd = (uint8_t*)S2R_ALIGNED_ALLOC(new_bytes, S2R_ALIGNMENT);
    if (!nd) return 0;
    
    /* Convert elements */
    size_t n = p->count;
    switch (new_size) {
        case S2R_8:
            for (size_t i = 0; i < n; i++)
                nd[i] = (uint8_t)s2r_get(p, i);
            break;
        case S2R_16:
            for (size_t i = 0; i < n; i++)
                ((uint16_t*)nd)[i] = (uint16_t)s2r_get(p, i);
            break;
        case S2R_32:
            for (size_t i = 0; i < n; i++)
                ((uint32_t*)nd)[i] = (uint32_t)s2r_get(p, i);
            break;
        default:
            S2R_ALIGNED_FREE(nd);
            return 0;
    }
    
    /* Replace buffer */
    if (p->data && !(p->flags & S2R_FLAG_EXTERNAL)) {
        S2R_ALIGNED_FREE(p->data);
    }
    p->data = nd;
    p->byte_cap = new_bytes;
    p->capacity = p->count;
    p->size = new_size;
    
    return 1;
}

/* ============================================================================
 * AGGREGATIONS
 * ============================================================================ */

static inline uint64_t s2r_sum(const S2RPool *p) {
    if (!p || p->count == 0) return 0;
    uint64_t sum = 0;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8: {
            const uint8_t * __restrict a = (const uint8_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += a[i];
            break;
        }
        case 16: {
            const uint16_t * __restrict a = (const uint16_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += a[i];
            break;
        }
        case 32: {
            const uint32_t * __restrict a = (const uint32_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += a[i];
            break;
        }
        case 64: {
            const uint64_t * __restrict a = (const uint64_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += a[i];
            break;
        }
    }
    return sum;
}

/* NEW: Sum for signed */
static inline int64_t s2r_sum_signed(const S2RPool *p) {
    if (!p || p->count == 0) return 0;
    /* Accumulate UNSIGNED. Signed overflow is undefined behaviour, and summing a
     * large i64 pool reaches it easily (caught by UBSan); the unsigned twin
     * s2r_sum has always accumulated in uint64_t for exactly this reason.
     * Two's-complement addition is bit-identical, so the returned value is the
     * same wraparound result the caller already got in practice - only now it is
     * defined by the standard instead of by the compiler's mood. */
    uint64_t sum = 0;
    const size_t n = p->count;
    
    switch (p->size) {
        case S2R_I8: {
            const int8_t *a = (const int8_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += (uint64_t)a[i];
            break;
        }
        case S2R_I16: {
            const int16_t *a = (const int16_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += (uint64_t)a[i];
            break;
        }
        case S2R_I32: {
            const int32_t *a = (const int32_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += (uint64_t)a[i];
            break;
        }
        case S2R_I64: {
            const int64_t *a = (const int64_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += (uint64_t)a[i];
            break;
        }
        default:
            return (int64_t)s2r_sum(p);
    }
    return (int64_t)sum;
}

static inline uint64_t s2r_min(const S2RPool *p) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8: {
            const uint8_t *a = (const uint8_t*)p->data;
            uint8_t m = a[0];
            for (size_t i = 1; i < n; i++) if (a[i] < m) m = a[i];
            return m;
        }
        case 16: {
            const uint16_t *a = (const uint16_t*)p->data;
            uint16_t m = a[0];
            for (size_t i = 1; i < n; i++) if (a[i] < m) m = a[i];
            return m;
        }
        case 32: {
            const uint32_t *a = (const uint32_t*)p->data;
            uint32_t m = a[0];
            for (size_t i = 1; i < n; i++) if (a[i] < m) m = a[i];
            return m;
        }
        case 64: {
            const uint64_t *a = (const uint64_t*)p->data;
            uint64_t m = a[0];
            for (size_t i = 1; i < n; i++) if (a[i] < m) m = a[i];
            return m;
        }
    }
    return 0;
}

static inline uint64_t s2r_max(const S2RPool *p) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8: {
            const uint8_t *a = (const uint8_t*)p->data;
            uint8_t m = a[0];
            for (size_t i = 1; i < n; i++) if (a[i] > m) m = a[i];
            return m;
        }
        case 16: {
            const uint16_t *a = (const uint16_t*)p->data;
            uint16_t m = a[0];
            for (size_t i = 1; i < n; i++) if (a[i] > m) m = a[i];
            return m;
        }
        case 32: {
            const uint32_t *a = (const uint32_t*)p->data;
            uint32_t m = a[0];
            for (size_t i = 1; i < n; i++) if (a[i] > m) m = a[i];
            return m;
        }
        case 64: {
            const uint64_t *a = (const uint64_t*)p->data;
            uint64_t m = a[0];
            for (size_t i = 1; i < n; i++) if (a[i] > m) m = a[i];
            return m;
        }
    }
    return 0;
}

/* ============================================================================
 * STATISTICS (NEW in v3.2)
 * ============================================================================ */

static inline double s2r_mean(const S2RPool *p) {
    if (!p || p->count == 0) return 0.0;
    return (double)s2r_sum(p) / (double)p->count;
}

static inline double s2r_mean_signed(const S2RPool *p) {
    if (!p || p->count == 0) return 0.0;
    return (double)s2r_sum_signed(p) / (double)p->count;
}

static inline double s2r_variance(const S2RPool *p) {
    if (!p || p->count < 2) return 0.0;
    
    double mean = s2r_mean(p);
    double sum_sq = 0.0;
    const size_t n = p->count;
    
    for (size_t i = 0; i < n; i++) {
        double diff = (double)s2r_get(p, i) - mean;
        sum_sq += diff * diff;
    }
    
    return sum_sq / (double)(n - 1);  /* Sample variance */
}

/* Square root with no dependency on math.h.
 * Normalizes v to [0.25, 4) by powers of 4 (exact scaling in binary),
 * runs Newton-Raphson (quadratic convergence in that interval) and rescales.
 * Robust across the whole double range, including very large variances. */
static inline double s2r_sqrt(double v) {
    if (v <= 0.0) return 0.0;
    double scale = 1.0;
    while (v >= 4.0)  { v *= 0.25; scale *= 2.0; }
    while (v < 0.25)  { v *= 4.0;  scale *= 0.5; }
    double x = (v + 1.0) * 0.5;  /* seed in [~0.31, 2.5) */
    for (int i = 0; i < 8; i++) x = 0.5 * (x + v / x);
    return x * scale;
}

static inline double s2r_stddev(const S2RPool *p) {
    return s2r_sqrt(s2r_variance(p));
}

/* ============================================================================
 * SIGNED-AWARE AGGREGATIONS / FILTERS / STATS (NEW in v3.2.1)
 * ----------------------------------------------------------------------------
 * The versions without the (_signed) suffix interpret bytes as UNSIGNED and give
 * wrong results on signed pools. Use these for S2R_Ixx pools.
 * ============================================================================ */

static inline int64_t s2r_min_signed_val(const S2RPool *p) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    int64_t m = s2r_get_signed(p, 0);
    for (size_t i = 1; i < n; i++) {
        int64_t v = s2r_get_signed(p, i);
        if (v < m) m = v;
    }
    return m;
}

static inline int64_t s2r_max_signed_val(const S2RPool *p) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    int64_t m = s2r_get_signed(p, 0);
    for (size_t i = 1; i < n; i++) {
        int64_t v = s2r_get_signed(p, i);
        if (v > m) m = v;
    }
    return m;
}

static inline double s2r_variance_signed(const S2RPool *p) {
    if (!p || p->count < 2) return 0.0;
    double mean = s2r_mean_signed(p);
    double sum_sq = 0.0;
    const size_t n = p->count;
    for (size_t i = 0; i < n; i++) {
        double diff = (double)s2r_get_signed(p, i) - mean;
        sum_sq += diff * diff;
    }
    return sum_sq / (double)(n - 1);  /* sample variance */
}

static inline double s2r_stddev_signed(const S2RPool *p) {
    return s2r_sqrt(s2r_variance_signed(p));
}

static inline size_t s2r_count_gt_signed(const S2RPool *p, int64_t thr) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    size_t cnt = 0;
    for (size_t i = 0; i < n; i++) cnt += (s2r_get_signed(p, i) > thr);
    return cnt;
}

static inline size_t s2r_count_lt_signed(const S2RPool *p, int64_t thr) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    size_t cnt = 0;
    for (size_t i = 0; i < n; i++) cnt += (s2r_get_signed(p, i) < thr);
    return cnt;
}

static inline size_t s2r_count_eq_signed(const S2RPool *p, int64_t value) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    size_t cnt = 0;
    for (size_t i = 0; i < n; i++) cnt += (s2r_get_signed(p, i) == value);
    return cnt;
}

static inline size_t s2r_count_range_signed(const S2RPool *p, int64_t min_v, int64_t max_v) {
    if (!p || p->count == 0 || min_v > max_v) return 0;
    const size_t n = p->count;
    size_t cnt = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t v = s2r_get_signed(p, i);
        cnt += (v >= min_v && v <= max_v);
    }
    return cnt;
}

static inline int64_t s2r_sum_if_signed(const S2RPool *p, int64_t min_v, int64_t max_v) {
    if (!p || p->count == 0 || min_v > max_v) return 0;
    const size_t n = p->count;
    uint64_t sum = 0;                       /* unsigned: signed overflow is UB */
    for (size_t i = 0; i < n; i++) {
        int64_t v = s2r_get_signed(p, i);
        if (v >= min_v && v <= max_v) sum += (uint64_t)v;
    }
    return (int64_t)sum;
}

/* ============================================================================
 * FILTERS AND COUNTS
 * ============================================================================ */

static inline size_t s2r_count_gt(const S2RPool *p, uint64_t thr) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    size_t cnt = 0;
    
    switch (s2r_abs_size(p->size)) {
        case 8: {
            if (thr >= 0xFFULL) return 0;
            const uint8_t t = (uint8_t)thr;
            const uint8_t *a = (const uint8_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] > t);
            break;
        }
        case 16: {
            if (thr >= 0xFFFFULL) return 0;
            const uint16_t t = (uint16_t)thr;
            const uint16_t *a = (const uint16_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] > t);
            break;
        }
        case 32: {
            if (thr >= 0xFFFFFFFFULL) return 0;
            const uint32_t t = (uint32_t)thr;
            const uint32_t *a = (const uint32_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] > t);
            break;
        }
        case 64: {
            if (thr == ~0ULL) return 0;
            const uint64_t *a = (const uint64_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] > thr);
            break;
        }
    }
    return cnt;
}

static inline size_t s2r_count_lt(const S2RPool *p, uint64_t thr) {
    if (!p || p->count == 0) return 0;
    if (thr == 0) return 0;
    const size_t n = p->count;
    size_t cnt = 0;
    
    switch (s2r_abs_size(p->size)) {
        case 8: {
            if (thr > 0xFFULL) return n;
            const uint8_t t = (uint8_t)thr;
            const uint8_t *a = (const uint8_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] < t);
            break;
        }
        case 16: {
            if (thr > 0xFFFFULL) return n;
            const uint16_t t = (uint16_t)thr;
            const uint16_t *a = (const uint16_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] < t);
            break;
        }
        case 32: {
            if (thr > 0xFFFFFFFFULL) return n;
            const uint32_t t = (uint32_t)thr;
            const uint32_t *a = (const uint32_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] < t);
            break;
        }
        case 64: {
            const uint64_t *a = (const uint64_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] < thr);
            break;
        }
    }
    return cnt;
}

static inline size_t s2r_count_eq(const S2RPool *p, uint64_t value) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    size_t cnt = 0;
    
    switch (s2r_abs_size(p->size)) {
        case 8: {
            if (value > 0xFFULL) return 0;
            const uint8_t v = (uint8_t)value;
            const uint8_t *a = (const uint8_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] == v);
            break;
        }
        case 16: {
            if (value > 0xFFFFULL) return 0;
            const uint16_t v = (uint16_t)value;
            const uint16_t *a = (const uint16_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] == v);
            break;
        }
        case 32: {
            if (value > 0xFFFFFFFFULL) return 0;
            const uint32_t v = (uint32_t)value;
            const uint32_t *a = (const uint32_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] == v);
            break;
        }
        case 64: {
            const uint64_t *a = (const uint64_t*)p->data;
            for (size_t i = 0; i < n; i++) cnt += (a[i] == value);
            break;
        }
    }
    return cnt;
}

/* NEW: Count in range [min, max] */
static inline size_t s2r_count_range(const S2RPool *p, uint64_t min_v, uint64_t max_v) {
    if (!p || p->count == 0) return 0;
    if (min_v > max_v) return 0;
    const size_t n = p->count;
    size_t cnt = 0;
    
    for (size_t i = 0; i < n; i++) {
        uint64_t v = s2r_get(p, i);
        cnt += (v >= min_v && v <= max_v);
    }
    return cnt;
}

/* NEW: Conditional sum */
static inline uint64_t s2r_sum_if(const S2RPool *p, uint64_t min_v, uint64_t max_v) {
    if (!p || p->count == 0) return 0;
    const size_t n = p->count;
    uint64_t sum = 0;
    
    for (size_t i = 0; i < n; i++) {
        uint64_t v = s2r_get(p, i);
        if (v >= min_v && v <= max_v) sum += v;
    }
    return sum;
}

static inline int64_t s2r_find(const S2RPool *p, uint64_t value) {
    if (!p || p->count == 0) return -1;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8: {
            if (value > 0xFFULL) return -1;
            const uint8_t v = (uint8_t)value;
            const uint8_t *a = (const uint8_t*)p->data;
            for (size_t i = 0; i < n; i++) if (a[i] == v) return (int64_t)i;
            break;
        }
        case 16: {
            if (value > 0xFFFFULL) return -1;
            const uint16_t v = (uint16_t)value;
            const uint16_t *a = (const uint16_t*)p->data;
            for (size_t i = 0; i < n; i++) if (a[i] == v) return (int64_t)i;
            break;
        }
        case 32: {
            if (value > 0xFFFFFFFFULL) return -1;
            const uint32_t v = (uint32_t)value;
            const uint32_t *a = (const uint32_t*)p->data;
            for (size_t i = 0; i < n; i++) if (a[i] == v) return (int64_t)i;
            break;
        }
        case 64: {
            const uint64_t *a = (const uint64_t*)p->data;
            for (size_t i = 0; i < n; i++) if (a[i] == value) return (int64_t)i;
            break;
        }
    }
    return -1;
}

/* ============================================================================
 * ARITHMETIC
 * ============================================================================ */

static inline void s2r_add_scalar(S2RPool *p, uint64_t s) {
    if (!p || p->count == 0) return;
    p->flags &= (uint8_t)~(S2R_FLAG_SORTED|S2R_FLAG_SUMMARY); p->epoch++;   /* wraparound can reorder */
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8:  { uint8_t  *a=(uint8_t*)p->data;  const uint8_t  v=(uint8_t)s;  for(size_t i=0;i<n;i++) a[i]+=v; break; }
        case 16: { uint16_t *a=(uint16_t*)p->data; const uint16_t v=(uint16_t)s; for(size_t i=0;i<n;i++) a[i]+=v; break; }
        case 32: { uint32_t *a=(uint32_t*)p->data; const uint32_t v=(uint32_t)s; for(size_t i=0;i<n;i++) a[i]+=v; break; }
        case 64: { uint64_t *a=(uint64_t*)p->data; for(size_t i=0;i<n;i++) a[i]+=s; break; }
    }
}

static inline void s2r_sub_scalar(S2RPool *p, uint64_t s) {
    if (!p || p->count == 0) return;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8:  { uint8_t  *a=(uint8_t*)p->data;  const uint8_t  v=(uint8_t)s;  for(size_t i=0;i<n;i++) a[i]-=v; break; }
        case 16: { uint16_t *a=(uint16_t*)p->data; const uint16_t v=(uint16_t)s; for(size_t i=0;i<n;i++) a[i]-=v; break; }
        case 32: { uint32_t *a=(uint32_t*)p->data; const uint32_t v=(uint32_t)s; for(size_t i=0;i<n;i++) a[i]-=v; break; }
        case 64: { uint64_t *a=(uint64_t*)p->data; for(size_t i=0;i<n;i++) a[i]-=s; break; }
    }
}

static inline void s2r_mul_scalar(S2RPool *p, uint64_t s) {
    if (!p || p->count == 0) return;
    p->flags &= (uint8_t)~(S2R_FLAG_SORTED|S2R_FLAG_SUMMARY); p->epoch++;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        /* Integer promotion trap: `a[i] *= v` on narrow unsigned types promotes BOTH
         * operands to int, so the product is computed in signed arithmetic. At width
         * 16 that overflows int (e.g. 65436 * 65533 = 4288236588 > INT_MAX), which is
         * undefined behaviour, not the wraparound this code relies on - and the whole
         * lazy-carry design (replay in Z/2^w after a single promotion) DEPENDS on
         * well-defined wraparound. Caught by UBSan in test_signed_lazy.
         * Multiplying through unsigned int makes the wrap defined by the standard.
         * Width 8 (max 255*255 = 65025) and add/sub at width 16 (max 131070) stay
         * inside int and are safe, but are written the same way for uniformity. */
        case 8:  { uint8_t  *a=(uint8_t*)p->data;  const unsigned v=(unsigned)(uint8_t)s;  for(size_t i=0;i<n;i++) a[i]=(uint8_t)((unsigned)a[i]*v);  break; }
        case 16: { uint16_t *a=(uint16_t*)p->data; const unsigned v=(unsigned)(uint16_t)s; for(size_t i=0;i<n;i++) a[i]=(uint16_t)((unsigned)a[i]*v); break; }
        case 32: { uint32_t *a=(uint32_t*)p->data; const uint32_t v=(uint32_t)s; for(size_t i=0;i<n;i++) a[i]*=v; break; }
        case 64: { uint64_t *a=(uint64_t*)p->data; for(size_t i=0;i<n;i++) a[i]*=s; break; }
    }
}

/* NEW: Transform with custom function */
typedef uint64_t (*s2r_transform_fn)(uint64_t value, void *ctx);

static inline void s2r_transform(S2RPool *p, s2r_transform_fn fn, void *ctx) {
    if (!p || !fn || p->count == 0) return;
    const size_t n = p->count;
    
    for (size_t i = 0; i < n; i++) {
        uint64_t v = s2r_get(p, i);
        s2r_set(p, i, fn(v, ctx));
    }
}

/* ============================================================================
 * BITWISE
 * ============================================================================ */

static inline void s2r_xor_scalar(S2RPool *p, uint64_t value) {
    if (!p || p->count == 0) return;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8:  { uint8_t  *a=(uint8_t*)p->data;  uint8_t  v=(uint8_t)value;  for(size_t i=0;i<n;i++) a[i]^=v; break; }
        case 16: { uint16_t *a=(uint16_t*)p->data; uint16_t v=(uint16_t)value; for(size_t i=0;i<n;i++) a[i]^=v; break; }
        case 32: { uint32_t *a=(uint32_t*)p->data; uint32_t v=(uint32_t)value; for(size_t i=0;i<n;i++) a[i]^=v; break; }
        case 64: { uint64_t *a=(uint64_t*)p->data; for(size_t i=0;i<n;i++) a[i]^=value; break; }
    }
}

static inline void s2r_and_scalar(S2RPool *p, uint64_t value) {
    if (!p || p->count == 0) return;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8:  { uint8_t  *a=(uint8_t*)p->data;  uint8_t  v=(uint8_t)value;  for(size_t i=0;i<n;i++) a[i]&=v; break; }
        case 16: { uint16_t *a=(uint16_t*)p->data; uint16_t v=(uint16_t)value; for(size_t i=0;i<n;i++) a[i]&=v; break; }
        case 32: { uint32_t *a=(uint32_t*)p->data; uint32_t v=(uint32_t)value; for(size_t i=0;i<n;i++) a[i]&=v; break; }
        case 64: { uint64_t *a=(uint64_t*)p->data; for(size_t i=0;i<n;i++) a[i]&=value; break; }
    }
}

static inline void s2r_or_scalar(S2RPool *p, uint64_t value) {
    if (!p || p->count == 0) return;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8:  { uint8_t  *a=(uint8_t*)p->data;  uint8_t  v=(uint8_t)value;  for(size_t i=0;i<n;i++) a[i]|=v; break; }
        case 16: { uint16_t *a=(uint16_t*)p->data; uint16_t v=(uint16_t)value; for(size_t i=0;i<n;i++) a[i]|=v; break; }
        case 32: { uint32_t *a=(uint32_t*)p->data; uint32_t v=(uint32_t)value; for(size_t i=0;i<n;i++) a[i]|=v; break; }
        case 64: { uint64_t *a=(uint64_t*)p->data; for(size_t i=0;i<n;i++) a[i]|=value; break; }
    }
}

static inline void s2r_not(S2RPool *p) {
    if (!p || p->count == 0) return;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8:  { uint8_t  *a=(uint8_t*)p->data;  for(size_t i=0;i<n;i++) a[i]=(uint8_t)~a[i]; break; }
        case 16: { uint16_t *a=(uint16_t*)p->data; for(size_t i=0;i<n;i++) a[i]=(uint16_t)~a[i]; break; }
        case 32: { uint32_t *a=(uint32_t*)p->data; for(size_t i=0;i<n;i++) a[i]=~a[i]; break; }
        case 64: { uint64_t *a=(uint64_t*)p->data; for(size_t i=0;i<n;i++) a[i]=~a[i]; break; }
    }
}

/* ============================================================================
 * STREAMING / CONVERSION
 * ============================================================================ */

static inline int s2r_from_array_auto(S2RPool *p, const uint64_t *arr, size_t count) {
    if (count == 0) return s2r_pool_init(p, S2R_8, 0);
    
    S2RSize sz = s2r_classify_array(arr, count);
    if (!s2r_pool_init(p, sz, count)) return 0;
    
    switch (sz) {
        case S2R_8:  { uint8_t  *d=(uint8_t*) p->data; for(size_t i=0;i<count;i++) d[i]=(uint8_t)arr[i]; break; }
        case S2R_16: { uint16_t *d=(uint16_t*)p->data; for(size_t i=0;i<count;i++) d[i]=(uint16_t)arr[i]; break; }
        case S2R_32: { uint32_t *d=(uint32_t*)p->data; for(size_t i=0;i<count;i++) d[i]=(uint32_t)arr[i]; break; }
        case S2R_64: { memcpy(p->data, arr, count * 8); break; }
    }
    
    p->count = count;
    return 1;
}

/* NEW: From signed array with auto-classification */
static inline int s2r_from_array_signed_auto(S2RPool *p, const int64_t *arr, size_t count) {
    if (count == 0) return s2r_pool_init(p, S2R_I8, 0);
    
    int8_t sz = s2r_classify_signed_array(arr, count);
    if (!s2r_pool_init(p, sz, count)) return 0;
    
    switch (sz) {
        case S2R_I8:  { int8_t  *d=(int8_t*) p->data; for(size_t i=0;i<count;i++) d[i]=(int8_t)arr[i]; break; }
        case S2R_I16: { int16_t *d=(int16_t*)p->data; for(size_t i=0;i<count;i++) d[i]=(int16_t)arr[i]; break; }
        case S2R_I32: { int32_t *d=(int32_t*)p->data; for(size_t i=0;i<count;i++) d[i]=(int32_t)arr[i]; break; }
        case S2R_I64: { memcpy(p->data, arr, count * 8); break; }
    }
    
    p->count = count;
    return 1;
}

static inline void s2r_to_array_any(const S2RPool *p, uint64_t *arr) {
    if (!p || !arr || p->count == 0) return;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8:  { const uint8_t  *a=(const uint8_t*) p->data; for(size_t i=0;i<n;i++) arr[i]=a[i]; break; }
        case 16: { const uint16_t *a=(const uint16_t*)p->data; for(size_t i=0;i<n;i++) arr[i]=a[i]; break; }
        case 32: { const uint32_t *a=(const uint32_t*)p->data; for(size_t i=0;i<n;i++) arr[i]=a[i]; break; }
        case 64: { const uint64_t *a=(const uint64_t*)p->data; for(size_t i=0;i<n;i++) arr[i]=a[i]; break; }
    }
}

/* ============================================================================
 * UTILITY
 * ============================================================================ */

static inline int s2r_copy(S2RPool *dst, const S2RPool *src) {
    if (!s2r_pool_init(dst, src->size, src->count)) return 0;
    const size_t bytes = s2r_used_bytes(src);
    if (bytes) memcpy(dst->data, src->data, bytes);
    dst->count = src->count;
    dst->flags = src->flags & ~S2R_FLAG_EXTERNAL;
    return 1;
}

static inline void s2r_fill(S2RPool *p, uint64_t value) {
    if (!p || p->count == 0) return;
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8:  { uint8_t  *a=(uint8_t*) p->data; uint8_t  v=(uint8_t) value; for(size_t i=0;i<n;i++) a[i]=v; break; }
        case 16: { uint16_t *a=(uint16_t*)p->data; uint16_t v=(uint16_t)value; for(size_t i=0;i<n;i++) a[i]=v; break; }
        case 32: { uint32_t *a=(uint32_t*)p->data; uint32_t v=(uint32_t)value; for(size_t i=0;i<n;i++) a[i]=v; break; }
        case 64: { uint64_t *a=(uint64_t*)p->data; for(size_t i=0;i<n;i++) a[i]=value; break; }
    }
}

static inline void s2r_clear(S2RPool *p) {
    if (p) { p->count = 0; p->flags &= (uint8_t)~(S2R_FLAG_SORTED|S2R_FLAG_SUMMARY); p->epoch++; }
}

/* Direct access */
static inline uint8_t*  s2r_as_u8 (S2RPool *p) { return (uint8_t*) p->data; }
static inline uint16_t* s2r_as_u16(S2RPool *p) { return (uint16_t*)p->data; }
static inline uint32_t* s2r_as_u32(S2RPool *p) { return (uint32_t*)p->data; }
static inline uint64_t* s2r_as_u64(S2RPool *p) { return (uint64_t*)p->data; }
static inline int8_t*   s2r_as_i8 (S2RPool *p) { return (int8_t*)  p->data; }
static inline int16_t*  s2r_as_i16(S2RPool *p) { return (int16_t*) p->data; }
static inline int32_t*  s2r_as_i32(S2RPool *p) { return (int32_t*) p->data; }
static inline int64_t*  s2r_as_i64(S2RPool *p) { return (int64_t*) p->data; }

/* ============================================================================
 * DIAGNOSTICS (NEW in v3.2)
 * ============================================================================ */

static inline S2RInfo s2r_info(const S2RPool *p) {
    S2RInfo info = {0};
    if (!p) return info;
    
    info.count = p->count;
    info.capacity = p->capacity;
    info.bytes_used = s2r_used_bytes(p);
    info.bytes_allocated = p->byte_cap;
    info.bits_per_element = (int)s2r_abs_size(p->size);
    info.is_signed = s2r_is_signed(p);
    info.fill_ratio = p->capacity > 0 ? (double)p->count / (double)p->capacity : 0.0;
    
    /* Memory efficiency vs int64_t baseline */
    size_t baseline = p->count * 8;
    if (baseline > 0) {
        info.memory_efficiency = 1.0 - ((double)info.bytes_used / (double)baseline);
    }
    
    return info;
}

static inline void s2r_print_info(const S2RPool *p, FILE *f) {
    S2RInfo info = s2r_info(p);
    fprintf(f, "S2RPool {\n");
    fprintf(f, "  count:      %zu\n", info.count);
    fprintf(f, "  capacity:   %zu\n", info.capacity);
    fprintf(f, "  bytes_used: %zu\n", info.bytes_used);
    fprintf(f, "  bytes_cap:  %zu\n", info.bytes_allocated);
    fprintf(f, "  type:       %s%d\n", info.is_signed ? "S2R_I" : "S2R_", info.bits_per_element);
    fprintf(f, "  fill:       %.1f%%\n", info.fill_ratio * 100);
    fprintf(f, "  efficiency: %.1f%%\n", info.memory_efficiency * 100);
    fprintf(f, "}\n");
}

/* ============================================================================
 * ITERATOR MACRO (NEW in v3.2)
 * ============================================================================ */

#define S2R_FOREACH(pool, idx, val) \
    for (size_t idx = 0, _s2r_n = (pool)->count; \
         idx < _s2r_n && ((val) = s2r_get((pool), idx), 1); \
         idx++)

#define S2R_FOREACH_SIGNED(pool, idx, val) \
    for (size_t idx = 0, _s2r_n = (pool)->count; \
         idx < _s2r_n && ((val) = s2r_get_signed((pool), idx), 1); \
         idx++)

/* ============================================================================
 * SERIALIZATION
 * ============================================================================ */

#define S2R_MAGIC 0x33523253u  /* "S2R3" */
#define S2R_MAGIC_V32 0x32523253u  /* "S2R2" - v3.2 format */

static inline int s2r_save(const S2RPool *p, const char *filename) {
    if (!p) return 0;
    size_t abs_size = s2r_abs_size(p->size);
    if (!(abs_size == 8 || abs_size == 16 || abs_size == 32 || abs_size == 64)) return 0;
    
    FILE *f = fopen(filename, "wb");
    if (!f) return 0;
    
    /* Write header */
    uint32_t magic = S2R_MAGIC_V32;
    uint8_t header[8] = {0};
    header[0] = (uint8_t)p->size;  /* Signed sizes are negative, stored as signed byte */
    header[1] = p->flags;
    
    uint64_t count = (uint64_t)p->count;
    
    if (fwrite(&magic, 4, 1, f) != 1) { fclose(f); return 0; }
    if (fwrite(header, 8, 1, f) != 1) { fclose(f); return 0; }
    if (fwrite(&count, 8, 1, f) != 1) { fclose(f); return 0; }
    
    if (p->count > 0) {
        if (fwrite(p->data, s2r_used_bytes(p), 1, f) != 1) { fclose(f); return 0; }
    }
    
    fclose(f);
    return 1;
}

static inline int s2r_load(S2RPool *p, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    
    uint32_t magic = 0;
    uint8_t header[8] = {0};
    uint64_t count = 0;
    
    if (fread(&magic, 4, 1, f) != 1) { fclose(f); return 0; }
    if (magic != S2R_MAGIC && magic != S2R_MAGIC_V32) { fclose(f); return 0; }
    if (fread(header, 8, 1, f) != 1) { fclose(f); return 0; }
    if (fread(&count, 8, 1, f) != 1) { fclose(f); return 0; }
    
    int8_t size = (int8_t)header[0];
    uint8_t flags = (magic == S2R_MAGIC_V32) ? header[1] : 0;
    
    size_t abs_size = s2r_abs_size(size);
    if (!(abs_size == 8 || abs_size == 16 || abs_size == 32 || abs_size == 64)) {
        fclose(f);
        return 0;
    }
    
    if (count > SIZE_MAX) { fclose(f); return 0; }
    
    if (!s2r_pool_init(p, size, (size_t)count)) { fclose(f); return 0; }
    p->flags = flags;
    
    size_t bytes = (size_t)count * (abs_size >> 3);
    if (bytes > 0) {
        if (fread(p->data, bytes, 1, f) != 1) {
            s2r_pool_free(p);
            fclose(f);
            return 0;
        }
    }
    p->count = (size_t)count;
    
    fclose(f);
    return 1;
}

/* ============================================================================
 * ERROR HANDLING HELPERS
 * ============================================================================ */

static inline const char* s2r_strerror(S2RError err) {
    switch (err) {
        case S2R_OK:                  return "OK";
        case S2R_ERR_NULL:            return "NULL pointer";
        case S2R_ERR_OOM:             return "Out of memory";
        case S2R_ERR_OVERFLOW:        return "Integer overflow";
        case S2R_ERR_INVALID_SIZE:    return "Invalid size class";
        case S2R_ERR_VALUE_TOO_LARGE: return "Value too large for class";
        case S2R_ERR_VALUE_TOO_SMALL: return "Value too small for class";
        case S2R_ERR_EMPTY:           return "Pool is empty";
        case S2R_ERR_IO:              return "I/O error";
        case S2R_ERR_CORRUPT:         return "Corrupted data";
        default:                       return "Unknown error";
    }
}

/* ============================================================================
 * PORTABLE BYTE ORDER + CRC32 (NEW in v3.3)  -  no stdio dependency
 * ============================================================================ */

#define S2R_MAGIC_V33 0x33335253u  /* portable format v3.3 (canonical LE + CRC) */
#define S2R_FMT_VERSION 1u         /* fmt byte written/accepted by this build */

/* Flag bits that are part of the FILE FORMAT. READONLY/EXTERNAL are in-memory
 * ownership state (an mmap'd pool carries them) and must never round-trip through
 * a file: a reader that adopted EXTERNAL would leak the heap buffer it allocated,
 * and one that adopted READONLY would return a permanently immutable pool. */
#define S2R_FLAGS_FORMAT_MASK ((uint8_t)S2R_FLAG_SIGNED)

static inline uint16_t s2r_bswap16(uint16_t x){ return (uint16_t)((x>>8)|(x<<8)); }
static inline uint32_t s2r_bswap32(uint32_t x){
    return ((x>>24)&0x000000FFu)|((x>>8)&0x0000FF00u)|((x<<8)&0x00FF0000u)|((x<<24)&0xFF000000u);
}
static inline uint64_t s2r_bswap64(uint64_t x){
    return ((uint64_t)s2r_bswap32((uint32_t)x)<<32)|(uint64_t)s2r_bswap32((uint32_t)(x>>32));
}

static inline void s2r_swap_payload(void *data, size_t elem_bytes, size_t count){
    switch(elem_bytes){
        case 2:{ uint16_t*a=(uint16_t*)data; for(size_t i=0;i<count;i++) a[i]=s2r_bswap16(a[i]); } break;
        case 4:{ uint32_t*a=(uint32_t*)data; for(size_t i=0;i<count;i++) a[i]=s2r_bswap32(a[i]); } break;
        case 8:{ uint64_t*a=(uint64_t*)data; for(size_t i=0;i<count;i++) a[i]=s2r_bswap64(a[i]); } break;
        default: break;
    }
}

/* CRC32 IEEE 802.3 (reflected poly 0xEDB88320), table built once. */
static inline uint32_t s2r_crc32(const void *data, size_t n, uint32_t crc){
    static uint32_t tab[256];
    static int init=0;
    if(!init){
        for(uint32_t i=0;i<256;i++){ uint32_t c=i; for(int k=0;k<8;k++) c=(c>>1)^(0xEDB88320u&(uint32_t)(-(int32_t)(c&1))); tab[i]=c; }
        init=1;
    }
    const uint8_t*p=(const uint8_t*)data; crc=~crc;
    for(size_t i=0;i<n;i++) crc=(crc>>8)^tab[(crc^p[i])&0xFF];
    return ~crc;
}

/* ============================================================================
 * PORTABLE SERIALIZATION (NEW in v3.3)  -  canonical LE format + CRC
 * ============================================================================ */
#if S2R_HAS_STDIO

static inline int s2r__wr_le32(FILE*f,uint32_t v){ uint8_t b[4]; for(int i=0;i<4;i++)b[i]=(uint8_t)(v>>(8*i)); return fwrite(b,4,1,f)==1; }
static inline int s2r__wr_le64(FILE*f,uint64_t v){ uint8_t b[8]; for(int i=0;i<8;i++)b[i]=(uint8_t)(v>>(8*i)); return fwrite(b,8,1,f)==1; }
static inline int s2r__rd_le32(FILE*f,uint32_t*v){ uint8_t b[4]; if(fread(b,4,1,f)!=1)return 0; *v=0; for(int i=0;i<4;i++)*v|=(uint32_t)b[i]<<(8*i); return 1; }
static inline int s2r__rd_le64(FILE*f,uint64_t*v){ uint8_t b[8]; if(fread(b,8,1,f)!=1)return 0; *v=0; for(int i=0;i<8;i++)*v|=(uint64_t)b[i]<<(8*i); return 1; }

static inline S2RError s2r_save_portable(const S2RPool *p, const char *filename){
    if(!p) return S2R_ERR_NULL;
    size_t abs=s2r_abs_size(p->size);
    if(!(abs==8||abs==16||abs==32||abs==64)) return S2R_ERR_INVALID_SIZE;
    FILE*f=fopen(filename,"wb"); if(!f) return S2R_ERR_IO;
    const size_t eb=abs>>3, bytes=p->count*eb;
    void *canon=NULL; const void *src=p->data;
#if !S2R_LITTLE_ENDIAN
    if(bytes && eb>1){ canon=malloc(bytes); if(!canon){ fclose(f); return S2R_ERR_OOM; }
        memcpy(canon,p->data,bytes); s2r_swap_payload(canon,eb,p->count); src=canon; }
#endif
    uint32_t crc = bytes ? s2r_crc32(src,bytes,0) : 0;
    uint8_t hdr[4]={ (uint8_t)p->size,
                     (uint8_t)(p->flags & S2R_FLAGS_FORMAT_MASK),
                     (uint8_t)S2R_FMT_VERSION, 0 };
    int ok = s2r__wr_le32(f,S2R_MAGIC_V33);
    ok = ok && (fwrite(hdr,4,1,f)==1);
    ok = ok && s2r__wr_le64(f,(uint64_t)p->count);
    if(ok && bytes) ok = (fwrite(src,bytes,1,f)==1);
    ok = ok && s2r__wr_le32(f,crc);
    if(canon) free(canon);
    fclose(f);
    return ok ? S2R_OK : S2R_ERR_IO;
}

static inline S2RError s2r_load_portable(S2RPool *p, const char *filename){
    FILE*f=fopen(filename,"rb"); if(!f) return S2R_ERR_IO;
    uint32_t magic; if(!s2r__rd_le32(f,&magic)){ fclose(f); return S2R_ERR_IO; }
    int is_v33=(magic==S2R_MAGIC_V33);
    int is_old=(magic==S2R_MAGIC || magic==S2R_MAGIC_V32);
    if(!is_v33 && !is_old){ fclose(f); return S2R_ERR_CORRUPT; }
    uint8_t hdr[8]={0}; size_t hdr_len=is_v33?4:8;
    if(fread(hdr,hdr_len,1,f)!=1){ fclose(f); return S2R_ERR_IO; }
    int8_t size=(int8_t)hdr[0];
    /* Mask off non-format bits: never adopt ownership state from a file. */
    uint8_t flags=(uint8_t)(((is_v33||magic==S2R_MAGIC_V32)?hdr[1]:0) & S2R_FLAGS_FORMAT_MASK);
    if(is_v33){
        /* SPEC_s2r_format.md requires a conforming reader to reject unsupported
         * fmt values and a nonzero reserved byte. The Go/JS/Python ports already
         * did; the C core silently accepted them. */
        if(hdr[2]!=(uint8_t)S2R_FMT_VERSION){ fclose(f); return S2R_ERR_CORRUPT; }
        if(hdr[3]!=0){ fclose(f); return S2R_ERR_CORRUPT; }
        /* SPEC "Class rules": a signed class must have flags bit 0 set and an
         * unsigned class must not. Only the Go port checked this. */
        if(((size<0)?1:0) != ((flags & S2R_FLAG_SIGNED)?1:0)){ fclose(f); return S2R_ERR_CORRUPT; }
    }
    uint64_t count; if(!s2r__rd_le64(f,&count)){ fclose(f); return S2R_ERR_IO; }
    size_t abs=s2r_abs_size(size), eb=abs>>3;
    if(!(abs==8||abs==16||abs==32||abs==64)){ fclose(f); return S2R_ERR_CORRUPT; }
    if(count>(uint64_t)SIZE_MAX){ fclose(f); return S2R_ERR_CORRUPT; }
    if(eb && count>SIZE_MAX/eb){ fclose(f); return S2R_ERR_CORRUPT; }
    if(!s2r_pool_init(p,size,(size_t)count)){ fclose(f); return S2R_ERR_OOM; }
    p->flags=flags;
    size_t bytes=(size_t)count*eb;
    if(bytes){ if(fread(p->data,bytes,1,f)!=1){ s2r_pool_free(p); fclose(f); return S2R_ERR_IO; } }
    if(is_v33){
        uint32_t crc_disk; if(!s2r__rd_le32(f,&crc_disk)){ s2r_pool_free(p); fclose(f); return S2R_ERR_IO; }
        uint32_t crc_calc=bytes? s2r_crc32(p->data,bytes,0):0;
        if(crc_calc!=crc_disk){ s2r_pool_free(p); fclose(f); return S2R_ERR_CORRUPT; }
        /* SPEC rule 6: exact length. Trailing bytes were silently tolerated. */
        if(fgetc(f)!=EOF){ s2r_pool_free(p); fclose(f); return S2R_ERR_CORRUPT; }
    }
    p->count=(size_t)count; fclose(f);
#if !S2R_LITTLE_ENDIAN
    if(eb>1) s2r_swap_payload(p->data,eb,p->count);
#endif
    return S2R_OK;
}
#endif /* S2R_HAS_STDIO */

/* ============================================================================
 * ZERO-COPY MMAP (NEW in v3.3)  -  access datasets without copying to RAM
 * ----------------------------------------------------------------------------
 * Ideal for the edge: opens large files from storage, paged in on
 * demand by the kernel. The resulting pool is READ-ONLY and points INSIDE the
 * mapping. Release it with s2r_map_close (do not use s2r_pool_free).
 * On a big-endian host, multibyte uses s2r_load_portable (copies + swaps).
 * ============================================================================ */
#if S2R_HAS_MMAP

typedef struct { S2RPool pool; void *_base; size_t _len; int _fd; } S2RMap;

static inline S2RError s2r_map_open(S2RMap *m, const char *filename, int verify_crc){
    if(!m||!filename) return S2R_ERR_NULL;
    m->_base=NULL; m->_len=0; m->_fd=-1; memset(&m->pool,0,sizeof m->pool);
    int fd=open(filename,O_RDONLY); if(fd<0) return S2R_ERR_IO;
    struct stat st; if(fstat(fd,&st)!=0){ close(fd); return S2R_ERR_IO; }
    size_t fsz=(size_t)st.st_size;
    if(fsz < 16u+4u){ close(fd); return S2R_ERR_CORRUPT; }
    void *base=mmap(NULL,fsz,PROT_READ,MAP_PRIVATE,fd,0);
    if(base==MAP_FAILED){ close(fd); return S2R_ERR_IO; }
    const uint8_t *b=(const uint8_t*)base;
    uint32_t magic=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
    if(magic!=S2R_MAGIC_V33){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    int8_t size=(int8_t)b[4];
    uint8_t flags=(uint8_t)(b[5] & S2R_FLAGS_FORMAT_MASK);   /* never adopt ownership bits */
    if(b[6]!=(uint8_t)S2R_FMT_VERSION){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    if(b[7]!=0){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    /* SPEC "Class rules": signed class <=> flags bit 0 set. */
    if(((size<0)?1:0) != ((flags & S2R_FLAG_SIGNED)?1:0)){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    uint64_t count=0; for(int i=0;i<8;i++) count|=(uint64_t)b[8+i]<<(8*i);
    size_t abs=s2r_abs_size(size), eb=abs>>3;
    if(!(abs==8||abs==16||abs==32||abs==64)){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    if(count>(uint64_t)SIZE_MAX){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    if(eb && count>SIZE_MAX/eb){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    size_t bytes=(size_t)count*eb;
    /* SPEC rule 6: the file length must EQUAL 16 + payload + 4, not merely cover it. */
    if((uint64_t)16u+bytes+4u != (uint64_t)fsz){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    const uint8_t *payload=b+16;
    if(verify_crc){
        uint32_t cd=(uint32_t)payload[bytes]|((uint32_t)payload[bytes+1]<<8)|((uint32_t)payload[bytes+2]<<16)|((uint32_t)payload[bytes+3]<<24);
        uint32_t cc=bytes? s2r_crc32(payload,bytes,0):0;
        if(cd!=cc){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    }
#if !S2R_LITTLE_ENDIAN
    /* Big-endian host: the file is canonical LE. Converts in place via COW
     * (MAP_PRIVATE) without changing the on-disk file. It is no longer zero-copy
     * nas paginas tocadas, mas mantem portabilidade. CRC ja validado acima
     * over the canonical LE bytes (correct on any host). */
    if(eb>1){
        if(mprotect(base,fsz,PROT_READ|PROT_WRITE)!=0){ munmap(base,fsz); close(fd); return S2R_ERR_IO; }
        s2r_swap_payload((void*)(uintptr_t)payload, eb, (size_t)count);
    }
#endif
    m->_base=base; m->_len=fsz; m->_fd=fd;
    m->pool.data=(uint8_t*)(uintptr_t)payload;
    m->pool.size=size;
    /* READONLY|EXTERNAL are correct HERE: the buffer really lives inside the
     * mapping and is not owned by the pool. They are set by us, not read from disk. */
    m->pool.flags=(uint8_t)(flags|S2R_FLAG_READONLY|S2R_FLAG_EXTERNAL);
    m->pool.count=(size_t)count; m->pool.capacity=(size_t)count;
    m->pool.byte_cap=bytes; m->pool._reserved=0;
    return S2R_OK;
}

static inline void s2r_map_close(S2RMap *m){
    if(!m) return;
    if(m->_base && m->_base!=MAP_FAILED) munmap(m->_base,m->_len);
    if(m->_fd>=0) close(m->_fd);
    m->_base=NULL; m->_len=0; m->_fd=-1; memset(&m->pool,0,sizeof m->pool);
}
#endif /* S2R_HAS_MMAP */

/* ============================================================================
 * SIMD SUM WITH RUNTIME DISPATCH (NEW in v3.3)
 * ----------------------------------------------------------------------------
 * A single binary: uses AVX2 if the CPU supports it (the target attribute allows
 * compiling without a global -mavx2), otherwise scalar. On non-x86 ISAs, scalar.
 * ============================================================================ */
#if S2R_X86_SIMD

__attribute__((target("avx2")))
static uint64_t s2r__sum_u8_avx2(const uint8_t *a, size_t n){
    const __m256i z=_mm256_setzero_si256(); __m256i acc=z; size_t i=0;
    for(; i+32<=n; i+=32) acc=_mm256_add_epi64(acc,_mm256_sad_epu8(_mm256_loadu_si256((const __m256i*)(a+i)),z));
    uint64_t s=(uint64_t)_mm256_extract_epi64(acc,0)+_mm256_extract_epi64(acc,1)+_mm256_extract_epi64(acc,2)+_mm256_extract_epi64(acc,3);
    for(; i<n; i++) s+=a[i];
    return s;
}
__attribute__((target("avx2")))
static uint64_t s2r__sum_u16_avx2(const uint16_t *a, size_t n){
    const __m256i z=_mm256_setzero_si256(); uint64_t total=0; size_t i=0;
    while(i+16<=n){
        __m256i acc32=z; size_t end=i+(size_t)16*16384; if(end>n) end=n-(n%16);
        for(; i+16<=end; i+=16){
            __m256i v=_mm256_loadu_si256((const __m256i*)(a+i));
            acc32=_mm256_add_epi32(acc32,_mm256_add_epi32(_mm256_unpacklo_epi16(v,z),_mm256_unpackhi_epi16(v,z)));
        }
        uint32_t tmp[8]; _mm256_storeu_si256((__m256i*)tmp,acc32);
        for(int k=0;k<8;k++) total+=tmp[k];
    }
    for(; i<n; i++) total+=a[i];
    return total;
}

/* AVX-512 (avx512bw). u8 sum via _mm512_sad_epu8 (64 bytes/iter): measured on a
 * Xeon at ~1.17x (cache-resident) to ~1.30x (memory-bound) over AVX2, ~12-14x over
 * scalar. NOTE: a u16 AVX-512 kernel was benchmarked and came out SLOWER than the
 * AVX2 u16 path (8 vs 16 elements/iter), so u16 stays on AVX2 by measurement. */
__attribute__((target("avx512bw")))
static uint64_t s2r__sum_u8_avx512(const uint8_t *a, size_t n){
    const __m512i z=_mm512_setzero_si512(); __m512i acc=z; size_t i=0;
    for(; i+64<=n; i+=64) acc=_mm512_add_epi64(acc,_mm512_sad_epu8(_mm512_loadu_si512((const void*)(a+i)),z));
    uint64_t s=(uint64_t)_mm512_reduce_add_epi64(acc);
    for(; i<n; i++) s+=a[i];
    return s;
}
static inline int s2r_has_avx2(void){ return __builtin_cpu_supports("avx2"); }
static inline int s2r_has_avx512bw(void){ return __builtin_cpu_supports("avx512bw"); }
#else
static inline int s2r_has_avx512bw(void){ return 0; }
static inline int s2r_has_avx2(void){ return 0; }
#endif /* S2R_X86_SIMD */

/* ---- NEON (ARM). Written to the standard intrinsics; NOT compiled/
 *      tested here for lack of an ARM toolchain. On aarch64 NEON is mandatory,
 *      so no runtime check is needed. The scalar fallback guarantees
 *      correctness if NEON is disabled (S2R_NO_SIMD). ---- */
#if S2R_ARM_NEON
static uint64_t s2r__sum_u8_neon(const uint8_t *a, size_t n){
    uint64x2_t acc = vdupq_n_u64(0); size_t i=0;
    for(; i+16<=n; i+=16){
        uint8x16_t v = vld1q_u8(a+i);
        uint16x8_t s16 = vpaddlq_u8(v);    /* 16xu8  -> 8xu16  */
        uint32x4_t s32 = vpaddlq_u16(s16); /* 8xu16  -> 4xu32  */
        uint64x2_t s64 = vpaddlq_u32(s32); /* 4xu32  -> 2xu64  */
        acc = vaddq_u64(acc, s64);
    }
    uint64_t s = vgetq_lane_u64(acc,0) + vgetq_lane_u64(acc,1);
    for(; i<n; i++) s += a[i];
    return s;
}
static uint64_t s2r__sum_u16_neon(const uint16_t *a, size_t n){
    uint64x2_t acc = vdupq_n_u64(0); size_t i=0;
    for(; i+8<=n; i+=8){
        uint16x8_t v = vld1q_u16(a+i);
        uint32x4_t s32 = vpaddlq_u16(v);
        uint64x2_t s64 = vpaddlq_u32(s32);
        acc = vaddq_u64(acc, s64);
    }
    uint64_t s = vgetq_lane_u64(acc,0) + vgetq_lane_u64(acc,1);
    for(; i<n; i++) s += a[i];
    return s;
}
#endif /* S2R_ARM_NEON */

/* ---- RISC-V Vector (RVV 1.0). EXPERIMENTAL: written to the RVV 1.0 C
 *      intrinsics, but NOT compiled on a RISC-V toolchain in this environment
 *      (none available). The strip-mining + reduction LOGIC is validated on x86
 *      via tests/rvv_emu (a minimal, auditable shim, like the NEON one). The
 *      intrinsic signatures must be confirmed on a real rv64gcv build (gcc/clang
 *      + QEMU or hardware) before this path is promoted from "experimental".
 *      The scalar fallback always guarantees correctness when RVV is off.
 *
 *      Idiom: zero-extend each element to u64 and accumulate in a u64 vector
 *      (overflow-safe for any count, no strip-mining), then a single reduction.
 *      Vector-length agnostic: works for any VLEN.
 *
 *      LMUL note. The accumulator is u64m8 fed from u8m1 / u16m2, so VLMAX
 *      matches at VLEN/8 elements per iteration. The earlier version used the
 *      fractional pairs u8mf8 / u16mf4 into u64m1, which also type-checks but
 *      consumes only VLEN/64 elements per iteration - 8x narrower, i.e. 2 bytes
 *      per iteration on a VLEN=128 core. Same correctness argument, 8x the work
 *      per loop; the cost is register pressure (16 of 32 vector registers). ---- */
#if S2R_RISCV_RVV
static uint64_t s2r__sum_u8_rvv(const uint8_t *a, size_t n){
    size_t vlmax = __riscv_vsetvlmax_e64m8();
    vuint64m8_t acc = __riscv_vmv_v_x_u64m8(0, vlmax);
    size_t i=0;
    while(i<n){
        size_t vl = __riscv_vsetvl_e8m1(n - i);                /* VLEN/8 elements */
        vuint8m1_t  v8  = __riscv_vle8_v_u8m1(a + i, vl);
        vuint64m8_t v64 = __riscv_vzext_vf8_u64m8(v8, vl);     /* u8 -> u64 */
        acc = __riscv_vadd_vv_u64m8_tu(acc, acc, v64, vl);     /* tail-undisturbed */
        i += vl;
    }
    vuint64m1_t zero = __riscv_vmv_v_x_u64m1(0, __riscv_vsetvlmax_e64m1());
    vuint64m1_t red  = __riscv_vredsum_vs_u64m8_u64m1(acc, zero, vlmax);
    return __riscv_vmv_x_s_u64m1_u64(red);
}
static uint64_t s2r__sum_u16_rvv(const uint16_t *a, size_t n){
    size_t vlmax = __riscv_vsetvlmax_e64m8();
    vuint64m8_t acc = __riscv_vmv_v_x_u64m8(0, vlmax);
    size_t i=0;
    while(i<n){
        size_t vl = __riscv_vsetvl_e16m2(n - i);               /* VLEN/8 elements */
        vuint16m2_t v16 = __riscv_vle16_v_u16m2(a + i, vl);
        vuint64m8_t v64 = __riscv_vzext_vf4_u64m8(v16, vl);    /* u16 -> u64 */
        acc = __riscv_vadd_vv_u64m8_tu(acc, acc, v64, vl);
        i += vl;
    }
    vuint64m1_t zero = __riscv_vmv_v_x_u64m1(0, __riscv_vsetvlmax_e64m1());
    vuint64m1_t red  = __riscv_vredsum_vs_u64m8_u64m1(acc, zero, vlmax);
    return __riscv_vmv_x_s_u64m1_u64(red);
}
#endif /* S2R_RISCV_RVV */

/* ---- ARM SVE2. EXPERIMENTAL: written to the SVE ACLE intrinsics; NOT compiled on
 *      an ARM/SVE toolchain here. The vector-length-agnostic LOGIC is validated on
 *      x86 via tests/sve2_emu.
 *
 *      Idiom: a full-width contiguous load plus UDOT against a vector of ones.
 *      UDOT is SVE's answer to vpsadbw - it reduces 4 narrow elements into one
 *      wide lane per instruction, which is what lets the kernel consume an entire
 *      vector of BYTES per iteration.
 *
 *      This replaces the original idiom (an extending load of one byte per 64-bit
 *      lane, mirroring the RVV kernel). That version was correct but consumed only
 *      svcntd() = VL/64 bytes per iteration: 2 bytes at VL=128, 4 at VL=256, 8 at
 *      VL=512 - against NEON's fixed 16 bytes per iteration. It was therefore
 *      SLOWER than NEON on every shipping SVE machine, and only reached parity at
 *      VL=1024. The header comment claimed "at 128-bit SVE the width equals NEON",
 *      which described the intent rather than the code. With UDOT the kernel
 *      consumes svcntb() = VL/8 bytes per iteration: parity with NEON at VL=128
 *      and a genuine 2x/4x at VL=256/512.
 *
 *      Still pending real SVE hardware before being promoted from experimental;
 *      see the dispatch guard in s2r_sum_fast for how NEON and SVE are chosen. ---- */
#if S2R_ARM_SVE2
/* Strip-mine bound for the u8 kernel: each u32 lane gains at most 4*255 = 1020
 * per iteration, so 2^22 iterations reach 4278190080 < UINT32_MAX. */
#ifndef S2R_SVE_U8_CHUNK
#define S2R_SVE_U8_CHUNK 4194304u
#endif

static uint64_t s2r__sum_u8_sve(const uint8_t *a, size_t n){
    const uint64_t vlb = svcntb();              /* BYTES per vector = VL/8 */
    const svuint8_t ones = svdup_n_u8(1);
    uint64_t total = 0, i = 0;
    while(i < n){
        uint64_t remain = n - i;
        uint64_t iters  = remain / vlb + ((remain % vlb) ? 1u : 0u);
        if(iters > (uint64_t)S2R_SVE_U8_CHUNK) iters = (uint64_t)S2R_SVE_U8_CHUNK;
        uint64_t end = i + iters * vlb; if(end > n) end = n;
        svuint32_t acc = svdup_n_u32(0);
        for(; i < end; i += vlb){
            svbool_t pg = svwhilelt_b8(i, end);
            /* svld1 is ZEROING: inactive lanes read as 0 and contribute nothing
             * to the dot product, so the tail needs no scalar epilogue. */
            svuint8_t v = svld1_u8(pg, a + i);
            acc = svdot_u32(acc, v, ones);      /* UDOT: 4 bytes per u32 lane */
        }
        total += svaddv_u32(svptrue_b32(), acc); /* UADDV widens to a u64 scalar */
    }
    return total;
}

static uint64_t s2r__sum_u16_sve(const uint16_t *a, size_t n){
    const uint64_t vlh = svcnth();              /* u16 ELEMENTS per vector = VL/16 */
    const svuint16_t ones = svdup_n_u16(1);
    svuint64_t acc = svdup_n_u64(0);
    uint64_t i = 0;
    for(; i < n; i += vlh){
        svbool_t pg = svwhilelt_b16(i, (uint64_t)n);
        svuint16_t v = svld1_u16(pg, a + i);
        /* UDOT 16->64: each u64 lane gains at most 4*65535 = 262140 per iteration,
         * so overflow needs ~7e13 iterations. No strip-mining required. */
        acc = svdot_u64(acc, v, ones);
    }
    return svaddv_u64(svptrue_b64(), acc);
}
#endif /* S2R_ARM_SVE2 */

/* accelerated sum, same result as s2r_sum(). */
static inline uint64_t s2r_sum_fast(const S2RPool *p){
    if(!p||p->count==0) return 0;
#if S2R_X86_SIMD
    if(s2r_has_avx512bw()){
        switch(s2r_abs_size(p->size)){
            case 8:  return s2r__sum_u8_avx512((const uint8_t*)p->data,p->count);
            default: break;   /* u16 stays on AVX2 (measured faster) */
        }
    }
    if(s2r_has_avx2()){
        switch(s2r_abs_size(p->size)){
            case 8:  return s2r__sum_u8_avx2((const uint8_t*)p->data,p->count);
            case 16: return s2r__sum_u16_avx2((const uint16_t*)p->data,p->count);
            default: break;
        }
    }
#endif
#if S2R_ARM_SVE2
    /* SVE2 must be tested BEFORE NEON. On any real SVE target both feature macros
     * are defined (SVE implies NEON on AArch64), so with the NEON block first the
     * SVE kernels were unreachable dead code - only the x86 emulation test, which
     * forces SVE2 without NEON, ever ran them.
     *
     * The runtime guard is deliberate: the SVE kernels consume svcntb() = VL/8
     * bytes per iteration and NEON consumes a fixed 16, so they are exactly at
     * parity when VL = 128. Taking SVE there would swap a measured path for an
     * unmeasured one for no width gain, so we only prefer SVE when the vector is
     * genuinely wider. svcntb() is a single instruction. */
    if(svcntb() > 16){
        switch(s2r_abs_size(p->size)){
            case 8:  return s2r__sum_u8_sve((const uint8_t*)p->data,p->count);
            case 16: return s2r__sum_u16_sve((const uint16_t*)p->data,p->count);
            default: break;
        }
    }
#endif
#if S2R_ARM_NEON
    switch(s2r_abs_size(p->size)){
        case 8:  return s2r__sum_u8_neon((const uint8_t*)p->data,p->count);
        case 16: return s2r__sum_u16_neon((const uint16_t*)p->data,p->count);
        default: break;
    }
#endif
#if S2R_RISCV_RVV
    switch(s2r_abs_size(p->size)){
        case 8:  return s2r__sum_u8_rvv((const uint8_t*)p->data,p->count);
        case 16: return s2r__sum_u16_rvv((const uint16_t*)p->data,p->count);
        default: break;
    }
#endif
    return s2r_sum(p);
}

/* ============================================================================
 * SIMD PREDICATE SCANS (NEW in v3.4.0)
 * ----------------------------------------------------------------------------
 * Until now s2r_sum_fast was the ONLY operation with a runtime SIMD dispatch.
 * The whole filter family - count_gt / count_lt / count_eq / count_range /
 * sum_if, signed and unsigned - ran through typed scalar loops (and the signed
 * variants, plus unsigned count_range and sum_if, through the s2r_get accessor,
 * so the width switch sat INSIDE the loop). Measured on the same u8 bytes of one
 * pool: sum_fast reached 24218 Mval/s while every filter sat near 4000 - the
 * library was leaving 6x on the table for scans, which is the operation a
 * compact integer store exists to make fast.
 *
 * ONE KERNEL SERVES ALL OF THEM. The whole family reduces to a range test:
 *
 *      count_gt(t)        = count_range(t+1, MAX)
 *      count_lt(t)        = count_range(MIN, t-1)
 *      count_eq(v)        = count_range(v, v)
 *      count_range(a,b)   = itself
 *
 * and a range test on a w-bit lane is a single WRAPPING subtract plus one
 * unsigned compare, because for lo <= hi with span = hi - lo < 2^w:
 *
 *      v in [lo,hi]   <=>   (v - lo) mod 2^w  <=  span
 *
 * The same identity serves SIGNED pools without a second kernel: two's-complement
 * subtraction is bit-identical to unsigned subtraction, so reducing lo and span
 * modulo 2^w and running the unsigned kernel on the raw stored bytes gives the
 * signed answer. This is the same reasoning the lazy-carry commit relies on.
 *
 * The unsigned compare itself avoids the missing unsigned SIMD compare via
 * saturating subtract: subs_epu(x, span) == 0 exactly when x <= span.
 *
 * sum_if reuses the frame of reference: SUM over matches of v equals
 * SUM(v - lo) + lo*count, and (v - lo) for a match lies in [0, span], i.e. it is
 * unsigned and narrow - so vpsadbw sums it directly, even for a signed pool.
 *
 * Scope, stated honestly: AVX2 kernels for the 8- and 16-bit classes, which is
 * where the compact classes live and where sum_fast also puts its effort. 32/64
 * bit and non-x86 targets fall back to typed hoisted loops that the compiler
 * auto-vectorises; an explicit NEON path is deliberately NOT claimed here because
 * it could not be measured in this environment. Every path returns exactly what
 * the scalar function returns.
 * ============================================================================ */

/* ---- raw range kernels: count elements whose (value - lo) mod 2^w <= span ---- */

#if S2R_X86_SIMD
__attribute__((target("avx2")))
static uint64_t s2r__cntrng_u8_avx2(const uint8_t *a, size_t n, uint8_t lo, uint8_t span){
    const __m256i vlo=_mm256_set1_epi8((char)lo), vsp=_mm256_set1_epi8((char)span);
    const __m256i z=_mm256_setzero_si256();
    uint64_t c=0; size_t i=0;
    for(; i+32<=n; i+=32){
        __m256i v=_mm256_loadu_si256((const __m256i*)(a+i));
        __m256i t=_mm256_sub_epi8(v,vlo);                    /* wraps, as intended */
        __m256i m=_mm256_cmpeq_epi8(_mm256_subs_epu8(t,vsp),z);  /* t<=span */
        c+=(uint64_t)__builtin_popcount((unsigned)_mm256_movemask_epi8(m));
    }
    for(; i<n; i++) c += ((uint8_t)(a[i]-lo) <= span);
    return c;
}
__attribute__((target("avx2")))
static uint64_t s2r__cntrng_u16_avx2(const uint16_t *a, size_t n, uint16_t lo, uint16_t span){
    const __m256i vlo=_mm256_set1_epi16((short)lo), vsp=_mm256_set1_epi16((short)span);
    const __m256i z=_mm256_setzero_si256();
    uint64_t c=0; size_t i=0;
    for(; i+16<=n; i+=16){
        __m256i v=_mm256_loadu_si256((const __m256i*)(a+i));
        __m256i t=_mm256_sub_epi16(v,vlo);
        __m256i m=_mm256_cmpeq_epi16(_mm256_subs_epu16(t,vsp),z);
        /* movemask is per byte: each matching u16 lane sets two bits */
        c+=(uint64_t)(__builtin_popcount((unsigned)_mm256_movemask_epi8(m))/2);
    }
    for(; i<n; i++) c += ((uint16_t)(a[i]-lo) <= span);
    return c;
}
/* returns SUM of (value - lo) over matches, and the match count via *cnt.
 * (value - lo) is in [0,span] for a match, so vpsadbw can sum it directly.
 * Each lane contributes at most 255 and the accumulator is 64-bit, so no
 * strip-mining is needed - the same argument as the u8 sum kernel. */
__attribute__((target("avx2")))
static uint64_t s2r__sumrng_u8_avx2(const uint8_t *a, size_t n, uint8_t lo, uint8_t span,
                                    uint64_t *cnt){
    const __m256i vlo=_mm256_set1_epi8((char)lo), vsp=_mm256_set1_epi8((char)span);
    const __m256i z=_mm256_setzero_si256();
    __m256i acc=z; uint64_t c=0; size_t i=0;
    for(; i+32<=n; i+=32){
        __m256i v=_mm256_loadu_si256((const __m256i*)(a+i));
        __m256i t=_mm256_sub_epi8(v,vlo);
        __m256i m=_mm256_cmpeq_epi8(_mm256_subs_epu8(t,vsp),z);
        c+=(uint64_t)__builtin_popcount((unsigned)_mm256_movemask_epi8(m));
        acc=_mm256_add_epi64(acc,_mm256_sad_epu8(_mm256_and_si256(t,m),z)); /* misses -> 0 */
    }
    uint64_t s=(uint64_t)_mm256_extract_epi64(acc,0)+_mm256_extract_epi64(acc,1)
              +_mm256_extract_epi64(acc,2)+_mm256_extract_epi64(acc,3);
    for(; i<n; i++){ uint8_t d=(uint8_t)(a[i]-lo); if(d<=span){ s+=d; c++; } }
    if(cnt) *cnt=c;
    return s;
}
/* Dedicated `> t` kernels. count_gt could be served by the range kernel above
 * (count_gt(t) = count_range(t+1, MAX)), and it was - but the range form needs a
 * wrapping subtract BEFORE the saturating one, and that extra instruction is
 * measurable on a bandwidth-bound scan: routing count_gt through range cost ~28%
 * on a 23 MB u16 column. `v > t` needs no rebasing at all, because
 * subs_epu(v,t) != 0 already IS the predicate. Specialised by measurement, the
 * same rule that kept u16 on AVX2 in v3.3.7. */
__attribute__((target("avx2")))
static uint64_t s2r__cntgt_u8_avx2(const uint8_t *a, size_t n, uint8_t t){
    const __m256i vt=_mm256_set1_epi8((char)t), z=_mm256_setzero_si256();
    uint64_t c=0; size_t i=0;
    for(; i+32<=n; i+=32){
        __m256i v=_mm256_loadu_si256((const __m256i*)(a+i));
        __m256i nz=_mm256_cmpeq_epi8(_mm256_subs_epu8(v,vt),z);   /* zero <=> v<=t */
        c += 32 - (uint64_t)__builtin_popcount((unsigned)_mm256_movemask_epi8(nz));
    }
    for(; i<n; i++) c += (a[i] > t);
    return c;
}
__attribute__((target("avx2")))
static uint64_t s2r__cntgt_u16_avx2(const uint16_t *a, size_t n, uint16_t t){
    const __m256i vt=_mm256_set1_epi16((short)t), z=_mm256_setzero_si256();
    uint64_t c=0; size_t i=0;
    for(; i+16<=n; i+=16){
        __m256i v=_mm256_loadu_si256((const __m256i*)(a+i));
        __m256i nz=_mm256_cmpeq_epi16(_mm256_subs_epu16(v,vt),z);
        c += 16 - (uint64_t)(__builtin_popcount((unsigned)_mm256_movemask_epi8(nz))/2);
    }
    for(; i<n; i++) c += (a[i] > t);
    return c;
}
#endif /* S2R_X86_SIMD */

/* scalar fallbacks: typed pointers, switch hoisted out of the loop */
static inline uint64_t s2r__cntrng_scalar(const S2RPool *p, uint64_t lo, uint64_t span){
    const size_t n=p->count; uint64_t c=0;
    switch(s2r_abs_size(p->size)){
        case 8: { const uint8_t *a=(const uint8_t*)p->data; const uint8_t l=(uint8_t)lo,s=(uint8_t)span;
                  for(size_t i=0;i<n;i++) c += ((uint8_t)(a[i]-l) <= s); } break;
        case 16:{ const uint16_t*a=(const uint16_t*)p->data; const uint16_t l=(uint16_t)lo,s=(uint16_t)span;
                  for(size_t i=0;i<n;i++) c += ((uint16_t)(a[i]-l) <= s); } break;
        case 32:{ const uint32_t*a=(const uint32_t*)p->data; const uint32_t l=(uint32_t)lo,s=(uint32_t)span;
                  for(size_t i=0;i<n;i++) c += ((uint32_t)(a[i]-l) <= s); } break;
        default:{ const uint64_t*a=(const uint64_t*)p->data;
                  for(size_t i=0;i<n;i++) c += ((uint64_t)(a[i]-lo) <= span); } break;
    }
    return c;
}
static inline uint64_t s2r__sumrng_scalar(const S2RPool *p, uint64_t lo, uint64_t span, uint64_t *cnt){
    const size_t n=p->count; uint64_t s=0,c=0;
    switch(s2r_abs_size(p->size)){
        case 8: { const uint8_t *a=(const uint8_t*)p->data; const uint8_t l=(uint8_t)lo,sp=(uint8_t)span;
                  for(size_t i=0;i<n;i++){ uint8_t d=(uint8_t)(a[i]-l); if(d<=sp){s+=d;c++;} } } break;
        case 16:{ const uint16_t*a=(const uint16_t*)p->data; const uint16_t l=(uint16_t)lo,sp=(uint16_t)span;
                  for(size_t i=0;i<n;i++){ uint16_t d=(uint16_t)(a[i]-l); if(d<=sp){s+=d;c++;} } } break;
        case 32:{ const uint32_t*a=(const uint32_t*)p->data; const uint32_t l=(uint32_t)lo,sp=(uint32_t)span;
                  for(size_t i=0;i<n;i++){ uint32_t d=(uint32_t)(a[i]-l); if(d<=sp){s+=d;c++;} } } break;
        default:{ const uint64_t*a=(const uint64_t*)p->data;
                  for(size_t i=0;i<n;i++){ uint64_t d=a[i]-lo; if(d<=span){s+=d;c++;} } } break;
    }
    if(cnt)*cnt=c;
    return s;
}

/* ---- dispatch ---- */
static inline uint64_t s2r__cntrng_fast(const S2RPool *p, uint64_t lo, uint64_t span){
    if(!p||p->count==0) return 0;
#if S2R_X86_SIMD
    if(s2r_has_avx2()){
        switch(s2r_abs_size(p->size)){
            case 8:  return s2r__cntrng_u8_avx2((const uint8_t*)p->data,p->count,(uint8_t)lo,(uint8_t)span);
            case 16: return s2r__cntrng_u16_avx2((const uint16_t*)p->data,p->count,(uint16_t)lo,(uint16_t)span);
            default: break;
        }
    }
#endif
    return s2r__cntrng_scalar(p,lo,span);
}
static inline uint64_t s2r__sumrng_fast(const S2RPool *p, uint64_t lo, uint64_t span, uint64_t *cnt){
    if(!p||p->count==0){ if(cnt)*cnt=0; return 0; }
#if S2R_X86_SIMD
    if(s2r_has_avx2() && s2r_abs_size(p->size)==8)
        return s2r__sumrng_u8_avx2((const uint8_t*)p->data,p->count,(uint8_t)lo,(uint8_t)span,cnt);
#endif
    return s2r__sumrng_scalar(p,lo,span,cnt);
}

/* Minimum payload BYTES in a straddling block before count_gt prefers a binary
 * search over a SIMD scan. Binary search is O(log n) dependent loads; the scan is
 * O(n) but sequential, prefetchable and vectorised, so for a small block the scan
 * simply wins. Measured on a sawtooth column where every block straddles the
 * threshold and both sides hold the identical payload:
 *
 *     block   binary search vs scan
 *        64          0.67x   <- a LOSS: 8 dependent probes lose to 2 cache lines
 *       256          1.21x
 *      1024          3.26x
 *      4096         10.78x
 *     16384         46.49x
 *     65536        140.30x
 *
 * 512 bytes is 8 cache lines, just past the crossover. Shipping the binary search
 * unconditionally would have been a 33% regression on the default block size of
 * 256 at width 1. Overridable. */
#ifndef S2R_BLK_BSEARCH_MIN_BYTES
#define S2R_BLK_BSEARCH_MIN_BYTES 512u
#endif

/* ---- established order answers a predicate in O(log n) --------------------
 *
 * Same idea the block-wise layer has used since v3.4.0, and the same gate: a
 * handful of DEPENDENT probes lose to a sequential, prefetchable, vectorised scan
 * until the array is big enough. S2R_BLK_BSEARCH_MIN_BYTES is that measured
 * crossover, reused here because the physics is the same.
 *
 * The unsigned helpers must NOT be used on a signed pool: a signed pool sorted by
 * signed order is not monotonic when its bytes are read as unsigned. The signed
 * entry points below have their own pair. */
static inline size_t s2r__lower_u(const S2RPool *p, uint64_t v){   /* first >= v */
    size_t lo=0, hi=p->count;
    while(lo<hi){ size_t m=lo+((hi-lo)>>1); if(s2r_get(p,m)<v) lo=m+1; else hi=m; }
    return lo;
}
static inline size_t s2r__upper_u(const S2RPool *p, uint64_t v){   /* first  > v */
    size_t lo=0, hi=p->count;
    while(lo<hi){ size_t m=lo+((hi-lo)>>1); if(s2r_get(p,m)<=v) lo=m+1; else hi=m; }
    return lo;
}
static inline size_t s2r__lower_s(const S2RPool *p, int64_t v){
    size_t lo=0, hi=p->count;
    while(lo<hi){ size_t m=lo+((hi-lo)>>1); if(s2r_get_signed(p,m)<v) lo=m+1; else hi=m; }
    return lo;
}
static inline size_t s2r__upper_s(const S2RPool *p, int64_t v){
    size_t lo=0, hi=p->count;
    while(lo<hi){ size_t m=lo+((hi-lo)>>1); if(s2r_get_signed(p,m)<=v) lo=m+1; else hi=m; }
    return lo;
}
static inline int s2r__bsearch_worth(const S2RPool *p){
    return (p->flags & S2R_FLAG_SORTED)
        && p->count*(s2r_abs_size(p->size)>>3) >= S2R_BLK_BSEARCH_MIN_BYTES;
}


/* ---- the flat pool's zone map, and the index above it ----------------------
 *
 * v3.4.0 gave every BLOCK a minimum, a span and a sum, and answered from them
 * without reading payload. The flat pool - the type most callers actually hold -
 * carried nothing, so a predicate the data's own bounds could refuse in O(1)
 * still read every byte. `count_gt(220)` on a column that stops at 200 scanned
 * 4 MB to return zero.
 *
 * The discipline is the one the sorted flag already established: the fact is
 * MAINTAINED, never assumed. Every arbitrary write drops it; an append updates
 * it, because a new element can only widen a min/max and only add to a sum.
 *
 * s2r_summarize() pays one pass and records min, max and sum. After that the
 * predicates answer three cases without touching the payload at all:
 *   thr >= max      -> count_gt is 0
 *   thr <  min      -> count_gt is the whole pool
 *   window misses   -> count_range is 0
 * and s2r_sum() becomes O(1). */
static inline int s2r_has_summary(const S2RPool *p){
    return p && (p->flags & S2R_FLAG_SUMMARY) != 0;
}
static inline int s2r_summarize(S2RPool *p){
    if(!p) return 0;
    if(p->flags & S2R_FLAG_SUMMARY) return 1;
    if(!p->count){ p->smin=p->smax=p->ssum=0; p->flags|=S2R_FLAG_SUMMARY; return 1; }
    if(s2r_is_signed(p)){
        int64_t mn=s2r_get_signed(p,0), mx=mn; uint64_t sm=0;
        for(size_t i=0;i<p->count;i++){ int64_t x=s2r_get_signed(p,i);
            if(x<mn) mn=x;
            if(x>mx) mx=x;
            sm+=(uint64_t)x; }
        p->smin=(uint64_t)mn; p->smax=(uint64_t)mx; p->ssum=sm;
    } else {
        uint64_t mn=s2r_get(p,0), mx=mn, sm=0;
        for(size_t i=0;i<p->count;i++){ uint64_t x=s2r_get(p,i);
            if(x<mn) mn=x;
            if(x>mx) mx=x;
            sm+=x; }
        p->smin=mn; p->smax=mx; p->ssum=sm;
    }
    p->flags |= S2R_FLAG_SUMMARY;
    return 1;
}
/* Marks a pool that is being filled by append as summarised from the start, so
 * the whole load maintains it incrementally instead of costing a pass at the end. */
static inline void s2r_summarize_empty(S2RPool *p){
    if(p && p->count==0){ p->smin=p->smax=p->ssum=0; p->flags|=S2R_FLAG_SUMMARY; }
}

/* ---- the cumulative index: a range answered in two reads -------------------
 *
 * A u8 column has 256 possible values and a u16 column 65536, which is a small
 * number next to n. The cumulative count of each value therefore answers ANY
 * range predicate EXACTLY, in two array reads, from a structure that does not
 * grow with the data: 2 KB for u8, 512 KB for u16.
 *
 *     count_range(lo,hi) = cum[hi+1] - cum[lo]
 *
 * This is the same idea as the zone map - precomputed metadata answers without
 * touching the payload - applied to the VALUE domain instead of the position
 * domain. It is not an approximation and not a sketch.
 *
 * Staleness is the whole risk, so it is not left to discipline: the pool carries
 * an epoch that every write bumps, the index records the epoch it was built at,
 * and a query against a changed pool refuses rather than answers. */
typedef struct {
    uint64_t *cum;       /* cum[v] = how many elements are < v; nbins+1 entries */
    size_t    nbins;
    uint32_t  epoch;
    int8_t    size;      /* the class it was built for                          */
    int64_t   base;      /* value of bin 0 (signed pools start at the class min) */
} S2RIndex;

static inline void s2r_index_free(S2RIndex *ix){
    if(!ix) return;
    free(ix->cum);
    memset(ix,0,sizeof *ix);
}
/* Builds the cumulative index. Only for classes narrow enough to be worth it -
 * 8 and 16 bits - because at 32 the table would be 16 GB and the whole point is
 * that the structure does NOT grow with the data. Returns 0 otherwise. */
static inline int s2r_index_build(S2RIndex *ix, const S2RPool *p){
    if(!ix||!p) return 0;
    memset(ix,0,sizeof *ix);
    size_t w=s2r_abs_size(p->size);
    if(w!=8 && w!=16) return 0;
    size_t nb = (size_t)1 << w;
    ix->cum=(uint64_t*)calloc(nb+1,sizeof(uint64_t));
    if(!ix->cum) return 0;
    ix->nbins=nb; ix->epoch=p->epoch; ix->size=p->size;
    ix->base = s2r_is_signed(p) ? s2r_min_signed(p->size) : 0;
    /* histogram into cum[v+1], then prefix-sum in place: cum[v] ends up holding
     * the number of elements strictly BELOW v, which is what a range needs.
     * The typed loops matter: going through s2r_get puts the width switch inside
     * the loop and cost 6x here, which is the same mistake the predicate family
     * was carrying before v3.4.0. */
    {   size_t n=p->count;
        if(w==8){
            const uint8_t *a8=(const uint8_t*)p->data;
            uint64_t h0[256]={0},h1[256]={0},h2[256]={0},h3[256]={0};
            size_t i=0;
            for(; i+4<=n; i+=4){        /* 4 tables break the dependency chain */
                h0[a8[i]]++; h1[a8[i+1]]++; h2[a8[i+2]]++; h3[a8[i+3]]++;
            }
            for(; i<n; i++) h0[a8[i]]++;
            /* a signed i8 column is stored as a byte; rebasing puts it in order */
            for(size_t v2=0; v2<256; v2++){
                uint64_t c=h0[v2]+h1[v2]+h2[v2]+h3[v2];
                size_t k = s2r_is_signed(p) ? (size_t)((int64_t)(int8_t)v2 - ix->base)
                                            : v2;
                ix->cum[k+1]+=c;
            }
        } else {                        /* w == 16 */
            const uint16_t *a16=(const uint16_t*)p->data;
            if(s2r_is_signed(p))
                for(size_t i=0;i<n;i++)
                    ix->cum[(size_t)((int64_t)(int16_t)a16[i] - ix->base)+1]++;
            else
                for(size_t i=0;i<n;i++) ix->cum[(size_t)a16[i]+1]++;
        }
    }
    for(size_t v=0;v<nb;v++) ix->cum[v+1]+=ix->cum[v];
    return 1;
}
static inline int s2r_index_valid(const S2RIndex *ix, const S2RPool *p){
    return ix && p && ix->cum && ix->epoch==p->epoch && ix->size==p->size;
}
static inline size_t s2r_index_bytes(const S2RIndex *ix){
    return ix&&ix->cum ? (ix->nbins+1)*sizeof(uint64_t) : 0;
}
/* count of values in [lo,hi]; *ok is 0 when the index cannot answer (stale, or
 * the window falls outside the class) and the caller must fall back */
static inline size_t s2r_index_count_range(const S2RIndex *ix, const S2RPool *p,
                                           int64_t lo, int64_t hi, int *ok){
    if(ok) *ok=0;
    if(!s2r_index_valid(ix,p) || lo>hi) return 0;
    int64_t cmin=ix->base, cmax=ix->base+(int64_t)ix->nbins-1;
    if(hi<cmin || lo>cmax) { if(ok)*ok=1; return 0; }
    if(lo<cmin) lo=cmin;
    if(hi>cmax) hi=cmax;
    size_t a=(size_t)(lo-ix->base), b=(size_t)(hi-ix->base);
    if(ok) *ok=1;
    return (size_t)(ix->cum[b+1]-ix->cum[a]);
}
static inline size_t s2r_index_count_gt(const S2RIndex *ix, const S2RPool *p,
                                        int64_t thr, int *ok){
    if(!s2r_index_valid(ix,p)){ if(ok)*ok=0; return 0; }
    int64_t cmax=ix->base+(int64_t)ix->nbins-1;
    if(thr>=cmax){ if(ok)*ok=1; return 0; }
    return s2r_index_count_range(ix,p,thr+1,cmax,ok);
}
static inline size_t s2r_index_count_lt(const S2RIndex *ix, const S2RPool *p,
                                        int64_t thr, int *ok){
    if(!s2r_index_valid(ix,p)){ if(ok)*ok=0; return 0; }
    if(thr<=ix->base){ if(ok)*ok=1; return 0; }
    return s2r_index_count_range(ix,p,ix->base,thr-1,ok);
}
static inline size_t s2r_index_count_eq(const S2RIndex *ix, const S2RPool *p,
                                        int64_t v, int *ok){
    return s2r_index_count_range(ix,p,v,v,ok);
}

/* ---- public: unsigned. Same results as the non-fast versions. ---- */

static inline size_t s2r_count_range_fast(const S2RPool *p, uint64_t min_v, uint64_t max_v){
    if(!p||p->count==0||min_v>max_v) return 0;
    /* the data's own bounds refuse or accept before the class bounds get a say */
    if(!s2r_is_signed(p) && (p->flags & S2R_FLAG_SUMMARY)){
        if(max_v < p->smin || min_v > p->smax) return 0;
        if(min_v <= p->smin && max_v >= p->smax) return p->count;
    }
    if(!s2r_is_signed(p) && s2r__bsearch_worth(p))
        return s2r__upper_u(p,max_v) - s2r__lower_u(p,min_v);
    const uint64_t cls_max=s2r_max_value(p->size);
    if(min_v>cls_max) return 0;                 /* nothing in the class can match */
    if(max_v>cls_max) max_v=cls_max;            /* clamp so span stays inside 2^w */
    return (size_t)s2r__cntrng_fast(p,min_v,max_v-min_v);
}
static inline size_t s2r_count_gt_fast(const S2RPool *p, uint64_t thr){
    if(!p||p->count==0) return 0;
    if(!s2r_is_signed(p) && (p->flags & S2R_FLAG_SUMMARY)){
        if(thr >= p->smax) return 0;
        if(thr <  p->smin) return p->count;
    }
    if(!s2r_is_signed(p) && s2r__bsearch_worth(p))
        return p->count - s2r__upper_u(p,thr);
    const uint64_t cls_max=s2r_max_value(p->size);
    if(thr>=cls_max) return 0;
#if S2R_X86_SIMD
    const size_t n=p->count;
    if(s2r_has_avx2()){
        switch(s2r_abs_size(p->size)){
            case 8:  return (size_t)s2r__cntgt_u8_avx2((const uint8_t*)p->data,n,(uint8_t)thr);
            case 16: return (size_t)s2r__cntgt_u16_avx2((const uint16_t*)p->data,n,(uint16_t)thr);
            default: break;
        }
    }
#endif
    return s2r_count_range_fast(p,thr+1,cls_max);
}
static inline size_t s2r_count_lt_fast(const S2RPool *p, uint64_t thr){
    if(!p||p->count==0||thr==0) return 0;
    return s2r_count_range_fast(p,0,thr-1);
}
static inline size_t s2r_count_eq_fast(const S2RPool *p, uint64_t value){
    return s2r_count_range_fast(p,value,value);
}
static inline uint64_t s2r_sum_if_fast(const S2RPool *p, uint64_t min_v, uint64_t max_v){
    if(!p||p->count==0||min_v>max_v) return 0;
    const uint64_t cls_max=s2r_max_value(p->size);
    if(min_v>cls_max) return 0;
    if(max_v>cls_max) max_v=cls_max;
    uint64_t cnt=0;
    uint64_t s=s2r__sumrng_fast(p,min_v,max_v-min_v,&cnt);
    return s + min_v*cnt;                        /* fold the frame of reference back */
}

/* ---- public: signed. Same kernel: two's complement makes the raw bytes work. ---- */

static inline size_t s2r_count_range_signed_fast(const S2RPool *p, int64_t min_v, int64_t max_v){
    if(!p||p->count==0||min_v>max_v||!s2r_is_signed(p)) return 0;
    if(p->flags & S2R_FLAG_SUMMARY){
        if(max_v < (int64_t)p->smin || min_v > (int64_t)p->smax) return 0;
        if(min_v <= (int64_t)p->smin && max_v >= (int64_t)p->smax) return p->count;
    }
    if(s2r__bsearch_worth(p))
        return s2r__upper_s(p,max_v) - s2r__lower_s(p,min_v);
    const int64_t cmin=s2r_min_signed(p->size), cmax=s2r_max_signed(p->size);
    if(min_v>cmax||max_v<cmin) return 0;
    if(min_v<cmin) min_v=cmin;
    if(max_v>cmax) max_v=cmax;
    return (size_t)s2r__cntrng_fast(p,(uint64_t)min_v,(uint64_t)max_v-(uint64_t)min_v);
}
static inline size_t s2r_count_gt_signed_fast(const S2RPool *p, int64_t thr){
    if(!p||p->count==0||!s2r_is_signed(p)) return 0;
    if(p->flags & S2R_FLAG_SUMMARY){
        if(thr >= (int64_t)p->smax) return 0;
        if(thr <  (int64_t)p->smin) return p->count;
    }
    if(s2r__bsearch_worth(p)) return p->count - s2r__upper_s(p,thr);
    const int64_t cmax=s2r_max_signed(p->size);
    if(thr>=cmax) return 0;
    return s2r_count_range_signed_fast(p,thr+1,cmax);
}
static inline size_t s2r_count_lt_signed_fast(const S2RPool *p, int64_t thr){
    if(!p||p->count==0||!s2r_is_signed(p)) return 0;
    const int64_t cmin=s2r_min_signed(p->size);
    if(thr<=cmin) return 0;
    return s2r_count_range_signed_fast(p,cmin,thr-1);
}
static inline size_t s2r_count_eq_signed_fast(const S2RPool *p, int64_t value){
    return s2r_count_range_signed_fast(p,value,value);
}
static inline int64_t s2r_sum_if_signed_fast(const S2RPool *p, int64_t min_v, int64_t max_v){
    if(!p||p->count==0||min_v>max_v||!s2r_is_signed(p)) return 0;
    const int64_t cmin=s2r_min_signed(p->size), cmax=s2r_max_signed(p->size);
    if(min_v>cmax||max_v<cmin) return 0;
    if(min_v<cmin) min_v=cmin;
    if(max_v>cmax) max_v=cmax;
    uint64_t cnt=0;
    uint64_t s=s2r__sumrng_fast(p,(uint64_t)min_v,(uint64_t)max_v-(uint64_t)min_v,&cnt);
    /* SUM(v) = SUM(v-lo) + lo*count, computed in two's complement */
    return (int64_t)(s + (uint64_t)min_v*cnt);
}


/* ============================================================================
 * SELF-ADAPTIVE PUSH (NEW in v3.3)  -  grows the type instead of truncating
 * ============================================================================ */

static inline S2RError s2r_push_adaptive(S2RPool *p, uint64_t v){
    if(!p) return S2R_ERR_NULL;
    if(p->flags & S2R_FLAG_READONLY) return S2R_ERR_INVALID_SIZE;
    if(s2r_is_signed(p)) return S2R_ERR_INVALID_SIZE;  /* use a variante signed */
    if(v > s2r_max_value(p->size)){
        int8_t need=(int8_t)s2r_classify(v);
        if(p->count==0){ p->size=need; s2r__recap_empty(p); }
        else if(!s2r_promote(p,need)) return S2R_ERR_OOM;
    }
    return s2r_push(p,v)? S2R_OK : S2R_ERR_OOM;
}

static inline S2RError s2r_push_signed_adaptive(S2RPool *p, int64_t v){
    if(!p) return S2R_ERR_NULL;
    if(p->flags & S2R_FLAG_READONLY) return S2R_ERR_INVALID_SIZE;
    if(!s2r_is_signed(p)) return S2R_ERR_INVALID_SIZE;
    if(v < s2r_min_signed(p->size) || v > s2r_max_signed(p->size)){
        int8_t need=s2r_classify_signed(v);
        size_t cur=s2r_abs_size(p->size), nd=s2r_abs_size(need);
        if(p->count==0){ p->size=need; s2r__recap_empty(p); }
        else if(nd>cur){ if(!s2r_promote(p,need)) return S2R_ERR_OOM; }
    }
    return s2r_push(p,(uint64_t)v)? S2R_OK : S2R_ERR_OOM;
}

/* ============================================================================
 * LAZY-CARRY ARITHMETIC (NEW in v3.3)  -  promotes once, no wraparound
 * ============================================================================ */

static inline int s2r_ensure_fits(S2RPool *p, uint64_t needed_max){
    if(!p) return 0;
    S2RSize req=s2r_classify(needed_max);
    size_t cur=s2r_abs_size(p->size);
    if((size_t)req<=cur) return (int)cur;
    if(p->count==0){ p->size=(int8_t)req; s2r__recap_empty(p); return (int)req; }
    if(!s2r_promote(p,(int8_t)req)) return 0;
    return (int)req;
}

static inline int s2r_add_scalar_safe(S2RPool *p, uint64_t s){
    if(!p||s2r_is_signed(p)) return 0;
    if(p->count==0) return (int)s2r_abs_size(p->size);
    uint64_t cmax=s2r_max(p);
    if(s > ~0ULL - cmax) return 0;
    if(!s2r_ensure_fits(p,cmax+s)) return 0;
    s2r_add_scalar(p,s);
    return (int)s2r_abs_size(p->size);
}

static inline int s2r_mul_scalar_safe(S2RPool *p, uint64_t s){
    if(!p||s2r_is_signed(p)) return 0;
    if(p->count==0) return (int)s2r_abs_size(p->size);
    uint64_t cmax=s2r_max(p);
    if(cmax!=0 && s > (~0ULL)/cmax) return 0;
    if(!s2r_ensure_fits(p,cmax*s)) return 0;
    s2r_mul_scalar(p,s);
    return (int)s2r_abs_size(p->size);
}

#define S2R_DEFER_MAX_OPS 64
enum { S2R_OP_ADD=0, S2R_OP_MUL=1 };
typedef struct {
    S2RPool *p; uint64_t vmax;
    uint8_t op[S2R_DEFER_MAX_OPS]; uint64_t arg[S2R_DEFER_MAX_OPS];
    int n; int overflow;
} S2RDeferred;

static inline void s2r_defer_begin(S2RDeferred *d, S2RPool *p){
    d->p=p; d->n=0; d->overflow=0; d->vmax=(p&&p->count)?s2r_max(p):0;
}
static inline void s2r_defer_add(S2RDeferred *d, uint64_t s){
    if(d->n>=S2R_DEFER_MAX_OPS || s > ~0ULL - d->vmax){ d->overflow=1; return; }
    d->vmax+=s; d->op[d->n]=S2R_OP_ADD; d->arg[d->n]=s; d->n++;
}
static inline void s2r_defer_mul(S2RDeferred *d, uint64_t s){
    if(d->n>=S2R_DEFER_MAX_OPS){ d->overflow=1; return; }
    if(d->vmax!=0 && s > (~0ULL)/d->vmax){ d->overflow=1; return; }
    d->vmax*=s; d->op[d->n]=S2R_OP_MUL; d->arg[d->n]=s; d->n++;
}
static inline int s2r_defer_commit(S2RDeferred *d){
    if(!d->p || d->overflow) return 0;
    if(d->p->count==0) return 1;
    if(!s2r_ensure_fits(d->p,d->vmax)) return 0;
    for(int i=0;i<d->n;i++){
        if(d->op[i]==S2R_OP_ADD) s2r_add_scalar(d->p,d->arg[i]);
        else                     s2r_mul_scalar(d->p,d->arg[i]);
    }
    return 1;
}

/* ============================================================================
 * SIGNED LAZY-CARRY ARITHMETIC (NEW in v3.3.1)
 * ----------------------------------------------------------------------------
 * For signed, add/mul can move the range in both directions, so we track
 * the result's [min,max] and size the class that fits BOTH.
 * ============================================================================ */

/* overflow-safe int64 (uses gcc/clang builtins; otherwise, manual checks) */
static inline int s2r__add_ovf_i64(int64_t a, int64_t b, int64_t *r){
#if defined(__GNUC__)
    return __builtin_add_overflow(a,b,r);
#else
    if((b>0 && a>INT64_MAX-b) || (b<0 && a<INT64_MIN-b)) return 1;
    *r=a+b; return 0;
#endif
}
static inline int s2r__mul_ovf_i64(int64_t a, int64_t b, int64_t *r){
#if defined(__GNUC__)
    return __builtin_mul_overflow(a,b,r);
#else
    if(a==0||b==0){ *r=0; return 0; }
    if(a>0){ if(b>0){ if(a>INT64_MAX/b) return 1; } else { if(b<INT64_MIN/a) return 1; } }
    else   { if(b>0){ if(a<INT64_MIN/b) return 1; } else { if(a<INT64_MAX/b) return 1; } }
    *r=a*b; return 0;
#endif
}

/* Smallest SIGNED class that fits the interval [lo,hi]. */
static inline int8_t s2r_classify_signed_range(int64_t lo, int64_t hi){
    int8_t a=s2r_classify_signed(lo), b=s2r_classify_signed(hi);
    return (a<b)?a:b;  /* more negative enum = larger class */
}

/* Promotes (signed) to fit [lo,hi] if needed. Returns bits or 0. */
static inline int s2r_ensure_fits_signed(S2RPool *p, int64_t lo, int64_t hi){
    if(!p || !s2r_is_signed(p)) return 0;
    int8_t req=s2r_classify_signed_range(lo,hi);
    size_t cur=s2r_abs_size(p->size), need=s2r_abs_size(req);
    if(need<=cur) return (int)cur;            /* never demotes */
    if(p->count==0){ p->size=req; s2r__recap_empty(p); return (int)need; }
    if(!s2r_promote(p,req)) return 0;
    return (int)need;
}

/* signed scalar add with NO wrap: promotes the class per the new range. */
static inline int s2r_add_scalar_signed_safe(S2RPool *p, int64_t s){
    if(!p || !s2r_is_signed(p)) return 0;
    if(p->count==0) return (int)s2r_abs_size(p->size);
    int64_t lo=s2r_min_signed_val(p), hi=s2r_max_signed_val(p), nlo, nhi;
    if(s2r__add_ovf_i64(lo,s,&nlo)) return 0;
    if(s2r__add_ovf_i64(hi,s,&nhi)) return 0;
    if(!s2r_ensure_fits_signed(p,nlo,nhi)) return 0;
    s2r_add_scalar(p,(uint64_t)s);            /* complemento de 2: bit-identico */
    return (int)s2r_abs_size(p->size);
}

/* signed scalar mul with NO wrap. */
static inline int s2r_mul_scalar_signed_safe(S2RPool *p, int64_t s){
    if(!p || !s2r_is_signed(p)) return 0;
    if(p->count==0) return (int)s2r_abs_size(p->size);
    int64_t lo=s2r_min_signed_val(p), hi=s2r_max_signed_val(p), p1, p2;
    if(s2r__mul_ovf_i64(lo,s,&p1)) return 0;
    if(s2r__mul_ovf_i64(hi,s,&p2)) return 0;
    int64_t nlo=(p1<p2)?p1:p2, nhi=(p1>p2)?p1:p2;
    if(!s2r_ensure_fits_signed(p,nlo,nhi)) return 0;
    s2r_mul_scalar(p,(uint64_t)s);
    return (int)s2r_abs_size(p->size);
}

/* ---- Signed deferred session: accumulates the chain [vmin,vmax], promotes once. ---- */
typedef struct {
    S2RPool *p; int64_t vmin, vmax;
    uint8_t op[S2R_DEFER_MAX_OPS]; int64_t arg[S2R_DEFER_MAX_OPS];
    int n; int overflow;
} S2RDeferredSigned;

static inline void s2r_defer_signed_begin(S2RDeferredSigned *d, S2RPool *p){
    d->p=p; d->n=0; d->overflow=0;
    if(p && p->count){ d->vmin=s2r_min_signed_val(p); d->vmax=s2r_max_signed_val(p); }
    else { d->vmin=0; d->vmax=0; }
}
static inline void s2r_defer_signed_add(S2RDeferredSigned *d, int64_t s){
    int64_t nlo,nhi;
    if(d->n>=S2R_DEFER_MAX_OPS || s2r__add_ovf_i64(d->vmin,s,&nlo) || s2r__add_ovf_i64(d->vmax,s,&nhi)){ d->overflow=1; return; }
    d->vmin=nlo; d->vmax=nhi; d->op[d->n]=S2R_OP_ADD; d->arg[d->n]=s; d->n++;
}
static inline void s2r_defer_signed_mul(S2RDeferredSigned *d, int64_t s){
    int64_t p1,p2;
    if(d->n>=S2R_DEFER_MAX_OPS || s2r__mul_ovf_i64(d->vmin,s,&p1) || s2r__mul_ovf_i64(d->vmax,s,&p2)){ d->overflow=1; return; }
    d->vmin=(p1<p2)?p1:p2; d->vmax=(p1>p2)?p1:p2; d->op[d->n]=S2R_OP_MUL; d->arg[d->n]=s; d->n++;
}
static inline int s2r_defer_signed_commit(S2RDeferredSigned *d){
    if(!d->p || d->overflow) return 0;
    if(d->p->count==0) return 1;
    if(!s2r_ensure_fits_signed(d->p,d->vmin,d->vmax)) return 0;
    for(int i=0;i<d->n;i++){
        if(d->op[i]==S2R_OP_ADD) s2r_add_scalar(d->p,(uint64_t)d->arg[i]);
        else                     s2r_mul_scalar(d->p,(uint64_t)d->arg[i]);
    }
    return 1;
}


/* ===================================================================
 * ANALYTICS MODULE (merged in v3.3.2): bidirectional width
 * (self-healing), S2RTracked and group-by on the compact format.
 * =================================================================== */
#include <string.h>
/* ============================================================================
 * #1  BIDIRECTIONAL WIDTH
 * ============================================================================ */

/* Reclassifies to any class (up or down, signed or unsigned). */
static inline int s2r__reclass(S2RPool *p, int8_t new_size){
    if(!p) return 0;
    if(p->count==0){ p->size=new_size; p->flags = (new_size<0)?(uint8_t)(p->flags|S2R_FLAG_SIGNED):(uint8_t)(p->flags&~S2R_FLAG_SIGNED); s2r__recap_empty(p); return 1; }
    size_t na=s2r_abs_size(new_size), eb=na>>3;
    size_t nb=s2r_align_up(p->count*eb, S2R_ALIGNMENT);
    uint8_t *nd=(uint8_t*)S2R_ALIGNED_ALLOC(nb,S2R_ALIGNMENT); if(!nd) return 0;
    size_t n=p->count; int cur_signed=s2r_is_signed(p);
    for(size_t i=0;i<n;i++){
        if(cur_signed){ int64_t v=s2r_get_signed(p,i);
            switch(na){case 8:((int8_t*)nd)[i]=(int8_t)v;break;case 16:((int16_t*)nd)[i]=(int16_t)v;break;
                       case 32:((int32_t*)nd)[i]=(int32_t)v;break;case 64:((int64_t*)nd)[i]=v;break;} }
        else { uint64_t v=s2r_get(p,i);
            switch(na){case 8:nd[i]=(uint8_t)v;break;case 16:((uint16_t*)nd)[i]=(uint16_t)v;break;
                       case 32:((uint32_t*)nd)[i]=(uint32_t)v;break;case 64:((uint64_t*)nd)[i]=v;break;} }
    }
    if(p->data && !(p->flags&S2R_FLAG_EXTERNAL)) S2R_ALIGNED_FREE(p->data);
    p->data=nd; p->byte_cap=nb; p->capacity=p->count; p->size=new_size;
    p->flags = (new_size<0)?(uint8_t)(p->flags|S2R_FLAG_SIGNED):(uint8_t)(p->flags&~S2R_FLAG_SIGNED);
    return 1;
}

/* O(1) unordered removal: moves the last element into slot i. */
static inline void s2r_remove_swap(S2RPool *p, size_t i){
    if(!p || i>=p->count) return;
    size_t last=p->count-1;
    if(i!=last){
        if(s2r_is_signed(p)) s2r_set_signed(p,i,s2r_get_signed(p,last));
        else                 s2r_set(p,i,s2r_get(p,last));
    }
    p->count--;
}

/* Self-healing: demotes to the SMALLEST class that fits the current data. */
static inline int s2r_fit_class(S2RPool *p){
    if(!p || p->count==0) return 1;
    int8_t target;
    if(s2r_is_signed(p)){ int64_t lo=s2r_min_signed_val(p), hi=s2r_max_signed_val(p);
                          target=s2r_classify_signed_range(lo,hi); }
    else { target=(int8_t)s2r_classify(s2r_max(p)); }
    if(s2r_abs_size(target) >= s2r_abs_size(p->size)) return 1; /* already minimal */
    return s2r__reclass(p, target);
}

/* Healing across the sign boundary.
 *
 * s2r_fit_class demotes the WIDTH but never the SIGN, so a column declared
 * signed that never receives a negative stays twice as wide as it needs: 0..200
 * does not fit i8 (-128..127), so it sits in i16 while u8 would hold it. Schemas
 * and ORMs declare INT signed by default, which makes this shape common.
 *
 * It is a separate entry point on purpose, because dropping the sign is a real
 * change of contract: afterwards s2r_push_adaptive REJECTS a negative instead of
 * promoting for it. Automatic would surprise a caller who intends to push one
 * later, so the caller asks.
 *
 * Returns 1 if the pool now holds the smallest class that fits, sign included. */
static inline int s2r_fit_class_signedness(S2RPool *p){
    if(!p || p->count==0) return 1;
    if(!s2r_is_signed(p)) return s2r_fit_class(p);
    if(s2r_min_signed_val(p) < 0) return s2r_fit_class(p);   /* the sign is earned */
    int8_t target=(int8_t)s2r_classify((uint64_t)s2r_max_signed_val(p));
    if(s2r_abs_size(target) >= s2r_abs_size(p->size) && target>0) return 1;
    return s2r__reclass(p, target);
}

/* ---- Pool with embedded range: min/max in O(1) on push ---- */
typedef struct {
    S2RPool p;
    uint64_t umin, umax;   /* tracked range (unsigned) */
    int      dirty;        /* remocao pode ter invalidado os extremos */
    int      empty;
} S2RTracked;

static inline int s2r_tracked_init(S2RTracked *t, int8_t size, size_t cap){
    t->umin=~0ULL; t->umax=0; t->dirty=0; t->empty=1;
    return s2r_pool_init(&t->p, size, cap);
}
static inline S2RError s2r_tracked_push(S2RTracked *t, uint64_t v){
    S2RError e=s2r_push_adaptive(&t->p, v);   /* grows the class if needed */
    if(e==S2R_OK){ if(v<t->umin)t->umin=v; if(v>t->umax)t->umax=v; t->empty=0; }
    return e;
}
/* O(1) range when clean; rescans only if dirty (after a removal). */
static inline void s2r_tracked_range(S2RTracked *t, uint64_t *mn, uint64_t *mx){
    if(t->dirty && t->p.count){ t->umin=s2r_min(&t->p); t->umax=s2r_max(&t->p); t->dirty=0; }
    *mn = t->p.count? t->umin : 0; *mx = t->p.count? t->umax : 0;
}
static inline void s2r_tracked_free(S2RTracked *t){ s2r_pool_free(&t->p); }

/* ============================================================================
 * #3  GROUP-BY DIRECTLY ON THE COMPACT FORMAT (u8 keys, <=256 groups)
 * ============================================================================ */

/* GROUP BY key COUNT(*) : 256-bin histogram. 4-way to break the
 * read-modify-write dependency chain on repeated values. */
static inline void s2r_histogram_u8(const S2RPool *keys, uint64_t hist[256]){
    memset(hist,0,256*sizeof(uint64_t));
    if(!keys || keys->count==0 || s2r_abs_size(keys->size)!=8) return;
    const uint8_t *a=(const uint8_t*)keys->data; size_t n=keys->count;
    uint64_t h0[256],h1[256],h2[256],h3[256];
    memset(h0,0,sizeof h0); memset(h1,0,sizeof h1); memset(h2,0,sizeof h2); memset(h3,0,sizeof h3);
    size_t i=0;
    for(; i+4<=n; i+=4){ h0[a[i]]++; h1[a[i+1]]++; h2[a[i+2]]++; h3[a[i+3]]++; }
    for(; i<n; i++) h0[a[i]]++;
    for(int k=0;k<256;k++) hist[k]=h0[k]+h1[k]+h2[k]+h3[k];
}

/* GROUP BY key SUM(val) : per-group sum. keys=u8, vals=u32. */
static inline void s2r_group_sum_u8u32(const S2RPool *keys, const S2RPool *vals, uint64_t gsum[256]){
    memset(gsum,0,256*sizeof(uint64_t));
    if(!keys||!vals||keys->count!=vals->count||s2r_abs_size(keys->size)!=8||s2r_abs_size(vals->size)!=32) return;
    const uint8_t  *k=(const uint8_t*)keys->data;
    const uint32_t *v=(const uint32_t*)vals->data;
    size_t n=keys->count;
    for(size_t i=0;i<n;i++) gsum[k[i]] += v[i];
}



/* ============================================================================
 * #4  ANALYTICS V2: sort, unique, nunique e value_counts
 * ============================================================================ */

static inline int s2r__cmp_u8 (const void *a, const void *b){ uint8_t  x=*(const uint8_t*)a,  y=*(const uint8_t*)b;  return (x>y)-(x<y); }
static inline int s2r__cmp_u16(const void *a, const void *b){ uint16_t x=*(const uint16_t*)a, y=*(const uint16_t*)b; return (x>y)-(x<y); }
static inline int s2r__cmp_u32(const void *a, const void *b){ uint32_t x=*(const uint32_t*)a, y=*(const uint32_t*)b; return (x>y)-(x<y); }
static inline int s2r__cmp_u64(const void *a, const void *b){ uint64_t x=*(const uint64_t*)a, y=*(const uint64_t*)b; return (x>y)-(x<y); }
static inline int s2r__cmp_i8 (const void *a, const void *b){ int8_t  x=*(const int8_t*)a,  y=*(const int8_t*)b;  return (x>y)-(x<y); }
static inline int s2r__cmp_i16(const void *a, const void *b){ int16_t x=*(const int16_t*)a, y=*(const int16_t*)b; return (x>y)-(x<y); }
static inline int s2r__cmp_i32(const void *a, const void *b){ int32_t x=*(const int32_t*)a, y=*(const int32_t*)b; return (x>y)-(x<y); }
static inline int s2r__cmp_i64(const void *a, const void *b){ int64_t x=*(const int64_t*)a, y=*(const int64_t*)b; return (x>y)-(x<y); }

/* Sorts the pool in place, preserving the current class. Returns S2R_OK or an error.
 * Note: READONLY/mmap pools cannot be sorted in place. */
static inline S2RError s2r_sort(S2RPool *p){
    if(!p) return S2R_ERR_NULL;
    if(p->flags & S2R_FLAG_READONLY) return S2R_ERR_INVALID_SIZE;
    if(p->count < 2) return S2R_OK;
    switch(p->size){
        case S2R_8:  qsort(p->data, p->count, sizeof(uint8_t),  s2r__cmp_u8);  break;
        case S2R_16: qsort(p->data, p->count, sizeof(uint16_t), s2r__cmp_u16); break;
        case S2R_32: qsort(p->data, p->count, sizeof(uint32_t), s2r__cmp_u32); break;
        case S2R_64: qsort(p->data, p->count, sizeof(uint64_t), s2r__cmp_u64); break;
        case S2R_I8:  qsort(p->data, p->count, sizeof(int8_t),  s2r__cmp_i8);  break;
        case S2R_I16: qsort(p->data, p->count, sizeof(int16_t), s2r__cmp_i16); break;
        case S2R_I32: qsort(p->data, p->count, sizeof(int32_t), s2r__cmp_i32); break;
        case S2R_I64: qsort(p->data, p->count, sizeof(int64_t), s2r__cmp_i64); break;
        default: return S2R_ERR_INVALID_SIZE;
    }
    p->flags |= S2R_FLAG_SORTED;              /* it IS sorted now, by construction */
    return S2R_OK;
}

static inline int s2r_is_sorted(const S2RPool *p){
    if(!p || p->count < 2) return 1;
    if(p->flags & S2R_FLAG_SORTED) return 1;   /* already established, O(1) */
    if(s2r_is_signed(p)){
        for(size_t i=1;i<p->count;i++) if(s2r_get_signed(p,i-1) > s2r_get_signed(p,i)) return 0;
    } else {
        for(size_t i=1;i<p->count;i++) if(s2r_get(p,i-1) > s2r_get(p,i)) return 0;
    }
    return 1;
}

/* ---- established order as a first-class fact ------------------------------
 *
 * The block-wise layer has answered a predicate by binary search since v3.4.0,
 * because a block's order is settled once at build time and the block is then
 * immutable. A flat pool is not: it can be written at any index, so the fact has
 * to be MAINTAINED rather than assumed. Every write clears the flag; only two
 * things set it - s2r_sort(), which established the order itself, and
 * s2r_mark_sorted(), which pays one O(n) pass to verify it.
 *
 * Appending in order keeps it, which is the ingest pattern that matters:
 * timestamps and monotonic ids arrive sorted, so the flag survives a whole load.
 *
 * The flag is NOT serialized: S2R_FLAGS_FORMAT_MASK keeps it out of the file, so
 * a loaded pool starts by knowing nothing, which is the safe direction. */
static inline int s2r_is_known_sorted(const S2RPool *p){
    return p && (p->flags & S2R_FLAG_SORTED) != 0;
}
/* Verifies in O(n) and records the answer. Returns 1 if the pool is sorted. */
static inline int s2r_mark_sorted(S2RPool *p){
    if(!p) return 0;
    if(p->flags & S2R_FLAG_SORTED) return 1;
    if(p->count < 2){ p->flags |= S2R_FLAG_SORTED; return 1; }
    if(s2r_is_signed(p)){
        for(size_t i=1;i<p->count;i++) if(s2r_get_signed(p,i-1) > s2r_get_signed(p,i)) return 0;
    } else {
        for(size_t i=1;i<p->count;i++) if(s2r_get(p,i-1) > s2r_get(p,i)) return 0;
    }
    p->flags |= S2R_FLAG_SORTED;
    return 1;
}

/* Removes adjacent duplicates from an already-sorted pool. Returns the new count.
 * The class may be demoted at the end (self-healing) if the data allows. */
static inline size_t s2r_unique_sorted(S2RPool *p){
    if(!p) return 0;
    if(p->flags & S2R_FLAG_READONLY) return p->count;
    if(p->count < 2) return p->count;
    size_t w=1;
    if(s2r_is_signed(p)){
        int64_t prev=s2r_get_signed(p,0);
        for(size_t r=1;r<p->count;r++){
            int64_t v=s2r_get_signed(p,r);
            if(v!=prev){ s2r_set_signed(p,w++,v); prev=v; }
        }
    } else {
        uint64_t prev=s2r_get(p,0);
        for(size_t r=1;r<p->count;r++){
            uint64_t v=s2r_get(p,r);
            if(v!=prev){ s2r_set(p,w++,v); prev=v; }
        }
    }
    p->count=w;
    (void)s2r_fit_class(p);
    return p->count;
}

/* Counts distinct values without modifying the original pool. For 8/16 bits it uses a bitmap;
 * for 32/64 bits it sorts a temporary copy and counts transitions. */
static inline S2RError s2r_nunique(const S2RPool *p, size_t *out){
    if(!p || !out) return S2R_ERR_NULL;
    *out=0;
    if(p->count==0) return S2R_OK;
    size_t bits=s2r_abs_size(p->size);
    if(bits==8){
        uint8_t seen[256]; memset(seen,0,sizeof seen);
        for(size_t i=0;i<p->count;i++){ uint64_t v=s2r_is_signed(p)?(uint64_t)(uint8_t)(int8_t)s2r_get_signed(p,i):s2r_get(p,i); if(!seen[v]){seen[v]=1; (*out)++;} }
        return S2R_OK;
    }
    if(bits==16){
        uint8_t *seen=(uint8_t*)calloc(65536,1);
        if(!seen) return S2R_ERR_OOM;
        for(size_t i=0;i<p->count;i++){
            uint16_t v=s2r_is_signed(p)?(uint16_t)(int16_t)s2r_get_signed(p,i):(uint16_t)s2r_get(p,i);
            if(!seen[v]){seen[v]=1; (*out)++;}
        }
        free(seen); return S2R_OK;
    }
    S2RPool tmp; if(!s2r_copy(&tmp,p)) return S2R_ERR_OOM;
    S2RError e=s2r_sort(&tmp); if(e!=S2R_OK){ s2r_pool_free(&tmp); return e; }
    *out=tmp.count?1:0;
    if(s2r_is_signed(&tmp)){
        for(size_t i=1;i<tmp.count;i++) if(s2r_get_signed(&tmp,i)!=s2r_get_signed(&tmp,i-1)) (*out)++;
    } else {
        for(size_t i=1;i<tmp.count;i++) if(s2r_get(&tmp,i)!=s2r_get(&tmp,i-1)) (*out)++;
    }
    s2r_pool_free(&tmp); return S2R_OK;
}

/* Value counts for u8/i8 pools. The index is the raw stored byte;
 * for i8, value -1 falls in bin 255, -128 in bin 128, etc. */
static inline S2RError s2r_value_counts_u8(const S2RPool *p, uint64_t counts[256]){
    if(!p || !counts) return S2R_ERR_NULL;
    memset(counts,0,256*sizeof(uint64_t));
    if(s2r_abs_size(p->size)!=8) return S2R_ERR_INVALID_SIZE;
    for(size_t i=0;i<p->count;i++) counts[((const uint8_t*)p->data)[i]]++;
    return S2R_OK;
}

/* ===================================================================
 * BLOCK-WISE WIDTH MODULE (PFOR)
 *   v3.3.3  per-block class
 *   v3.4.0  frame of reference; constant blocks store no payload
 *   v3.4.0  zone statistics, sorted blocks, portable serialization
 *
 * Partitions the array into fixed-size blocks. Each block stores its values
 * RELATIVE to its own minimum, in the smallest class that fits its own range, so
 * an outlier inflates only its own block and a baseline far from zero costs
 * nothing. A block whose values are all equal stores no payload at all.
 *
 * v3.4.0 adds the information the data already carries and the layout was
 * throwing away:
 *
 *   ZONE STATISTICS. Each block keeps its true delta span and its sum. The span
 *     tightens the predicate: the old skip test bounded a block's maximum by its
 *     WIDTH (a block of 100..150 was treated as reaching 355, because one byte
 *     can), so blocks that could have been skipped were scanned. The sum makes
 *     s2r_blocked_sum an O(nblocks) walk over metadata that never touches the
 *     payload.
 *
 *   SORTED BLOCKS. A non-decreasing block answers count_gt by BINARY SEARCH,
 *     O(log n) instead of O(n). Order is information the data already has;
 *     the classical layout discards it.
 *
 *   SERIALIZATION (ROADMAP: "block-wise .s2r serialization"). fmt = 2, canonical
 *     little-endian, CRC32 over metadata AND payload. The class byte is 0, so a
 *     v3.3 reader rejects the file on both the class and the fmt check - which is
 *     correct: a blocked file is not a flat pool.
 *
 * Every array of bookkeeping is stored in the smallest class that fits its own
 * range, chosen by the library's own classifier. Metadata that said "use int64 to
 * be safe" would contradict the library it belongs to.
 * =================================================================== */
#ifndef S2R_BLOCK_DEFAULT
#define S2R_BLOCK_DEFAULT 256
#endif

#define S2R_BLK_SORTED 0x01
#define S2R_BLK_FMT    2u     /* serialized blocked column, no per-block stride  */
#define S2R_BLK_FMT_S  3u     /* same, plus the per-block stride array            */

/* ---- affine factoring: the frame of reference gains a scale ----------------
 *
 * v3.4.0 stored a block relative to its own minimum: v = base + delta. That
 * removes an OFFSET the data does not need. It does not remove a SCALE.
 *
 *     column { 500, 1500, 2500, ... 11500 }   base 500, span 11000 -> 14 bits
 *
 * but every value is base + 1000*i with i in 0..11, so the information is 4 bits
 * of index wearing a 14-bit coat. The common step is the gcd of the deltas, one
 * pass to find, and dividing it out is exact by construction.
 *
 *     v = base + stride * i        stride = gcd over the block of (v - base)
 *
 * This is NOT a dictionary. There is no lookup table and no per-value
 * indirection: the map is a closed-form affine function, so every operation the
 * library offers rewrites in the index domain and reads the stored bytes as the
 * native integers they still are.
 *
 *     v > t          <=>   i > (t - base) / stride        (integer division)
 *     SUM(v)         =     len*base + stride*SUM(i)
 *     max(v)         =     base + stride*span_i
 *
 * stride == 1 is exactly v3.4.0, the same way base == 0 was exactly v3.3. A
 * column with no common step pays one gcd pass at build and nothing after.
 *
 * Where it pays, measured on 12M elements: a column of 12 distinct values spread
 * over 500..11500 goes from 22.89 MB / 1.032 ms to 11.44 MB / 0.489 ms - 2.00x
 * the space and 2.11x the predicate. Where it does not (arbitrary values, gcd 1)
 * the classification is byte-for-byte what v3.4.0 produced.
 *
 * Honest scope: this closes part of the gap against dictionary encoding on
 * stride-bearing data (fixed sampling intervals, fixed-point money, quantization
 * steps). It does not close it in general - 12 distinct values still need only
 * log2(12) = 3.58 bits, and the smallest native class is 8. */


typedef struct {
    uint8_t *data;      /* contiguous payload: the deltas of every block   */
    size_t   bytes;     /* payload bytes used (includes alignment padding) */
    uint8_t *bclass;    /* DELTA width of each block, in bits: 0/8/16/32/64 */
    uint8_t *bflags;    /* per block: bit 0 = non-decreasing                */
    void    *boff;      /* byte offset of each block, class ocls (unsigned) */
    void    *bbase;     /* frame of reference of each block, class bcls     */
    void    *bspan;     /* max INDEX of each block, class pcls              */
    void    *bsum;      /* sum of each block, class scls                    */
    void    *bstride;   /* common step of each block, class tcls; NULL if all 1 */
    int8_t   bcls, ocls, pcls, scls, tcls;
    int      has_stride;/* 0 = every stride is 1 (v3.4.0 shape, byte-identical) */
    size_t   nblocks;
    size_t   count;     /* total number of elements                        */
    size_t   block;     /* block size (elements)                           */
    int      is_signed; /* 0 = unsigned, 1 = signed                        */
} S2RBlocked;

/* ---- typed metadata access: the class is per ARRAY, so callers hoist ---- */
static inline size_t s2r__md_bytes(int8_t cls){ return s2r_abs_size(cls)>>3; }

static inline uint64_t s2r__md_ru(const void *a, int8_t cls, size_t i){
    switch(s2r_abs_size(cls)){
        case 8:  return ((const uint8_t *)a)[i];
        case 16: return ((const uint16_t*)a)[i];
        case 32: return ((const uint32_t*)a)[i];
        default: return ((const uint64_t*)a)[i];
    }
}
static inline void s2r__md_wu(void *a, int8_t cls, size_t i, uint64_t v){
    switch(s2r_abs_size(cls)){
        case 8:  ((uint8_t *)a)[i]=(uint8_t )v; break;
        case 16: ((uint16_t*)a)[i]=(uint16_t)v; break;
        case 32: ((uint32_t*)a)[i]=(uint32_t)v; break;
        default: ((uint64_t*)a)[i]=v; break;
    }
}
static inline int64_t s2r__md_rs(const void *a, int8_t cls, size_t i){
    switch(s2r_abs_size(cls)){
        case 8:  return ((const int8_t *)a)[i];
        case 16: return ((const int16_t*)a)[i];
        case 32: return ((const int32_t*)a)[i];
        default: return ((const int64_t*)a)[i];
    }
}
static inline void s2r__md_ws(void *a, int8_t cls, size_t i, int64_t v){
    switch(s2r_abs_size(cls)){
        case 8:  ((int8_t *)a)[i]=(int8_t )v; break;
        case 16: ((int16_t*)a)[i]=(int16_t)v; break;
        case 32: ((int32_t*)a)[i]=(int32_t)v; break;
        default: ((int64_t*)a)[i]=v; break;
    }
}
/* the common step of a set is the gcd of its offsets from the minimum */
static inline uint64_t s2r_gcd64(uint64_t a, uint64_t b){
    while(b){ uint64_t t=a%b; a=b; b=t; }
    return a;
}

/* bases and sums are signed for a signed column, unsigned otherwise */
static inline uint64_t s2r__bb(const S2RBlocked *b, size_t i){
    return b->is_signed ? (uint64_t)s2r__md_rs(b->bbase,b->bcls,i)
                        : s2r__md_ru(b->bbase,b->bcls,i);
}
static inline size_t s2r__bo(const S2RBlocked *b, size_t i){
    return (size_t)s2r__md_ru(b->boff,b->ocls,i);
}
static inline uint64_t s2r__bp(const S2RBlocked *b, size_t i){
    return s2r__md_ru(b->bspan,b->pcls,i);
}
/* stride is 1 unless the column actually has a common step: the branch is on a
 * column-wide flag, not a per-block load, so the v3.4.0 path stays free */
static inline uint64_t s2r__bt(const S2RBlocked *b, size_t i){
    return b->has_stride ? s2r__md_ru(b->bstride,b->tcls,i) : 1u;
}
static inline uint64_t s2r_blocked_stride(const S2RBlocked *b, size_t bi){
    return (b && bi<b->nblocks) ? s2r__bt(b,bi) : 1u;
}
static inline uint64_t s2r__bs(const S2RBlocked *b, size_t i){
    return b->is_signed ? (uint64_t)s2r__md_rs(b->bsum,b->scls,i)
                        : s2r__md_ru(b->bsum,b->scls,i);
}
static inline size_t s2r__blk_len(const S2RBlocked *b, size_t bi){
    size_t start=bi*b->block, rest=b->count-start;
    return rest<b->block ? rest : b->block;
}
static inline uint8_t s2r__delta_bytes(uint64_t span){
    if(span==0)             return 0;
    if(span<=0xFFull)       return 1;
    if(span<=0xFFFFull)     return 2;
    if(span<=0xFFFFFFFFull) return 4;
    return 8;
}

static inline void s2r_blocked_free(S2RBlocked *b){
    if(!b) return;
    if(b->data) S2R_ALIGNED_FREE(b->data);
    free(b->bclass); free(b->bflags);
    free(b->boff); free(b->bbase); free(b->bspan); free(b->bsum); free(b->bstride);
    memset(b,0,sizeof *b);
}

/* shared build: exactly one of su / ss is used */
static inline int s2r__blocked_build(S2RBlocked *b, const uint64_t *su, const int64_t *ss,
                                     size_t n, size_t block, int is_signed){
    if(!b) return 0;
    if(n && !su && !ss) return 0;
    if(block==0) block=S2R_BLOCK_DEFAULT;
    memset(b,0,sizeof *b);
    b->block=block; b->count=n; b->is_signed=is_signed;
    b->bcls = is_signed?S2R_I8:S2R_8; b->ocls=S2R_8;
    b->pcls = S2R_8;  b->scls = is_signed?S2R_I8:S2R_8;
    b->nblocks=(n+block-1)/block;
    if(!b->nblocks) return 1;

    int64_t  *tbase=(int64_t*)malloc(b->nblocks*sizeof(int64_t));
    uint64_t *tspan=(uint64_t*)malloc(b->nblocks*sizeof(uint64_t));
    uint64_t *tsum =(uint64_t*)malloc(b->nblocks*sizeof(uint64_t));
    uint64_t *tstr =(uint64_t*)malloc(b->nblocks*sizeof(uint64_t));
    size_t   *toff =(size_t*)  malloc(b->nblocks*sizeof(size_t));
    b->bclass=(uint8_t*)malloc(b->nblocks);
    b->bflags=(uint8_t*)calloc(b->nblocks,1);
    if(!tbase||!tspan||!tsum||!tstr||!toff||!b->bclass||!b->bflags){
        free(tbase);free(tspan);free(tsum);free(tstr);free(toff); s2r_blocked_free(b); return 0;
    }

    size_t total=0;
    int64_t  lo_b=0, hi_b=0;                 /* range of the bases, SIGNED column   */
    uint64_t hi_bu=0, hi_su=0;               /* ...and of an UNSIGNED one.
        * These were tracked in int64_t for both, so a base or a block sum above
        * INT64_MAX read as negative, the running maximum stayed too small, and
        * the bookkeeping class came out far too narrow. The base was then
        * TRUNCATED on write and every value in that block came back wrong -
        * silently. A column of { 1, UINT64_MAX } classified its bases into 8
        * bits and returned 255 for UINT64_MAX. Unsigned ranges need unsigned
        * maxima; there is no sign to compare. */
    uint64_t hi_p=0;                         /* largest INDEX span */
    int64_t  lo_s=0, hi_s=0;                 /* range of the sums  */
    uint64_t hi_t=1;                         /* largest stride     */
    int any_stride=0;
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t start=bi*block, len=(start+block<=n)?block:(n-start);
        uint64_t span, sum=0;                /* unsigned: signed overflow is UB */
        int64_t base; int sorted=1;
        if(is_signed){
            int64_t mn=ss[start], mx=ss[start];
            for(size_t i=0;i<len;i++){
                int64_t x=ss[start+i];
                if(x<mn) mn=x;
                if(x>mx) mx=x;
                sum += (uint64_t)x;
                if(i && x<ss[start+i-1]) sorted=0;
            }
            base=mn; span=(uint64_t)mx-(uint64_t)mn;
        } else {
            uint64_t mn=su[start], mx=su[start];
            for(size_t i=0;i<len;i++){
                uint64_t x=su[start+i];
                if(x<mn) mn=x;
                if(x>mx) mx=x;
                sum += x;
                if(i && x<su[start+i-1]) sorted=0;
            }
            base=(int64_t)mn; span=mx-mn;
        }
        /* the common step: gcd of every offset from the base. Exact by
         * construction, so dividing it out loses nothing. A constant block has
         * gcd 0 - it has no step to speak of, and stride 1 keeps span 0. */
        uint64_t stride=0;
        if(span){
            for(size_t i=0;i<len && stride!=1;i++){
                uint64_t off = (is_signed ? (uint64_t)ss[start+i] : su[start+i]) - (uint64_t)base;
                stride = s2r_gcd64(stride, off);
            }
        }
        if(stride<2) stride=1; else { any_stride=1; span/=stride; }
        tstr[bi]=stride;
        if(stride>hi_t) hi_t=stride;

        tbase[bi]=base; tspan[bi]=span; tsum[bi]=sum;
        if(sorted) b->bflags[bi]=S2R_BLK_SORTED;
        uint8_t w=s2r__delta_bytes(span);
        b->bclass[bi]=(uint8_t)(w*8);
        if(!w) toff[bi]=0;
        else { size_t at=s2r_align_up(total,8); toff[bi]=at; total=at+len*(size_t)w; }
        if(bi==0){ lo_b=hi_b=base; hi_p=span; lo_s=hi_s=(int64_t)sum;
                   hi_bu=(uint64_t)base; hi_su=sum; }
        else {
            if(base<lo_b) lo_b=base;
            if(base>hi_b) hi_b=base;
            if(span>hi_p) hi_p=span;
            if((int64_t)sum<lo_s) lo_s=(int64_t)sum;
            if((int64_t)sum>hi_s) hi_s=(int64_t)sum;
            if((uint64_t)base>hi_bu) hi_bu=(uint64_t)base;
            if(sum>hi_su) hi_su=sum;
        }
    }

    /* every bookkeeping array gets the same treatment as the data */
    b->bcls = is_signed ? s2r_classify_signed_range(lo_b,hi_b) : (int8_t)s2r_classify(hi_bu);
    b->pcls = (int8_t)s2r_classify(hi_p);
    b->ocls = (int8_t)s2r_classify((uint64_t)total);
    b->scls = is_signed ? s2r_classify_signed_range(lo_s,hi_s)
                        : (int8_t)s2r_classify(hi_su);
    b->has_stride = any_stride;
    b->tcls = (int8_t)s2r_classify(hi_t);
    b->bbase=calloc(b->nblocks,s2r__md_bytes(b->bcls));
    b->bspan=calloc(b->nblocks,s2r__md_bytes(b->pcls));
    b->boff =calloc(b->nblocks,s2r__md_bytes(b->ocls));
    b->bsum =calloc(b->nblocks,s2r__md_bytes(b->scls));
    if(any_stride) b->bstride=calloc(b->nblocks,s2r__md_bytes(b->tcls));
    if(!b->bbase||!b->bspan||!b->boff||!b->bsum||(any_stride&&!b->bstride)){
        free(tbase);free(tspan);free(tsum);free(tstr);free(toff); s2r_blocked_free(b); return 0;
    }
    for(size_t bi=0;bi<b->nblocks;bi++){
        if(is_signed){ s2r__md_ws(b->bbase,b->bcls,bi,tbase[bi]);
                       s2r__md_ws(b->bsum ,b->scls,bi,(int64_t)tsum[bi]); }
        else         { s2r__md_wu(b->bbase,b->bcls,bi,(uint64_t)tbase[bi]);
                       s2r__md_wu(b->bsum ,b->scls,bi,tsum[bi]); }
        s2r__md_wu(b->bspan,b->pcls,bi,tspan[bi]);
        s2r__md_wu(b->boff ,b->ocls,bi,(uint64_t)toff[bi]);
        if(any_stride) s2r__md_wu(b->bstride,b->tcls,bi,tstr[bi]);
    }

    if(total){
        b->data=(uint8_t*)S2R_ALIGNED_ALLOC(s2r_align_up(total,S2R_ALIGNMENT),S2R_ALIGNMENT);
        if(!b->data){ free(tbase);free(tspan);free(tsum);free(tstr);free(toff); s2r_blocked_free(b); return 0; }
    }
    b->bytes=total;

    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t w=b->bclass[bi]>>3;
        if(!w) continue;
        size_t start=bi*block, len=(start+block<=n)?block:(n-start);
        uint64_t base=(uint64_t)tbase[bi], st=tstr[bi];
        uint8_t *d=b->data+toff[bi];
        for(size_t i=0;i<len;i++){
            uint64_t dv=(is_signed?(uint64_t)ss[start+i]:su[start+i])-base;
            if(st>1) dv/=st;                 /* exact: st divides every offset */
            switch(w){ case 1: d[i]=(uint8_t)dv; break;
                       case 2: ((uint16_t*)d)[i]=(uint16_t)dv; break;
                       case 4: ((uint32_t*)d)[i]=(uint32_t)dv; break;
                       default:((uint64_t*)d)[i]=dv; break; }
        }
    }
    free(tbase);free(tspan);free(tsum);free(tstr);free(toff);
    return 1;
}

static inline int s2r_blocked_build(S2RBlocked *b, const uint64_t *src, size_t n, size_t block){
    return s2r__blocked_build(b,src,NULL,n,block,0);
}
static inline int s2r_blocked_build_signed(S2RBlocked *b, const int64_t *src, size_t n, size_t block){
    return s2r__blocked_build(b,NULL,src,n,block,1);
}

static inline uint64_t s2r__blocked_delta(const S2RBlocked *b, size_t bi, size_t within){
    size_t w=b->bclass[bi]>>3;
    if(!w) return 0;
    const uint8_t *d=b->data+s2r__bo(b,bi);
    switch(w){ case 1: return d[within];
               case 2: return ((const uint16_t*)d)[within];
               case 4: return ((const uint32_t*)d)[within];
               default:return ((const uint64_t*)d)[within]; }
}
static inline uint64_t s2r_blocked_get(const S2RBlocked *b, size_t i){
    if(!b || i>=b->count) return 0;
    size_t bi=i/b->block;
    return s2r__bb(b,bi) + s2r__bt(b,bi)*s2r__blocked_delta(b,bi,i%b->block);
}
static inline int64_t s2r_blocked_get_signed(const S2RBlocked *b, size_t i){
    if(!b || i>=b->count) return 0;
    size_t bi=i/b->block;
    return (int64_t)(s2r__bb(b,bi) + s2r__bt(b,bi)*s2r__blocked_delta(b,bi,i%b->block));
}

/* O(nblocks) over metadata: the payload is never touched. */
static inline uint64_t s2r_blocked_sum(const S2RBlocked *b){
    if(!b) return 0;
    uint64_t s=0;
    for(size_t bi=0; bi<b->nblocks; bi++) s += s2r__bs(b,bi);
    return s;
}
static inline int64_t s2r_blocked_sum_signed(const S2RBlocked *b){
    return (int64_t)s2r_blocked_sum(b);      /* two's complement: bit-identical */
}
/* kept as the cross-check the zone sums are validated against, and as the path a
 * caller can use to verify a deserialized column against its own payload */
static inline uint64_t s2r_blocked_sum_fast(const S2RBlocked *b){
    uint64_t s=0; if(!b) return 0;
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t len=s2r__blk_len(b,bi);
        s += s2r__bb(b,bi)*(uint64_t)len;
        size_t w=b->bclass[bi]>>3;
        if(!w) continue;
        S2RPool view; memset(&view,0,sizeof view);
        view.data=b->data+s2r__bo(b,bi);
        view.size=(int8_t)b->bclass[bi];
        view.count=len; view.capacity=len; view.byte_cap=len*w;
        view.flags=S2R_FLAG_EXTERNAL;
        s += s2r__bt(b,bi)*s2r_sum_fast(&view);   /* SUM(v) = len*base + stride*SUM(i) */
    }
    return s;
}

/* O(nblocks): base + span is the block's true maximum, so no payload is read. */
static inline uint64_t s2r_blocked_max(const S2RBlocked *b){
    if(!b||!b->count) return 0;
    uint64_t m=s2r__bb(b,0)+s2r__bt(b,0)*s2r__bp(b,0);
    for(size_t bi=1; bi<b->nblocks; bi++){
        uint64_t x=s2r__bb(b,bi)+s2r__bt(b,bi)*s2r__bp(b,bi);
        if(x>m) m=x;
    }
    return m;
}
static inline uint64_t s2r_blocked_min(const S2RBlocked *b){
    if(!b||!b->count) return 0;
    uint64_t m=s2r__bb(b,0);
    for(size_t bi=1; bi<b->nblocks; bi++){ uint64_t x=s2r__bb(b,bi); if(x<m) m=x; }
    return m;
}

/* Storable footprint: delta payload, plus every bookkeeping array in the class it
 * was classified into. Offsets are derivable from the classes and the block size,
 * so a serialized form could drop them; this reports what is resident. */
static inline size_t s2r_blocked_bytes(const S2RBlocked *b){
    if(!b) return 0;
    return b->bytes + b->nblocks*(2 + s2r__md_bytes(b->bcls) + s2r__md_bytes(b->pcls)
                                    + s2r__md_bytes(b->ocls) + s2r__md_bytes(b->scls)
                                    + (b->has_stride ? s2r__md_bytes(b->tcls) : 0));
}
static inline int s2r_blocked_is_sorted(const S2RBlocked *b, size_t bi){
    return (b && bi<b->nblocks) ? ((b->bflags[bi]&S2R_BLK_SORTED)!=0) : 0;
}

/* first index in a sorted block whose delta exceeds kp */
static inline size_t s2r__blk_upper(const S2RBlocked *b, size_t bi, uint64_t kp){
    size_t w=b->bclass[bi]>>3, lo=0, hi=s2r__blk_len(b,bi);
    const uint8_t *d=b->data+s2r__bo(b,bi);
    while(lo<hi){
        size_t mid=lo+((hi-lo)>>1); uint64_t val;
        switch(w){ case 1: val=d[mid]; break;
                   case 2: val=((const uint16_t*)d)[mid]; break;
                   case 4: val=((const uint32_t*)d)[mid]; break;
                   default:val=((const uint64_t*)d)[mid]; break; }
        if(val>kp) hi=mid; else lo=mid+1;
    }
    return lo;
}

/* Predicate on the compact blocks.
 *
 * Three ways a block is answered, in increasing cost:
 *   1. its true range falls entirely on one side of the threshold - answered from
 *      base and span, no payload read at all;
 *   2. it is non-decreasing - binary search, O(log n);
 *   3. otherwise - the v3.4.0 SIMD dispatch on the block's deltas.
 *
 * Note the skip test avoids `base + span <= thr`: at delta width 8 span can be
 * UINT64_MAX and the sum wraps, which silently skipped blocks that did contain
 * matches. Testing `base > thr` first makes `thr - base` safe. */
static inline size_t s2r_blocked_count_gt(const S2RBlocked *b, uint64_t thr){
    if(!b||!b->count) return 0;
    size_t cnt=0;
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t len=s2r__blk_len(b,bi);
        uint64_t base=s2r__bb(b,bi);
        if(base>thr){ cnt+=len; continue; }          /* every value matches */
        uint64_t rem=thr-base;                       /* safe: base <= thr    */
        /* base + stride*i > thr  <=>  i > rem/stride. One integer division per
         * block moves the threshold into the index domain; the kernel below then
         * runs unchanged on the stored bytes. With stride 1 this is rem itself. */
        uint64_t st=s2r__bt(b,bi);
        if(st>1) rem/=st;
        if(s2r__bp(b,bi)<=rem) continue;             /* no value matches     */
        size_t w=b->bclass[bi]>>3;
        /* order is information - but only worth using once the block is big
         * enough that log2(n) dependent probes beat a vectorised scan */
        if((b->bflags[bi]&S2R_BLK_SORTED) && len*w >= S2R_BLK_BSEARCH_MIN_BYTES){
            cnt += len - s2r__blk_upper(b,bi,rem);
            continue;
        }
        S2RPool view; memset(&view,0,sizeof view);
        view.data=b->data+s2r__bo(b,bi);
        view.size=(int8_t)b->bclass[bi];
        view.count=len; view.capacity=len; view.byte_cap=len*w;
        view.flags=S2R_FLAG_EXTERNAL;
        cnt += s2r_count_gt_fast(&view, rem);
    }
    return cnt;
}


/* ---- the rest of the predicate family, on the block-wise layer -------------
 *
 * Until now S2RBlocked answered exactly ONE predicate, count_gt, while the flat
 * pool answered five. That is backwards: the zone map - a per-block minimum,
 * span and sum - is worth MORE to a bounded range query than to a one-sided one,
 * because a window can miss a block from either end.
 *
 * All four go through the same three ways a block is answered:
 *   1. the block's true range falls entirely inside or entirely outside the
 *      window - answered from base, stride and span, no payload read;
 *   2. the block is constant (delta width 0) - the base IS every value;
 *   3. otherwise - the shipped SIMD dispatch, with the window moved into the
 *      index domain exactly as count_gt moves the threshold.
 */

/* index window of a block for the value window [lo,hi]; 0 if empty, and
 * *whole = 1 when every value in the block matches */
static inline int s2r__blk_window(const S2RBlocked *b, size_t bi,
                                  uint64_t lo, uint64_t hi,
                                  uint64_t *ilo, uint64_t *ihi, int *whole){
    uint64_t base=s2r__bb(b,bi), st=s2r__bt(b,bi), sp=s2r__bp(b,bi);
    uint64_t vmax=base+st*sp;                 /* the block's true maximum */
    *whole=0;
    if(hi<base || lo>vmax) return 0;          /* disjoint: skip, no payload read */
    if(lo<=base && hi>=vmax){ *whole=1; return 1; }
    uint64_t rl = (lo<=base) ? 0u : (lo-base);
    uint64_t rh = (hi>=vmax) ? (st*sp) : (hi-base);
    *ilo = (lo<=base) ? 0u : (st>1 ? (rl+st-1)/st : rl);    /* ceil  */
    *ihi = st>1 ? rh/st : rh;                               /* floor */
    return *ilo<=*ihi;
}

static inline size_t s2r_blocked_count_range(const S2RBlocked *b, uint64_t lo, uint64_t hi){
    if(!b||!b->count||lo>hi) return 0;
    size_t cnt=0;
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t len=s2r__blk_len(b,bi);
        uint64_t il=0, ih=0; int whole=0;
        if(!s2r__blk_window(b,bi,lo,hi,&il,&ih,&whole)) continue;
        if(whole){ cnt+=len; continue; }
        size_t w=b->bclass[bi]>>3;
        if(!w){ cnt+=len; continue; }          /* constant block, and it matched */
        S2RPool view; memset(&view,0,sizeof view);
        view.data=b->data+s2r__bo(b,bi);
        view.size=(int8_t)b->bclass[bi];
        view.count=len; view.capacity=len; view.byte_cap=len*w;
        view.flags=S2R_FLAG_EXTERNAL;
        cnt += s2r_count_range_fast(&view, il, ih);
    }
    return cnt;
}
static inline size_t s2r_blocked_count_lt(const S2RBlocked *b, uint64_t thr){
    if(!b||!b->count||thr==0) return 0;
    return s2r_blocked_count_range(b,0,thr-1);
}
static inline size_t s2r_blocked_count_eq(const S2RBlocked *b, uint64_t v){
    return s2r_blocked_count_range(b,v,v);
}
/* SUM over a window: per block, count*base + stride*SUM(index | window) */
static inline uint64_t s2r_blocked_sum_if(const S2RBlocked *b, uint64_t lo, uint64_t hi){
    if(!b||!b->count||lo>hi) return 0;
    uint64_t total=0;
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t len=s2r__blk_len(b,bi);
        uint64_t il=0, ih=0; int whole=0;
        if(!s2r__blk_window(b,bi,lo,hi,&il,&ih,&whole)) continue;
        uint64_t base=s2r__bb(b,bi), st=s2r__bt(b,bi);
        if(whole){ total += s2r__bs(b,bi); continue; }   /* the zone sum, verbatim */
        size_t w=b->bclass[bi]>>3;
        if(!w){ total += base*(uint64_t)len; continue; }
        S2RPool view; memset(&view,0,sizeof view);
        view.data=b->data+s2r__bo(b,bi);
        view.size=(int8_t)b->bclass[bi];
        view.count=len; view.capacity=len; view.byte_cap=len*w;
        view.flags=S2R_FLAG_EXTERNAL;
        uint64_t c=(uint64_t)s2r_count_range_fast(&view,il,ih);
        total += base*c + st*s2r_sum_if_fast(&view,il,ih);
    }
    return total;
}
static inline int64_t s2r_blocked_sum_if_signed(const S2RBlocked *b, int64_t lo, int64_t hi){
    if(!b||!b->count||lo>hi) return 0;
    return (int64_t)s2r_blocked_sum_if(b,(uint64_t)lo,(uint64_t)hi);
}

/* ---- portable serialization: fmt = 2, canonical LE, CRC32 ----
 *
 *   0   4  magic "SR33"
 *   4   1  class   = 0   (sentinel: blocked, per-block classes follow)
 *   5   1  flags   bit 0 = signed column
 *   6   1  fmt     = 2
 *   7   1  rsvd    = 0
 *   8   8  count        u64
 *  16   8  block        u64
 *  24   8  nblocks      u64
 *  32   1  bcls  (int8)   class of the bases
 *  33   1  pcls  (int8)   class of the spans
 *  34   1  ocls  (int8)   class of the offsets
 *  35   1  scls  (int8)   class of the block sums
 *  36   8  payload bytes  u64
 *  44   .  bclass[] bflags[] bbase[] bspan[] boff[] bsum[] payload[]
 *       4  crc32 of everything from offset 44 onwards
 *
 * fmt = 3 is the same file with one extra class byte and one extra array:
 *
 *  44   1  tcls  (int8)   class of the per-block strides
 *  45   .  bclass[] bflags[] bbase[] bspan[] boff[] bsum[] bstride[] payload[]
 *       4  crc32 of everything from offset 45 onwards
 *
 * A writer emits fmt = 3 only when some block actually has a stride above 1, so
 * a column without one is byte-for-byte the file v3.4.0 wrote and a v3.4.0
 * reader still opens it. A v3.5.0 reader accepts both, and reads fmt = 2 as
 * "every stride is 1" - which is what it means.
 */
#if S2R_HAS_STDIO

/* nblocks * (per-block metadata) + payload bytes.
 *
 * On the WRITE side both terms come from a struct this library built, so the
 * sum is the size of something that already exists in memory and cannot
 * overflow. On the READ side both terms come off disk, and a file that says
 * nblocks = 2^22 and bytes = 2^64 - 2^22*K + 16 makes the sum wrap to 16: the
 * loader would malloc 16 bytes and then memcpy megabytes into it. So the sum
 * is done in checked arithmetic and the length is only handed back when it is
 * real. Portable on purpose - no compiler builtins, same guard style the flat
 * loader already uses (count > SIZE_MAX/eb). */
static inline int s2r__blk_body_len_ck(const S2RBlocked *b, size_t *out){
    size_t per = (size_t)2 + s2r__md_bytes(b->bcls) + s2r__md_bytes(b->pcls)
                           + s2r__md_bytes(b->ocls) + s2r__md_bytes(b->scls)
                           + (b->has_stride ? s2r__md_bytes(b->tcls) : 0);
    size_t len;
    if(per && b->nblocks > (size_t)SIZE_MAX / per) return 0;
    len = b->nblocks * per;
    if(b->bytes > (size_t)SIZE_MAX - len) return 0;
    *out = len + b->bytes;
    return 1;
}
static inline size_t s2r__blk_body_len(const S2RBlocked *b){
    size_t len;
    return s2r__blk_body_len_ck(b,&len) ? len : (size_t)SIZE_MAX;
}
/* stage the body in canonical LE so the bytes are identical on any host */
static inline uint8_t *s2r__blk_stage(const S2RBlocked *b, size_t *out){
    size_t len;
    if(!s2r__blk_body_len_ck(b,&len)) return NULL;
    uint8_t *buf=(uint8_t*)malloc(len?len:1);
    if(!buf) return NULL;
    size_t at=0;
    /* memcpy with a null source is undefined even when the length is zero */
    if(b->nblocks){
        memcpy(buf+at,b->bclass,b->nblocks); at+=b->nblocks;
        memcpy(buf+at,b->bflags,b->nblocks); at+=b->nblocks;
    }
    struct { const void *a; int8_t c; int sg; } arr[5] = {
        { b->bbase, b->bcls, b->is_signed },
        { b->bspan, b->pcls, 0 },
        { b->boff,  b->ocls, 0 },
        { b->bsum,  b->scls, b->is_signed },
        { b->bstride, b->tcls, 0 } };
    int narr = b->has_stride ? 5 : 4;
    for(int k=0;k<narr;k++){
        size_t eb=s2r__md_bytes(arr[k].c);
        for(size_t i=0;i<b->nblocks;i++){
            uint64_t raw = arr[k].sg ? (uint64_t)s2r__md_rs(arr[k].a,arr[k].c,i)
                                     : s2r__md_ru(arr[k].a,arr[k].c,i);
            for(size_t j=0;j<eb;j++) buf[at++]=(uint8_t)(raw>>(8*j));
        }
    }
    if(b->bytes){
        memcpy(buf+at,b->data,b->bytes);
#if !S2R_LITTLE_ENDIAN
        for(size_t bi=0;bi<b->nblocks;bi++){
            size_t w=b->bclass[bi]>>3;
            if(w>1) s2r_swap_payload(buf+at+s2r__bo(b,bi), w, s2r__blk_len(b,bi));
        }
#endif
        at+=b->bytes;
    }
    *out=at;
    return buf;
}

static inline S2RError s2r_blocked_save(const S2RBlocked *b, const char *path){
    if(!b||!path) return S2R_ERR_NULL;
    size_t blen=0; uint8_t *body=s2r__blk_stage(b,&blen);
    if(!body) return S2R_ERR_OOM;
    FILE *f=fopen(path,"wb");
    if(!f){ free(body); return S2R_ERR_IO; }
    uint8_t fmt = (uint8_t)(b->has_stride ? S2R_BLK_FMT_S : S2R_BLK_FMT);
    uint8_t hdr[4]={ 0, (uint8_t)(b->is_signed?S2R_FLAG_SIGNED:0), fmt, 0 };
    uint8_t cls[4]={ (uint8_t)b->bcls,(uint8_t)b->pcls,(uint8_t)b->ocls,(uint8_t)b->scls };
    uint8_t tb = (uint8_t)b->tcls;
    int ok = s2r__wr_le32(f,S2R_MAGIC_V33)
          && fwrite(hdr,4,1,f)==1
          && s2r__wr_le64(f,(uint64_t)b->count)
          && s2r__wr_le64(f,(uint64_t)b->block)
          && s2r__wr_le64(f,(uint64_t)b->nblocks)
          && fwrite(cls,4,1,f)==1
          && s2r__wr_le64(f,(uint64_t)b->bytes);
    if(ok && b->has_stride) ok = fwrite(&tb,1,1,f)==1;
    if(ok && blen) ok = fwrite(body,blen,1,f)==1;
    uint32_t crc = blen ? s2r_crc32(body,blen,0) : 0;
    if(ok) ok = s2r__wr_le32(f,crc);
    free(body); fclose(f);
    return ok ? S2R_OK : S2R_ERR_IO;
}

static inline S2RError s2r_blocked_load(S2RBlocked *b, const char *path){
    if(!b||!path) return S2R_ERR_NULL;
    memset(b,0,sizeof *b);
    FILE *f=fopen(path,"rb"); if(!f) return S2R_ERR_IO;
    uint32_t magic; uint8_t hdr[4],cls[4]; uint64_t cnt,blk,nb,pb;
    if(!s2r__rd_le32(f,&magic)||magic!=S2R_MAGIC_V33){ fclose(f); return S2R_ERR_CORRUPT; }
    if(fread(hdr,4,1,f)!=1){ fclose(f); return S2R_ERR_IO; }
    if(hdr[0]!=0||hdr[3]!=0
       ||(hdr[2]!=(uint8_t)S2R_BLK_FMT && hdr[2]!=(uint8_t)S2R_BLK_FMT_S)){
        fclose(f); return S2R_ERR_CORRUPT; }
    if(!s2r__rd_le64(f,&cnt)||!s2r__rd_le64(f,&blk)||!s2r__rd_le64(f,&nb)){ fclose(f); return S2R_ERR_IO; }
    if(fread(cls,4,1,f)!=1){ fclose(f); return S2R_ERR_IO; }
    if(!s2r__rd_le64(f,&pb)){ fclose(f); return S2R_ERR_IO; }
    b->has_stride = (hdr[2]==(uint8_t)S2R_BLK_FMT_S) ? 1 : 0;
    b->tcls = S2R_8;
    if(b->has_stride){
        uint8_t tb;
        if(fread(&tb,1,1,f)!=1){ fclose(f); memset(b,0,sizeof *b); return S2R_ERR_IO; }
        b->tcls=(int8_t)tb;
        size_t at5=s2r_abs_size(b->tcls);
        if(b->tcls<0 || !(at5==8||at5==16||at5==32||at5==64)){
            fclose(f); memset(b,0,sizeof *b); return S2R_ERR_CORRUPT; }
    }
    if(blk==0||cnt>(uint64_t)SIZE_MAX||nb>(uint64_t)SIZE_MAX||pb>(uint64_t)SIZE_MAX){
        fclose(f); memset(b,0,sizeof *b); return S2R_ERR_CORRUPT; }
    if(nb != (cnt+blk-1)/blk){ fclose(f); return S2R_ERR_CORRUPT; }  /* never trust it */
    b->is_signed=(hdr[1]&S2R_FLAG_SIGNED)?1:0;
    b->count=(size_t)cnt; b->block=(size_t)blk; b->nblocks=(size_t)nb; b->bytes=(size_t)pb;
    b->bcls=(int8_t)cls[0]; b->pcls=(int8_t)cls[1]; b->ocls=(int8_t)cls[2]; b->scls=(int8_t)cls[3];
    {   size_t a1=s2r_abs_size(b->bcls),a2=s2r_abs_size(b->pcls),
               a3=s2r_abs_size(b->ocls),a4=s2r_abs_size(b->scls);
        int bad = !(a1==8||a1==16||a1==32||a1==64) || !(a2==8||a2==16||a2==32||a2==64)
               || !(a3==8||a3==16||a3==32||a3==64) || !(a4==8||a4==16||a4==32||a4==64)
               || b->pcls<0 || b->ocls<0
               || (b->is_signed ? (b->bcls>0||b->scls>0) : (b->bcls<0||b->scls<0));
        if(bad){ fclose(f); memset(b,0,sizeof *b); return S2R_ERR_CORRUPT; }
    }
    if(!b->nblocks){ fclose(f); return S2R_OK; }

    size_t blen;
    /* first lock: the claimed body length has to be arithmetic that closes */
    if(!s2r__blk_body_len_ck(b,&blen)){ fclose(f); memset(b,0,sizeof *b); return S2R_ERR_CORRUPT; }
    /* second lock: it also has to be a body that is actually IN the file. The
     * bytes on disk are the only honest witness of how big the body is, and
     * they are free - the file is already open and seekable. */
    {   long here=ftell(f), end;
        if(here<0 || fseek(f,0,SEEK_END)!=0){ fclose(f); memset(b,0,sizeof *b); return S2R_ERR_IO; }
        end=ftell(f);
        if(end<0 || fseek(f,here,SEEK_SET)!=0){ fclose(f); memset(b,0,sizeof *b); return S2R_ERR_IO; }
        if(end-here < 4 || blen > (size_t)(end-here) - 4u){
            fclose(f); memset(b,0,sizeof *b); return S2R_ERR_CORRUPT; }
    }
    uint8_t *body=(uint8_t*)malloc(blen?blen:1);
    if(!body){ fclose(f); memset(b,0,sizeof *b); return S2R_ERR_OOM; }
    if(blen && fread(body,blen,1,f)!=1){ free(body); fclose(f); memset(b,0,sizeof *b); return S2R_ERR_IO; }
    uint32_t crc_disk;
    if(!s2r__rd_le32(f,&crc_disk)){ free(body); fclose(f); memset(b,0,sizeof *b); return S2R_ERR_IO; }
    if(fgetc(f)!=EOF){ free(body); fclose(f); memset(b,0,sizeof *b); return S2R_ERR_CORRUPT; }
    fclose(f);
    if(crc_disk != (blen ? s2r_crc32(body,blen,0) : 0)){
        free(body); memset(b,0,sizeof *b); return S2R_ERR_CORRUPT; }

    b->bclass=(uint8_t*)malloc(b->nblocks);
    b->bflags=(uint8_t*)malloc(b->nblocks);
    b->bbase=calloc(b->nblocks,s2r__md_bytes(b->bcls));
    b->bspan=calloc(b->nblocks,s2r__md_bytes(b->pcls));
    b->boff =calloc(b->nblocks,s2r__md_bytes(b->ocls));
    b->bsum =calloc(b->nblocks,s2r__md_bytes(b->scls));
    if(b->has_stride) b->bstride=calloc(b->nblocks,s2r__md_bytes(b->tcls));
    if(!b->bclass||!b->bflags||!b->bbase||!b->bspan||!b->boff||!b->bsum
       ||(b->has_stride&&!b->bstride)){
        free(body); s2r_blocked_free(b); return S2R_ERR_OOM; }
    size_t at=0;
    memcpy(b->bclass,body+at,b->nblocks); at+=b->nblocks;
    memcpy(b->bflags,body+at,b->nblocks); at+=b->nblocks;   /* nblocks > 0 here */
    {   struct { void *a; int8_t c; int sg; } arr[5] = {
            { b->bbase, b->bcls, b->is_signed }, { b->bspan, b->pcls, 0 },
            { b->boff,  b->ocls, 0 },            { b->bsum,  b->scls, b->is_signed },
            { b->bstride, b->tcls, 0 } };
        int narr = b->has_stride ? 5 : 4;
        for(int k=0;k<narr;k++){
            size_t eb=s2r__md_bytes(arr[k].c);
            for(size_t i=0;i<b->nblocks;i++){
                uint64_t raw=0;
                for(size_t j=0;j<eb;j++) raw|=(uint64_t)body[at+j]<<(8*j);
                at+=eb;
                if(arr[k].sg){
                    int64_t sv=(eb==8)?(int64_t)raw
                        :(int64_t)((raw&(1ull<<(eb*8-1))) ? (raw|~((1ull<<(eb*8))-1)) : raw);
                    s2r__md_ws(arr[k].a,arr[k].c,i,sv);
                } else s2r__md_wu(arr[k].a,arr[k].c,i,raw);
            }
        }
    }
    for(size_t bi=0;bi<b->nblocks;bi++){
        uint8_t cb=b->bclass[bi];
        if(!(cb==0||cb==8||cb==16||cb==32||cb==64)){ free(body); s2r_blocked_free(b); return S2R_ERR_CORRUPT; }
        /* a stride of 0 would divide by zero in the predicate and collapse every
         * value onto the base: it is never something this library writes */
        if(b->has_stride && s2r__bt(b,bi)==0){ free(body); s2r_blocked_free(b); return S2R_ERR_CORRUPT; }
        if(cb){
            size_t w=cb>>3, o=s2r__bo(b,bi), len=s2r__blk_len(b,bi);
            if(o>b->bytes || len>(b->bytes-o)/w){ free(body); s2r_blocked_free(b); return S2R_ERR_CORRUPT; }
        }
    }
    if(b->bytes){
        b->data=(uint8_t*)S2R_ALIGNED_ALLOC(s2r_align_up(b->bytes,S2R_ALIGNMENT),S2R_ALIGNMENT);
        if(!b->data){ free(body); s2r_blocked_free(b); return S2R_ERR_OOM; }
        memcpy(b->data,body+at,b->bytes);
#if !S2R_LITTLE_ENDIAN
        for(size_t bi=0;bi<b->nblocks;bi++){
            size_t w=b->bclass[bi]>>3;
            if(w>1) s2r_swap_payload(b->data+s2r__bo(b,bi), w, s2r__blk_len(b,bi));
        }
#endif
    }
    free(body);
    return S2R_OK;
}
#endif /* S2R_HAS_STDIO */

/* =====================================================================
 * AFFINE POOL - the frame of reference with a scale, on a flat column
 * =====================================================================
 *
 * S2RBlocked factors base and stride per block. A whole column often has a
 * single common step - a sampling interval, a fixed-point granularity, an ID
 * allocated in fixed increments - and then one base and one stride serve the
 * entire array, with the index pool staying a plain S2RPool that every shipped
 * SIMD kernel already understands.
 *
 * The pool of indices is ALWAYS unsigned, even for a signed column: the offsets
 * are measured from the minimum, so they cannot be negative. The sign lives in
 * the base alone.
 *
 * Every operation rewrites in closed form and then calls the shipped kernel, so
 * there is no second implementation to keep in agreement:
 *
 *     v = base + stride*i
 *     v > t            <=>  i > (t - base) / stride
 *     v in [lo,hi]     <=>  i in [ceil((lo-base)/stride), floor((hi-base)/stride)]
 *     SUM(v)           =    n*base + stride*SUM(i)
 *     SUM(v | pred)    =    count*base + stride*SUM(i | pred)
 */
typedef struct {
    S2RPool  idx;        /* the indices, unsigned, in their own smallest class */
    int64_t  base;       /* minimum of the column                             */
    uint64_t stride;     /* common step; 1 means "no scale to factor"         */
    size_t   count;
    int      is_signed;
    int      is_const;   /* every value equals base: NO payload is allocated   */
} S2RAffine;

/* gcd of the offsets from the minimum. Returns the stride; 1 means there is no
 * scale worth factoring. Costs one pass and stops early once it reaches 1. */
static inline uint64_t s2r_affine_detect(const uint64_t *v, size_t n, uint64_t *base_out){
    if(!v||!n){ if(base_out)*base_out=0; return 1; }
    uint64_t mn=v[0];
    for(size_t i=1;i<n;i++) if(v[i]<mn) mn=v[i];
    uint64_t g=0;
    for(size_t i=0;i<n && g!=1;i++) g=s2r_gcd64(g,v[i]-mn);
    if(base_out)*base_out=mn;
    return g<2 ? 1u : g;
}
static inline uint64_t s2r_affine_detect_signed(const int64_t *v, size_t n, int64_t *base_out){
    if(!v||!n){ if(base_out)*base_out=0; return 1; }
    int64_t mn=v[0];
    for(size_t i=1;i<n;i++) if(v[i]<mn) mn=v[i];
    uint64_t g=0;
    for(size_t i=0;i<n && g!=1;i++) g=s2r_gcd64(g,(uint64_t)v[i]-(uint64_t)mn);
    if(base_out)*base_out=mn;
    return g<2 ? 1u : g;
}

static inline void s2r_affine_free(S2RAffine *a){
    if(!a) return;
    s2r_pool_free(&a->idx);
    memset(a,0,sizeof *a);
}

static inline int s2r__affine_build(S2RAffine *a, const uint64_t *su, const int64_t *ss,
                                    size_t n, int is_signed){
    if(!a) return 0;
    if(n && !su && !ss) return 0;
    memset(a,0,sizeof *a);
    a->count=n; a->is_signed=is_signed; a->stride=1;
    if(!n){ return s2r_pool_init(&a->idx,S2R_8,1); }   /* pool_init: 1 = ok */

    uint64_t st; int64_t bs;
    if(is_signed){ st=s2r_affine_detect_signed(ss,n,&bs); }
    else { uint64_t ub; st=s2r_affine_detect(su,n,&ub); bs=(int64_t)ub; }
    a->base=bs; a->stride=st;

    uint64_t imax=0;
    for(size_t i=0;i<n;i++){
        uint64_t off=(is_signed?(uint64_t)ss[i]:su[i])-(uint64_t)bs;
        if(st>1) off/=st;
        if(off>imax) imax=off;
    }
    /* a constant column carries zero bits: the base and the count ARE the column,
     * and allocating one byte per element to store the same index would be the
     * exact habit this library exists to break */
    if(imax==0){ a->is_const=1; return s2r_pool_init(&a->idx,S2R_8,1); }
    if(!s2r_pool_init(&a->idx,(int8_t)s2r_classify(imax),n)) return 0;
    for(size_t i=0;i<n;i++){
        uint64_t off=(is_signed?(uint64_t)ss[i]:su[i])-(uint64_t)bs;
        if(st>1) off/=st;
        s2r_set(&a->idx,i,off);
    }
    a->idx.count=n;
    return 1;
}
static inline int s2r_affine_build(S2RAffine *a, const uint64_t *src, size_t n){
    return s2r__affine_build(a,src,NULL,n,0);
}
static inline int s2r_affine_build_signed(S2RAffine *a, const int64_t *src, size_t n){
    return s2r__affine_build(a,NULL,src,n,1);
}

static inline size_t s2r_affine_bytes(const S2RAffine *a){
    if(!a) return 0;
    return a->is_const ? 0u : s2r_pool_bytes(&a->idx);
}
static inline uint64_t s2r_affine_get(const S2RAffine *a, size_t i){
    if(!a||i>=a->count) return 0;
    if(a->is_const) return (uint64_t)a->base;
    return (uint64_t)a->base + a->stride*s2r_get(&a->idx,i);
}
static inline int64_t s2r_affine_get_signed(const S2RAffine *a, size_t i){
    if(!a||i>=a->count) return 0;
    if(a->is_const) return a->base;
    return (int64_t)((uint64_t)a->base + a->stride*s2r_get(&a->idx,i));
}
static inline uint64_t s2r_affine_sum(const S2RAffine *a){
    if(!a||!a->count) return 0;
    if(a->is_const) return (uint64_t)a->base*(uint64_t)a->count;
    return (uint64_t)a->base*(uint64_t)a->count + a->stride*s2r_sum_fast(&a->idx);
}
static inline int64_t s2r_affine_sum_signed(const S2RAffine *a){
    return (int64_t)s2r_affine_sum(a);        /* two's complement: bit-identical */
}

/* index window for the value window [lo,hi]; returns 0 if the window is empty */
static inline int s2r__affine_window(const S2RAffine *a, int64_t lo, int64_t hi,
                                     uint64_t *ilo, uint64_t *ihi){
    if(!a||!a->count) return 0;
    int64_t b=a->base; uint64_t st=a->stride;
    if(a->is_signed){ if(hi<b) return 0; }
    else            { if((uint64_t)hi<(uint64_t)b) return 0; }
    uint64_t rl, rh;
    int lo_below = a->is_signed ? (lo<=b) : ((uint64_t)lo<=(uint64_t)b);
    rl = lo_below ? 0u : ((uint64_t)lo-(uint64_t)b);
    rh = (uint64_t)hi-(uint64_t)b;
    *ilo = lo_below ? 0u : (st>1 ? (rl+st-1)/st : rl);   /* ceil  */
    *ihi = st>1 ? rh/st : rh;                            /* floor */
    return *ilo<=*ihi;
}

static inline size_t s2r_affine_count_gt(const S2RAffine *a, int64_t thr){
    if(!a||!a->count) return 0;
    int64_t b=a->base;
    int all = a->is_signed ? (thr<b) : ((uint64_t)thr<(uint64_t)b);
    if(all) return a->count;
    if(a->is_const) return 0;                 /* base <= thr and base is the value */
    uint64_t rem=(uint64_t)thr-(uint64_t)b;
    if(a->stride>1) rem/=a->stride;
    return s2r_count_gt_fast(&a->idx,rem);
}
static inline size_t s2r_affine_count_range(const S2RAffine *a, int64_t lo, int64_t hi){
    uint64_t il=0, ih=0;
    if(!s2r__affine_window(a,lo,hi,&il,&ih)) return 0;
    if(a->is_const) return il==0 ? a->count : 0;  /* index 0 is the only one */
    return s2r_count_range_fast(&a->idx,il,ih);
}
static inline size_t s2r_affine_count_lt(const S2RAffine *a, int64_t thr){
    if(!a||!a->count) return 0;
    int64_t b=a->base;
    int none = a->is_signed ? (thr<=b) : ((uint64_t)thr<=(uint64_t)b);
    if(none) return 0;
    return s2r_affine_count_range(a,b,(int64_t)((uint64_t)thr-1u));
}
static inline size_t s2r_affine_count_eq(const S2RAffine *a, int64_t val){
    if(!a||!a->count) return 0;
    int64_t b=a->base;
    int below = a->is_signed ? (val<b) : ((uint64_t)val<(uint64_t)b);
    if(below) return 0;
    uint64_t off=(uint64_t)val-(uint64_t)b;
    if(a->is_const) return off==0 ? a->count : 0;
    if(a->stride>1 && off%a->stride) return 0;      /* not on the lattice */
    if(a->stride>1) off/=a->stride;
    return s2r_count_eq_fast(&a->idx,off);
}
/* SUM(v | v in [lo,hi]) = count*base + stride*SUM(i | i in [ilo,ihi]) */
static inline uint64_t s2r_affine_sum_if(const S2RAffine *a, int64_t lo, int64_t hi){
    uint64_t il=0, ih=0;
    if(!s2r__affine_window(a,lo,hi,&il,&ih)) return 0;
    if(a->is_const) return il==0 ? (uint64_t)a->base*(uint64_t)a->count : 0;
    uint64_t cnt=(uint64_t)s2r_count_range_fast(&a->idx,il,ih);
    return (uint64_t)a->base*cnt + a->stride*s2r_sum_if_fast(&a->idx,il,ih);
}
static inline int64_t s2r_affine_sum_if_signed(const S2RAffine *a, int64_t lo, int64_t hi){
    return (int64_t)s2r_affine_sum_if(a,lo,hi);
}


/* ---- classifying the BLOCK SIZE, not just the width -----------------------
 *
 * The library picks the class from the data and then asks the caller to guess
 * the block size, which is the more consequential parameter of the two. The
 * default was measured against three real shapes and is DOMINATED on two of
 * them - smaller and faster at a larger block - while on the third it trades
 * 200x of predicate time for 1.9x of memory without telling anyone the trade
 * exists.
 *
 * So classify it the same way: from the data, once.
 *
 * The cost model is exact, not sampled. One pass at the finest granularity
 * records per-cell min, max and common step; adjacent cells then MERGE in a
 * tree, which is exact because
 *
 *     child i holds  min_i + g_i * k        (every value, by construction)
 *     so relative to m = min(min_i):  (min_i - m) + g_i * k
 *     and the merged step is  gcd over i of ( min_i - m , g_i )
 *
 * Every candidate block size is then priced from the same single pass. */

#define S2R_PLAN_GRAIN 64u          /* finest granularity of the cost model */
#define S2R_PLAN_MAX   12           /* candidates: grain, then doubling      */

typedef struct {
    size_t block;        /* candidate block size            */
    size_t bytes;        /* payload + metadata, exact       */
    size_t nblocks;
    size_t nconst;       /* blocks that store no payload    */
    int    has_stride;
} S2RBlockPlan;

typedef struct { uint64_t mn, mx, g, sum; int any; } S2R__Cell;

static inline void s2r__cell_merge(S2R__Cell *o, const S2R__Cell *a, const S2R__Cell *b){
    if(!a->any){ *o=*b; return; }
    if(!b->any){ *o=*a; return; }
    uint64_t m = a->mn < b->mn ? a->mn : b->mn;
    uint64_t M = a->mx > b->mx ? a->mx : b->mx;
    uint64_t g = s2r_gcd64(a->g, b->g);
    g = s2r_gcd64(g, a->mn - m);
    g = s2r_gcd64(g, b->mn - m);
    o->mn=m; o->mx=M; o->g=g; o->sum=a->sum+b->sum; o->any=1;
}

/* Prices every candidate block size from ONE pass over the data.
 * Returns how many candidates were written into out[]. */
static inline int s2r_blocked_plan(const uint64_t *su, const int64_t *ss, size_t n,
                                   int is_signed, S2RBlockPlan *out, int max_out)
{
    if((!su&&!ss) || !n || !out || max_out<=0) return 0;
    size_t ncell=(n+S2R_PLAN_GRAIN-1)/S2R_PLAN_GRAIN;
    S2R__Cell *cell=(S2R__Cell*)malloc(ncell*sizeof(S2R__Cell));
    if(!cell) return 0;
    for(size_t c=0;c<ncell;c++){
        size_t st=c*S2R_PLAN_GRAIN, len=(st+S2R_PLAN_GRAIN<=n)?S2R_PLAN_GRAIN:(n-st);
        uint64_t mn,mx,sum=0;
        if(is_signed){ int64_t a=ss[st],b=ss[st];
            for(size_t i=0;i<len;i++){ int64_t x=ss[st+i];
                if(x<a) a=x;
                if(x>b) b=x;
                sum+=(uint64_t)x; }
            mn=(uint64_t)a; mx=(uint64_t)b;
        } else { mn=su[st]; mx=su[st];
            for(size_t i=0;i<len;i++){ uint64_t x=su[st+i];
                if(x<mn) mn=x;
                if(x>mx) mx=x;
                sum+=x; } }
        uint64_t g=0;
        if(mx!=mn){ for(size_t i=0;i<len && g!=1;i++){
            uint64_t x=is_signed?(uint64_t)ss[st+i]:su[st+i]; g=s2r_gcd64(g,x-mn); } }
        if(g<2) g=1;
        cell[c].mn=mn; cell[c].mx=mx; cell[c].g=g; cell[c].sum=sum; cell[c].any=1;
    }

    int nout=0;
    for(size_t blk=S2R_PLAN_GRAIN; blk<=(size_t)1<<20 && nout<max_out; blk*=2){
        size_t per=blk/S2R_PLAN_GRAIN;              /* cells per block */
        size_t nb=(n+blk-1)/blk;
        size_t total=0, nconst=0;
        uint64_t hi_p=0, hi_t=1, hi_b=0, hi_s=0;
        int64_t lo_b=0, hi_bs=0, lo_s=0, hi_ss=0; int first=1, any_stride=0;
        for(size_t bi=0; bi<nb; bi++){
            S2R__Cell acc; acc.any=0; acc.mn=0; acc.mx=0; acc.g=0; acc.sum=0;
            size_t c0=bi*per, c1=c0+per; if(c1>ncell) c1=ncell;
            for(size_t c=c0;c<c1;c++){ S2R__Cell t; s2r__cell_merge(&t,&acc,&cell[c]); acc=t; }
            size_t start=bi*blk, len=(start+blk<=n)?blk:(n-start);
            uint64_t span=(acc.mx-acc.mn)/acc.g;
            if(acc.g>1) any_stride=1;
            uint8_t w=s2r__delta_bytes(span);
            if(!w) nconst++;
            else { size_t at=s2r_align_up(total,8); total=at+len*(size_t)w; }
            if(span>hi_p) hi_p=span;
            if(acc.g>hi_t) hi_t=acc.g;
            if(is_signed){
                int64_t bs=(int64_t)acc.mn, sm=(int64_t)acc.sum;
                if(first){ lo_b=hi_bs=bs; lo_s=hi_ss=sm; }
                else {
                    if(bs<lo_b) lo_b=bs;
                    if(bs>hi_bs) hi_bs=bs;
                    if(sm<lo_s) lo_s=sm;
                    if(sm>hi_ss) hi_ss=sm;
                }
            } else {
                if(acc.mn>hi_b) hi_b=acc.mn;
                if(acc.sum>hi_s) hi_s=acc.sum;
            }
            first=0;
        }
        int8_t bcls = is_signed ? s2r_classify_signed_range(lo_b,hi_bs) : (int8_t)s2r_classify(hi_b);
        int8_t pcls = (int8_t)s2r_classify(hi_p);
        int8_t ocls = (int8_t)s2r_classify((uint64_t)total);
        int8_t scls = is_signed ? s2r_classify_signed_range(lo_s,hi_ss) : (int8_t)s2r_classify(hi_s);
        int8_t tcls = (int8_t)s2r_classify(hi_t);
        size_t meta = nb*(2 + s2r__md_bytes(bcls) + s2r__md_bytes(pcls)
                            + s2r__md_bytes(ocls) + s2r__md_bytes(scls)
                            + (any_stride? s2r__md_bytes(tcls):0));
        out[nout].block=blk; out[nout].bytes=total+meta; out[nout].nblocks=nb;
        out[nout].nconst=nconst; out[nout].has_stride=any_stride;
        nout++;
        if(blk>=n) break;
    }
    free(cell);
    return nout;
}

/* The block size that minimises resident bytes. Larger blocks are also faster on
 * ordered data (fewer metadata walks, and the binary search gets its threshold),
 * so ties break UPWARD - the free direction. */
static inline size_t s2r_blocked_choose_block(const uint64_t *su, const int64_t *ss,
                                              size_t n, int is_signed){
    S2RBlockPlan pl[S2R_PLAN_MAX];
    int k=s2r_blocked_plan(su,ss,n,is_signed,pl,S2R_PLAN_MAX);
    if(k<=0) return S2R_BLOCK_DEFAULT;
    size_t best=pl[0].block, bb=pl[0].bytes;
    for(int i=1;i<k;i++) if(pl[i].bytes<=bb){ bb=pl[i].bytes; best=pl[i].block; }
    return best;
}
static inline int s2r_blocked_build_auto(S2RBlocked *b, const uint64_t *src, size_t n){
    return s2r_blocked_build(b,src,n,s2r_blocked_choose_block(src,NULL,n,0));
}
static inline int s2r_blocked_build_signed_auto(S2RBlocked *b, const int64_t *src, size_t n){
    return s2r_blocked_build_signed(b,src,n,s2r_blocked_choose_block(NULL,src,n,1));
}

/* ---- which representation, and the promise that none of them expands -------
 *
 * A caller who does the obvious thing reaches for S2RPool, and on a
 * time-partitioned column that is the WORST of the three: measured on 4M
 * timestamps, the flat pool is 15.26 MB and 0.73 ms where the block-wise form is
 * 4.11 MB and 0.04 ms. The library knew which was better and never said so.
 *
 * The guarantee underneath is worth stating because the peers do not have it:
 * every classical alternative has a regime where it EXPANDS the data. Dictionary
 * encoding on a high-cardinality column stores a dictionary the size of the data
 * (measured: 41.01 MB for a 30.52 MB int64 column). RLE on unordered data stores
 * one run per value. Smart2Raw cannot expand, and not by luck: it classifies by
 * RANGE, so the worst case is "the range needs 64 bits", which IS the int64
 * input. The widest class is the baseline. */
typedef struct {
    size_t raw_bytes;     /* the int64 baseline                  */
    size_t flat_bytes;
    size_t affine_bytes;
    size_t blocked_bytes;
    size_t block;         /* block size the planner chose        */
    size_t best_bytes;
    const char *best;     /* "flat" | "affine" | "blocked"       */
} S2RAdvice;

static inline int s2r_recommend(const uint64_t *v, size_t n, S2RAdvice *out){
    if(!v||!out) return 0;
    memset(out,0,sizeof *out);
    out->raw_bytes=n*8;
    out->flat_bytes = n * (s2r_abs_size(s2r_classify_array(v,n))>>3);
    S2RAffine af;
    out->affine_bytes = s2r_affine_build(&af,v,n) ? s2r_affine_bytes(&af) : out->flat_bytes;
    s2r_affine_free(&af);
    out->block = s2r_blocked_choose_block(v,NULL,n,0);
    S2RBlocked b;
    out->blocked_bytes = s2r_blocked_build(&b,v,n,out->block) ? s2r_blocked_bytes(&b)
                                                             : out->flat_bytes;
    s2r_blocked_free(&b);
    out->best_bytes=out->flat_bytes; out->best="flat";
    if(out->affine_bytes < out->best_bytes){ out->best_bytes=out->affine_bytes; out->best="affine"; }
    if(out->blocked_bytes< out->best_bytes){ out->best_bytes=out->blocked_bytes;out->best="blocked"; }
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* SMART2RAW_V3_3_6_H */
