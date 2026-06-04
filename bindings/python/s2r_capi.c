/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/*
 * s2r_capi.c - Stable Smart2Raw C ABI for bindings (Python/ctypes/cffi).
 *
 * The header functions are `static inline` and do not appear in a shared
 * library's symbol table; this file exposes them behind a flat API with an
 * opaque S2RPool* pointer.
 *
 * Build (POSIX):   cc -O2 -shared -fPIC -I ../include s2r_capi.c -o libsmart2raw.so
 * Build (Windows): gcc -O2 -shared -I ../include s2r_capi.c -o smart2raw.dll
 *                  (or use build_lib.py, which detects the platform)
 */
#include <stdlib.h>
#include "smart2raw.h"

#if defined(_WIN32)
  #define S2R_API __declspec(dllexport)
#else
  #define S2R_API __attribute__((visibility("default")))
#endif

/* --- lifecycle --- */
S2R_API S2RPool *s2r_capi_new(int size_bits, size_t capacity) {
    S2RPool *p = (S2RPool *)malloc(sizeof(S2RPool));
    if (!p) return NULL;
    if (!s2r_pool_init(p, (int8_t)size_bits, capacity)) { free(p); return NULL; }
    return p;
}
S2R_API void s2r_capi_free(S2RPool *p) { if (p) { s2r_pool_free(p); free(p); } }

/* --- adaptive insertion (grows the class, never truncates) --- */
S2R_API int s2r_capi_push_adaptive(S2RPool *p, uint64_t v)        { return (int)s2r_push_adaptive(p, v); }
S2R_API int s2r_capi_push_signed_adaptive(S2RPool *p, int64_t v)  { return (int)s2r_push_signed_adaptive(p, v); }

/* --- access --- */
S2R_API uint64_t s2r_capi_get(const S2RPool *p, size_t i)         { return s2r_get(p, i); }
S2R_API int64_t  s2r_capi_get_signed(const S2RPool *p, size_t i)  { return s2r_get_signed(p, i); }

/* --- metadata --- */
S2R_API size_t s2r_capi_count(const S2RPool *p)      { return p ? p->count : 0; }
S2R_API int    s2r_capi_class_bits(const S2RPool *p) { return p ? (int)p->size : 0; }   /* negative = signed */
S2R_API size_t s2r_capi_used_bytes(const S2RPool *p) { return p ? s2r_used_bytes(p) : 0; }

/* --- reductions --- */
S2R_API uint64_t s2r_capi_sum(const S2RPool *p)       { return s2r_sum(p); }
S2R_API uint64_t s2r_capi_sum_fast(const S2RPool *p)  { return s2r_sum_fast(p); }
S2R_API uint64_t s2r_capi_min(const S2RPool *p)       { return s2r_min(p); }
S2R_API uint64_t s2r_capi_max(const S2RPool *p)       { return s2r_max(p); }
S2R_API int64_t  s2r_capi_min_signed(const S2RPool *p){ return s2r_min_signed_val(p); }
S2R_API int64_t  s2r_capi_max_signed(const S2RPool *p){ return s2r_max_signed_val(p); }

/* --- overflow-safe arithmetic (lazy-carry; returns the new class in bits) --- */
S2R_API int s2r_capi_add_scalar_safe(S2RPool *p, uint64_t s)         { return s2r_add_scalar_safe(p, s); }
S2R_API int s2r_capi_mul_scalar_safe(S2RPool *p, uint64_t s)         { return s2r_mul_scalar_safe(p, s); }
S2R_API int s2r_capi_add_scalar_signed_safe(S2RPool *p, int64_t s)   { return s2r_add_scalar_signed_safe(p, s); }
S2R_API int s2r_capi_mul_scalar_signed_safe(S2RPool *p, int64_t s)   { return s2r_mul_scalar_signed_safe(p, s); }

/* --- portable persistence (.s2r) --- */
S2R_API int s2r_capi_save(const S2RPool *p, const char *path) { return (int)s2r_save_portable(p, path); }
S2R_API S2RPool *s2r_capi_load(const char *path) {
    S2RPool *p = (S2RPool *)malloc(sizeof(S2RPool));
    if (!p) return NULL;
    if (s2r_load_portable(p, path) != S2R_OK) { free(p); return NULL; }
    return p;
}

/* --- misc --- */
S2R_API const char *s2r_capi_version(void)        { return S2R_VERSION_STRING; }
S2R_API const char *s2r_capi_strerror(int code)   { return s2r_strerror((S2RError)code); }
