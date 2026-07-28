/* Freestanding shim: only what smart2raw.h actually asks <stdlib.h> for.
 * Implementations live in s2r_rt.c. */
#ifndef S2R_SHIM_STDLIB_H
#define S2R_SHIM_STDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void  *malloc(size_t n);
void  *calloc(size_t n, size_t sz);
void  *realloc(void *p, size_t n);
void   free(void *p);
void  *aligned_alloc(size_t align, size_t size);
void   qsort(void *base, size_t n, size_t sz,
             int (*cmp)(const void *, const void *));
int    abs(int v);
void   exit(int code);
void   abort(void);
#ifdef __cplusplus
}
#endif
#endif
