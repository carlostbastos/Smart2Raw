/*
 * Smart2Raw v3.3.5 - Self-adaptive numeric storage (header-only)
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * =======================================================================
 *
 * A SINGLE-FILE C library, zero external dependencies. Stores integer arrays
 * in the SMALLEST native class that fits the real range (signed/unsigned
 * +-8/16/32/64 bits) and operates directly on the compact form.
 *
 * Principle: classify once, always operate on the most compact form.
 *
 * --- CONTENTS (sections of this file, in order) ---
 *   VERSION  ·  CONFIGURATION  ·  ENDIANNESS  ·  PLATFORM/FEATURE DETECTION
 *   ERROR CODES  ·  TYPES  ·  ALIGNED ALLOC  ·  HELPERS  ·  VALUE LIMITS
 *   CLASSIFICATION  ·  POOL MANAGEMENT  ·  TYPED ACCESS  ·  PUSH VARIANTS
 *   CLASS PROMOTION/DEMOTION  ·  AGGREGATIONS  ·  STATISTICS
 *   SIGNED-AWARE AGGREGATIONS/FILTERS/STATS  ·  FILTERS AND COUNTS
 *   ARITHMETIC (includes signed/unsigned lazy-carry)  ·  BITWISE
 *   STREAMING/CONVERSION  ·  UTILITY  ·  DIAGNOSTICS  ·  ITERATOR MACRO
 *   SERIALIZATION (canonical LE + CRC32)  ·  zero-copy mmap (COW big-endian)
 *   SIMD (runtime dispatch: AVX2 vpsadbw / NEON)  ·  ERROR HANDLING
 *   ANALYTICS MODULE: bidirectional width (self-healing), S2RTracked, group-by
 *   BLOCK-WISE WIDTH MODULE (PFOR): S2RBlocked - per-block class
 *
 * --- ADAPTABILITY (compile-time gates) ---
 *   -DS2R_NO_STDIO   removes file serialization
 *   -DS2R_NO_MMAP    removes memory mapping
 *   -DS2R_NO_SIMD    removes SIMD dispatch (uses the scalar path)
 *   MCU footprint (all gates on): ~3.4 KB of code.
 *
 * --- CHANGELOG (mais recente primeiro) ---
 * v3.3.5: fix - class promotion on an empty pool (count==0) did not re-fit the
 *         capacity to the already-allocated buffer (could overflow if the first
 *         value required a wider class). Found with ASan; regression test added.
 * v3.3.4: signed PFOR (s2r_blocked_build/get/sum_signed) and SIMD-accelerated
 *         block sum (s2r_blocked_sum_fast: each block reuses the vpsadbw/NEON
 *         dispatch in its own native type).
 * v3.3.3: block-wise width (PFOR) - S2RBlocked: each block picks its own class;
 *         an outlier inflates only its own block (recovers ~3.7x of memory under
 *         localized outliers). API: s2r_blocked_build/get/sum/max/bytes/free.
 *         Scope: unsigned.
 * v3.3.2: Analytics module merged into the single header - bidirectional width
 *         (s2r_remove_swap, s2r_fit_class/self-healing), S2RTracked (O(1) min/max
 *         on push) and group-by on the compact form (s2r_histogram_u8,
 *         s2r_group_sum_u8u32).
 * v3.3.1: SIGNED lazy-carry arithmetic (s2r_add/mul_scalar_signed_safe,
 *         S2RDeferredSigned); NEON path (ARM); big-endian mmap via
 *         copy-on-write (on-disk file untouched).
 * v3.3.0: self-adaptive push (s2r_push_adaptive); SIMD with runtime dispatch
 *         (AVX2 vpsadbw, scalar fallback; s2r_sum_fast); zero-copy mmap
 *         (s2r_map_open/close); portable I/O (canonical LE + CRC32).
 * v3.2.1: s2r_stddev() fixed (robust s2r_sqrt, no math.h); aligned allocation
 *         via aligned_alloc (C11); SIGNED-AWARE aggregations/filters/statistics
 *         (the non-_signed versions read bytes as unsigned and gave wrong
 *         results on signed pools).
 * v3.2.0: signed integers (S2R_I8..I64); promote/demote; statistics;
 *         range queries; push_many/transform; S2R_FOREACH; s2r_info.
 *
 * Honest note: locally, the NEON and big-endian paths are exercised in an
 * ACLE-faithful EMULATED environment (the real code runs on x86). CI also runs
 * the test suite on real linux/arm64 (NEON) and real linux/s390x (big-endian)
 * via QEMU.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Dual-licensed: AGPL-3.0-or-later OR commercial. See LICENSING.md.
 */

#ifndef SMART2RAW_V3_3_5_H
#define SMART2RAW_V3_3_5_H

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
#define S2R_VERSION_MINOR 3
#define S2R_VERSION_PATCH 5
#define S2R_VERSION_STRING "3.3.5"

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
 * Allows lean builds for edge/MCU: define S2R_NO_STDIO (no files) and/or
 * S2R_NO_SIMD (no SIMD dispatch). mmap on POSIX only.
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
    S2R_FLAG_EXTERNAL = 1 << 2   /* Data owned externally, don't free */
} S2RFlags;

typedef struct {
    uint8_t  *data;      /* alias-safe buffer */
    size_t    byte_cap;  /* allocated bytes */
    size_t    count;     /* used elements */
    size_t    capacity;  /* capacity in elements */
    int8_t    size;      /* bits per element: ±8/16/32/64 (negative = signed) */
    uint8_t   flags;     /* S2RFlags */
    uint16_t  _reserved; /* padding for alignment */
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
   * compiles cleanly under strict -std=c11. Requires size to be a multiple of
   * align, which this library always guarantees (s2r_align_up to S2R_ALIGNMENT). */
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

/* Empty pool: when the class changes, reinterpret the capacity (in elements)
 * for the ALREADY-allocated buffer, avoiding overflow when the first value
 * requires a wider class than the initial one. (fixes the count==0 promotion bug) */
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
    int64_t sum = 0;
    const size_t n = p->count;
    
    switch (p->size) {
        case S2R_I8: {
            const int8_t *a = (const int8_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += a[i];
            break;
        }
        case S2R_I16: {
            const int16_t *a = (const int16_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += a[i];
            break;
        }
        case S2R_I32: {
            const int32_t *a = (const int32_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += a[i];
            break;
        }
        case S2R_I64: {
            const int64_t *a = (const int64_t*)p->data;
            for (size_t i = 0; i < n; i++) sum += a[i];
            break;
        }
        default:
            return (int64_t)s2r_sum(p);
    }
    return sum;
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

/* Square root with no math.h dependency.
 * Normalizes v to [0.25, 4) by powers of 4 (exact scaling in binary), runs
 * Newton-Raphson (quadratic convergence in that interval) and rescales.
 * Robust over the full double range, including very large variances. */
static inline double s2r_sqrt(double v) {
    if (v <= 0.0) return 0.0;
    double scale = 1.0;
    while (v >= 4.0)  { v *= 0.25; scale *= 2.0; }
    while (v < 0.25)  { v *= 4.0;  scale *= 0.5; }
    double x = (v + 1.0) * 0.5;  /* seed em [~0.31, 2.5) */
    for (int i = 0; i < 8; i++) x = 0.5 * (x + v / x);
    return x * scale;
}

static inline double s2r_stddev(const S2RPool *p) {
    return s2r_sqrt(s2r_variance(p));
}

/* ============================================================================
 * SIGNED-AWARE AGGREGATIONS / FILTERS / STATS (NEW in v3.2.1)
 * ----------------------------------------------------------------------------
 * The non-suffixed versions (_signed) read the bytes as UNSIGNED and give
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
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t v = s2r_get_signed(p, i);
        if (v >= min_v && v <= max_v) sum += v;
    }
    return sum;
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
    const size_t n = p->count;
    
    switch (s2r_abs_size(p->size)) {
        case 8:  { uint8_t  *a=(uint8_t*)p->data;  const uint8_t  v=(uint8_t)s;  for(size_t i=0;i<n;i++) a[i]*=v; break; }
        case 16: { uint16_t *a=(uint16_t*)p->data; const uint16_t v=(uint16_t)s; for(size_t i=0;i<n;i++) a[i]*=v; break; }
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
    if (p) p->count = 0;
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

#define S2R_MAGIC_V33 0x33335253u  /* portable v3.3 format (canonical LE + CRC) */

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
    uint8_t hdr[4]={ (uint8_t)p->size, p->flags, 1, 0 };
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
    uint8_t flags=(is_v33||magic==S2R_MAGIC_V32)?hdr[1]:0;
    uint64_t count; if(!s2r__rd_le64(f,&count)){ fclose(f); return S2R_ERR_IO; }
    size_t abs=s2r_abs_size(size), eb=abs>>3;
    if(!(abs==8||abs==16||abs==32||abs==64)){ fclose(f); return S2R_ERR_CORRUPT; }
    if(eb && count>SIZE_MAX/eb){ fclose(f); return S2R_ERR_CORRUPT; }
    if(!s2r_pool_init(p,size,(size_t)count)){ fclose(f); return S2R_ERR_OOM; }
    p->flags=flags;
    size_t bytes=(size_t)count*eb;
    if(bytes){ if(fread(p->data,bytes,1,f)!=1){ s2r_pool_free(p); fclose(f); return S2R_ERR_IO; } }
    if(is_v33){
        uint32_t crc_disk; if(!s2r__rd_le32(f,&crc_disk)){ s2r_pool_free(p); fclose(f); return S2R_ERR_IO; }
        uint32_t crc_calc=bytes? s2r_crc32(p->data,bytes,0):0;
        if(crc_calc!=crc_disk){ s2r_pool_free(p); fclose(f); return S2R_ERR_CORRUPT; }
    }
    p->count=(size_t)count; fclose(f);
#if !S2R_LITTLE_ENDIAN
    if(eb>1) s2r_swap_payload(p->data,eb,p->count);
#endif
    return S2R_OK;
}
#endif /* S2R_HAS_STDIO */

/* ============================================================================
 * ZERO-COPY MMAP (NEW in v3.3)  -  acesso a datasets sem copiar p/ RAM
 * ----------------------------------------------------------------------------
 * Ideal for the edge: opens large files from storage, paged on demand by the
 * kernel. The resulting pool is READ-ONLY and points INSIDE the mapping.
 * Free it with s2r_map_close (do not use s2r_pool_free).
 * On a big-endian host, multibyte uses s2r_load_portable (copy + swap).
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
    int8_t size=(int8_t)b[4]; uint8_t flags=b[5];
    uint64_t count=0; for(int i=0;i<8;i++) count|=(uint64_t)b[8+i]<<(8*i);
    size_t abs=s2r_abs_size(size), eb=abs>>3;
    if(!(abs==8||abs==16||abs==32||abs==64)){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    if(eb && count>SIZE_MAX/eb){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    size_t bytes=(size_t)count*eb;
    if((uint64_t)16u+bytes+4u > fsz){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    const uint8_t *payload=b+16;
    if(verify_crc){
        uint32_t cd=(uint32_t)payload[bytes]|((uint32_t)payload[bytes+1]<<8)|((uint32_t)payload[bytes+2]<<16)|((uint32_t)payload[bytes+3]<<24);
        uint32_t cc=bytes? s2r_crc32(payload,bytes,0):0;
        if(cd!=cc){ munmap(base,fsz); close(fd); return S2R_ERR_CORRUPT; }
    }
#if !S2R_LITTLE_ENDIAN
    /* Big-endian host: the file is canonical LE. Convert in place via COW
     * (MAP_PRIVATE) without modifying the on-disk file. No longer zero-copy on
     * the touched pages, but stays portable. CRC already validated above over
     * the canonical LE bytes (correct on any host). */
    if(eb>1){
        if(mprotect(base,fsz,PROT_READ|PROT_WRITE)!=0){ munmap(base,fsz); close(fd); return S2R_ERR_IO; }
        s2r_swap_payload((void*)(uintptr_t)payload, eb, (size_t)count);
    }
#endif
    m->_base=base; m->_len=fsz; m->_fd=fd;
    m->pool.data=(uint8_t*)(uintptr_t)payload;
    m->pool.size=size;
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
 * SIMD SUM com DISPATCH EM RUNTIME (NEW in v3.3)
 * ----------------------------------------------------------------------------
 * A single binary: uses AVX2 if the CPU supports it (the target attribute lets
 * it compile without a global -mavx2), otherwise scalar. On non-x86 ISAs, scalar.
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
static inline int s2r_has_avx2(void){ return __builtin_cpu_supports("avx2"); }
#else
static inline int s2r_has_avx2(void){ return 0; }
#endif /* S2R_X86_SIMD */

/* ---- NEON (ARM). Written per the standard intrinsics; NOT compiled/tested
 *      here for lack of an ARM toolchain. On aarch64 NEON is mandatory, so no
 *      runtime check is needed. The scalar fallback guarantees correctness if
 *      NEON is disabled (S2R_NO_SIMD). ---- */
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

/* sum acelerado, mesmo resultado de s2r_sum(). */
static inline uint64_t s2r_sum_fast(const S2RPool *p){
    if(!p||p->count==0) return 0;
#if S2R_X86_SIMD
    if(s2r_has_avx2()){
        switch(s2r_abs_size(p->size)){
            case 8:  return s2r__sum_u8_avx2((const uint8_t*)p->data,p->count);
            case 16: return s2r__sum_u16_avx2((const uint16_t*)p->data,p->count);
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
    return s2r_sum(p);
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
 * LAZY-CARRY ARITHMETIC (NEW in v3.3)  -  promove uma vez, sem wraparound
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
 * For signed values, add/mul can move the range in both directions, so we track
 * the result's [min,max] and size the class that fits BOTH.
 * ============================================================================ */

/* overflow-safe int64 (usa builtins do gcc/clang; senao, checagem manual) */
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

/* Smallest SIGNED class that fits the range [lo,hi]. */
static inline int8_t s2r_classify_signed_range(int64_t lo, int64_t hi){
    int8_t a=s2r_classify_signed(lo), b=s2r_classify_signed(hi);
    return (a<b)?a:b;  /* more-negative enum = wider class */
}

/* Promote (signed) to fit [lo,hi] if needed. Returns bits or 0. */
static inline int s2r_ensure_fits_signed(S2RPool *p, int64_t lo, int64_t hi){
    if(!p || !s2r_is_signed(p)) return 0;
    int8_t req=s2r_classify_signed_range(lo,hi);
    size_t cur=s2r_abs_size(p->size), need=s2r_abs_size(req);
    if(need<=cur) return (int)cur;            /* nunca rebaixa */
    if(p->count==0){ p->size=req; s2r__recap_empty(p); return (int)need; }
    if(!s2r_promote(p,req)) return 0;
    return (int)need;
}

/* signed scalar add WITHOUT wrap: promotes the class per the new range. */
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

/* signed scalar mul WITHOUT wrap. */
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

/* ---- Signed deferred session: accumulates the chain's [vmin,vmax], promotes once. ---- */
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
 * (self-healing), S2RTracked and group-by on the compact form.
 * =================================================================== */
#include <string.h>
/* ============================================================================
 * #1  LARGURA BIDIRECIONAL
 * ============================================================================ */

/* Reclassify to any class (up or down, signed or unsigned). */
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

/* Remocao O(1) sem ordem: move o ultimo para o slot i. */
static inline void s2r_remove_swap(S2RPool *p, size_t i){
    if(!p || i>=p->count) return;
    size_t last=p->count-1;
    if(i!=last){
        if(s2r_is_signed(p)) s2r_set_signed(p,i,s2r_get_signed(p,last));
        else                 s2r_set(p,i,s2r_get(p,last));
    }
    p->count--;
}

/* Self-healing: lowers to the SMALLEST class that fits the current data. */
static inline int s2r_fit_class(S2RPool *p){
    if(!p || p->count==0) return 1;
    int8_t target;
    if(s2r_is_signed(p)){ int64_t lo=s2r_min_signed_val(p), hi=s2r_max_signed_val(p);
                          target=s2r_classify_signed_range(lo,hi); }
    else { target=(int8_t)s2r_classify(s2r_max(p)); }
    if(s2r_abs_size(target) >= s2r_abs_size(p->size)) return 1; /* ja minimo */
    return s2r__reclass(p, target);
}

/* ---- Pool com intervalo embutido: min/max em O(1) no push ---- */
typedef struct {
    S2RPool p;
    uint64_t umin, umax;   /* range rastreado (unsigned) */
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
/* range O(1) quando limpo; rescaneia so se sujo (apos remocao). */
static inline void s2r_tracked_range(S2RTracked *t, uint64_t *mn, uint64_t *mx){
    if(t->dirty && t->p.count){ t->umin=s2r_min(&t->p); t->umax=s2r_max(&t->p); t->dirty=0; }
    *mn = t->p.count? t->umin : 0; *mx = t->p.count? t->umax : 0;
}
static inline void s2r_tracked_free(S2RTracked *t){ s2r_pool_free(&t->p); }

/* ============================================================================
 * #3  GROUP-BY DIRETO NO FORMATO COMPACTO (chaves u8, <=256 grupos)
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

/* GROUP BY key SUM(val) : sum per group. keys=u8, vals=u32. */
static inline void s2r_group_sum_u8u32(const S2RPool *keys, const S2RPool *vals, uint64_t gsum[256]){
    memset(gsum,0,256*sizeof(uint64_t));
    if(!keys||!vals||keys->count!=vals->count||s2r_abs_size(keys->size)!=8||s2r_abs_size(vals->size)!=32) return;
    const uint8_t  *k=(const uint8_t*)keys->data;
    const uint32_t *v=(const uint32_t*)vals->data;
    size_t n=keys->count;
    for(size_t i=0;i<n;i++) gsum[k[i]] += v[i];
}


/* ===================================================================
 * BLOCK-WISE WIDTH MODULE (PFOR) — NEW in v3.3.3
 * Partitions the array into fixed-size blocks; each block picks the SMALLEST
 * class that fits its own range. An outlier inflates only its own block,
 * instead of dragging the whole collection up to a wider class.
 * Current scope: unsigned. Read/aggregate on the compact form.
 * =================================================================== */
#ifndef S2R_BLOCK_DEFAULT
#define S2R_BLOCK_DEFAULT 256
#endif

typedef struct {
    uint8_t *data;      /* contiguous payload of all blocks            */
    size_t   bytes;     /* payload bytes used (includes padding)       */
    uint8_t *bclass;    /* class (8/16/32/64) of each block            */
    size_t  *boff;      /* byte offset of each block in the payload    */
    size_t   nblocks;
    size_t   count;     /* total number of elements                    */
    size_t   block;     /* block size (elements)                       */
    int      is_signed; /* 0 = unsigned, 1 = signed                    */
} S2RBlocked;

/* Build the block-wise representation from a uint64 array (unsigned).
 * Each block is aligned to 8 bytes for safe typed access. Returns 1 on ok. */
static inline int s2r_blocked_build(S2RBlocked *b, const uint64_t *src, size_t n, size_t block){
    if(!b) return 0;
    if(n && !src) return 0;
    if(block==0) block=S2R_BLOCK_DEFAULT;
    memset(b,0,sizeof *b);
    b->block=block; b->count=n;
    b->nblocks = (n + block - 1) / block;
    if(b->nblocks){
        b->bclass=(uint8_t*)malloc(b->nblocks);
        b->boff=(size_t*)malloc(b->nblocks*sizeof(size_t));
        if(!b->bclass || !b->boff){ free(b->bclass); free(b->boff); memset(b,0,sizeof *b); return 0; }
    }
    /* pass 1: classify each block, compute aligned offsets and total */
    size_t total=0;
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t start=bi*block, len=(start+block<=n)?block:(n-start);
        uint64_t mx=0;
        for(size_t i=0;i<len;i++) if(src[start+i]>mx) mx=src[start+i];
        int sz=(int)s2r_classify(mx);              /* 8/16/32/64 */
        size_t off=s2r_align_up(total, 8);          /* align the block */
        b->bclass[bi]=(uint8_t)sz;
        b->boff[bi]=off;
        total = off + len*(size_t)(sz/8);
    }
    if(total){
        b->data=(uint8_t*)S2R_ALIGNED_ALLOC(s2r_align_up(total,S2R_ALIGNMENT),S2R_ALIGNMENT);
        if(!b->data){ free(b->bclass); free(b->boff); memset(b,0,sizeof *b); return 0; }
    }
    b->bytes=total;
    /* pass 2: write each value in its block's type */
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t start=bi*block, len=(start+block<=n)?block:(n-start);
        uint8_t *d=b->data+b->boff[bi]; int sz=b->bclass[bi];
        for(size_t i=0;i<len;i++){ uint64_t v=src[start+i];
            switch(sz){ case 8:  d[i]=(uint8_t)v; break;
                        case 16: ((uint16_t*)d)[i]=(uint16_t)v; break;
                        case 32: ((uint32_t*)d)[i]=(uint32_t)v; break;
                        default: ((uint64_t*)d)[i]=v; break; } }
    }
    return 1;
}

static inline uint64_t s2r_blocked_get(const S2RBlocked *b, size_t i){
    if(!b || i>=b->count) return 0;
    size_t bi=i/b->block, within=i%b->block;
    const uint8_t *d=b->data+b->boff[bi];
    switch(b->bclass[bi]){ case 8:  return d[within];
                           case 16: return ((const uint16_t*)d)[within];
                           case 32: return ((const uint32_t*)d)[within];
                           default: return ((const uint64_t*)d)[within]; }
}

static inline uint64_t s2r_blocked_sum(const S2RBlocked *b){
    uint64_t s=0; if(!b) return 0;
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t start=bi*b->block, len=(start+b->block<=b->count)?b->block:(b->count-start);
        const uint8_t *d=b->data+b->boff[bi]; int sz=b->bclass[bi];
        for(size_t i=0;i<len;i++)
            switch(sz){ case 8:  s+=d[i]; break;
                        case 16: s+=((const uint16_t*)d)[i]; break;
                        case 32: s+=((const uint32_t*)d)[i]; break;
                        default: s+=((const uint64_t*)d)[i]; break; }
    }
    return s;
}

static inline uint64_t s2r_blocked_max(const S2RBlocked *b){
    uint64_t m=0; if(!b) return 0;
    for(size_t i=0;i<b->count;i++){ uint64_t v=s2r_blocked_get(b,i); if(v>m)m=v; }
    return m;
}

/* Storable footprint: payload (with padding) + 1 class byte per block.
 * Offsets are derivable from the classes + block size, so they don't count. */
static inline size_t s2r_blocked_bytes(const S2RBlocked *b){
    return b ? b->bytes + b->nblocks : 0;
}

static inline void s2r_blocked_free(S2RBlocked *b){
    if(!b) return;
    if(b->data) S2R_ALIGNED_FREE(b->data);
    free(b->bclass); free(b->boff);
    memset(b,0,sizeof *b);
}


/* ---- Signed PFOR + SIMD-accelerated block sum — NEW in v3.3.4 ---- */

/* Build the block-wise representation from an int64 array (signed).
 * Each block picks the smallest SIGNED class that covers its [min,max]. */
static inline int s2r_blocked_build_signed(S2RBlocked *b, const int64_t *src, size_t n, size_t block){
    if(!b) return 0;
    if(n && !src) return 0;
    if(block==0) block=S2R_BLOCK_DEFAULT;
    memset(b,0,sizeof *b);
    b->block=block; b->count=n; b->is_signed=1;
    b->nblocks=(n+block-1)/block;
    if(b->nblocks){
        b->bclass=(uint8_t*)malloc(b->nblocks);
        b->boff=(size_t*)malloc(b->nblocks*sizeof(size_t));
        if(!b->bclass||!b->boff){ free(b->bclass); free(b->boff); memset(b,0,sizeof *b); return 0; }
    }
    size_t total=0;
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t start=bi*block, len=(start+block<=n)?block:(n-start);
        int64_t lo=src[start], hi=src[start];
        for(size_t i=1;i<len;i++){ int64_t v=src[start+i]; if(v<lo)lo=v; if(v>hi)hi=v; }
        int sz=s2r_abs_size(s2r_classify_signed_range(lo,hi));   /* 8/16/32/64 */
        size_t off=s2r_align_up(total,8);
        b->bclass[bi]=(uint8_t)sz; b->boff[bi]=off;
        total = off + len*(size_t)(sz/8);
    }
    if(total){
        b->data=(uint8_t*)S2R_ALIGNED_ALLOC(s2r_align_up(total,S2R_ALIGNMENT),S2R_ALIGNMENT);
        if(!b->data){ free(b->bclass); free(b->boff); memset(b,0,sizeof *b); return 0; }
    }
    b->bytes=total;
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t start=bi*block, len=(start+block<=n)?block:(n-start);
        uint8_t *d=b->data+b->boff[bi]; int sz=b->bclass[bi];
        for(size_t i=0;i<len;i++){ int64_t v=src[start+i];
            switch(sz){ case 8:  ((int8_t*)d)[i]=(int8_t)v; break;
                        case 16: ((int16_t*)d)[i]=(int16_t)v; break;
                        case 32: ((int32_t*)d)[i]=(int32_t)v; break;
                        default: ((int64_t*)d)[i]=v; break; } }
    }
    return 1;
}

static inline int64_t s2r_blocked_get_signed(const S2RBlocked *b, size_t i){
    if(!b || i>=b->count) return 0;
    size_t bi=i/b->block, within=i%b->block;
    const uint8_t *d=b->data+b->boff[bi];
    switch(b->bclass[bi]){ case 8:  return ((const int8_t*)d)[within];
                           case 16: return ((const int16_t*)d)[within];
                           case 32: return ((const int32_t*)d)[within];
                           default: return ((const int64_t*)d)[within]; }
}

static inline int64_t s2r_blocked_sum_signed(const S2RBlocked *b){
    int64_t s=0; if(!b) return 0;
    for(size_t i=0;i<b->count;i++) s += s2r_blocked_get_signed(b,i);
    return s;
}

/* Accelerated block sum: each block becomes a "view" S2RPool and reuses the
 * SIMD dispatch (s2r_sum_fast) in the block's native type (u8 -> vpsadbw, etc).
 * Per-block memory + fast reduction in the same pass. Unsigned only. */
static inline uint64_t s2r_blocked_sum_fast(const S2RBlocked *b){
    uint64_t s=0; if(!b || b->is_signed) return s2r_blocked_sum(b);
    for(size_t bi=0; bi<b->nblocks; bi++){
        size_t start=bi*b->block, len=(start+b->block<=b->count)?b->block:(b->count-start);
        S2RPool view; memset(&view,0,sizeof view);
        view.data=b->data+b->boff[bi];
        view.size=(int8_t)b->bclass[bi];
        view.count=len; view.capacity=len;
        view.byte_cap=len*(size_t)(b->bclass[bi]/8);
        view.flags=S2R_FLAG_EXTERNAL;
        s += s2r_sum_fast(&view);
    }
    return s;
}

#ifdef __cplusplus
}
#endif

#endif /* SMART2RAW_V3_3_5_H */
