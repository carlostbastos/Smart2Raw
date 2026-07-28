/* Freestanding shim: no Windows a smart2raw.h pede _aligned_malloc daqui. */
#ifndef S2R_SHIM_MALLOC_H
#define S2R_SHIM_MALLOC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *_aligned_malloc(size_t size, size_t align);
void  _aligned_free(void *p);
#ifdef __cplusplus
}
#endif
#endif
