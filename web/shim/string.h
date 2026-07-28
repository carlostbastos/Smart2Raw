/* Freestanding shim: only what smart2raw.h actually asks <string.h> for. */
#ifndef S2R_SHIM_STRING_H
#define S2R_SHIM_STRING_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
#ifdef __cplusplus
}
#endif
#endif
