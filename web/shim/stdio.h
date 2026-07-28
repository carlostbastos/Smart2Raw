/* Freestanding shim: a <stdio.h> whose FILE is a block of memory.
 *
 * This is what lets the REAL s2r_blocked_save()/s2r_blocked_load() run inside a
 * browser: the serializer is not modified or reimplemented, it just writes its
 * bytes into linear memory instead of onto a disk. The bytes the page offers for
 * download are the bytes the library wrote. */
#ifndef S2R_SHIM_STDIO_H
#define S2R_SHIM_STDIO_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct S2R_MEMFILE {
    unsigned char *buf;
    size_t         len;      /* bytes written                */
    size_t         cap;      /* bytes allocated              */
    size_t         pos;      /* read/write cursor            */
    int            writing;
    int            err;
    const char    *name;
    int            open;
} FILE;

extern FILE *stderr;
extern FILE *stdout;

FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *f);
size_t fread(void *p, size_t sz, size_t n, FILE *f);
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f);
int    fseek(FILE *f, long off, int whence);
long   ftell(FILE *f);
int    fflush(FILE *f);
int    feof(FILE *f);
int    ferror(FILE *f);
int    fprintf(FILE *f, const char *fmt, ...);
int    printf(const char *fmt, ...);
int    puts(const char *s);
int    remove(const char *path);
int    fgetc(FILE *f);
int    getc(FILE *f);
int    fputc(int c, FILE *f);

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifdef __cplusplus
}
#endif
#endif
