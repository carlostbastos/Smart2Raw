/*
 * s2r_rt.c - the C runtime that smart2raw.h needs, and nothing else, so the
 * header can be compiled for a target with no libc at all: wasm32 for the
 * browser, or a Windows PE linked without the CRT.
 *
 * Three pieces:
 *   1. a boundary-tag allocator with coalescing, aligned to S2R_ALIGNMENT (64)
 *      so aligned_alloc() is just malloc() and free() works on both
 *   2. the mem/str primitives and qsort
 *   3. a <stdio.h> whose FILE is a block of memory, so the REAL
 *      s2r_blocked_save()/s2r_blocked_load() run unmodified and the bytes the
 *      page offers for download are the bytes the library wrote
 *
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stddef.h>
#include <stdint.h>
#include "stdlib.h"
#include "string.h"
#include "stdio.h"

/* ==========================================================================
 * 1. system memory
 * ========================================================================== */

#if defined(__wasm__)
extern unsigned char __heap_base;
static unsigned char *sys_base(void){ return &__heap_base; }
static unsigned char *sys_end(void){
    return (unsigned char*)((size_t)__builtin_wasm_memory_size(0) << 16);
}
static int sys_grow(size_t bytes){
    size_t pages = (bytes + 65535u) >> 16;
    return (size_t)__builtin_wasm_memory_grow(0, pages) != (size_t)-1;
}

#elif defined(_WIN32)
__declspec(dllimport) void* __stdcall VirtualAlloc(void*, size_t,
                                                   unsigned long, unsigned long);
#define RT_RESERVE (1024u*1024u*1024u)          /* reserved, not committed */
static unsigned char *rt_res, *rt_com;
static unsigned char *sys_base(void){
    if(!rt_res){
        rt_res = (unsigned char*)VirtualAlloc(0, RT_RESERVE, 0x2000u, 0x04u);
        rt_com = rt_res;
    }
    return rt_res;
}
static unsigned char *sys_end(void){ sys_base(); return rt_com; }
static int sys_grow(size_t bytes){
    unsigned char *p;
    if(!sys_base()) return 0;
    bytes = (bytes + 0xFFFFFu) & ~(size_t)0xFFFFFu;      /* 1 MB steps */
    if(rt_com + bytes > rt_res + RT_RESERVE) return 0;
    p = (unsigned char*)VirtualAlloc(rt_com, bytes, 0x1000u, 0x04u);
    if(!p) return 0;
    rt_com += bytes;
    return 1;
}
#else
# error "s2r_rt.c targets wasm32 or Windows"
#endif

/* ==========================================================================
 * 2. boundary-tag allocator
 *
 * Header is 64 bytes so every payload comes back 64-byte aligned: that makes
 * aligned_alloc(64, n) literally malloc(n), which is what lets the header's C11
 * branch free an aligned block with plain free() the way it does on glibc.
 * ========================================================================== */

#define RT_ALIGN 64u
#define RT_HDR   64u
#define RT_MIN   (RT_HDR + RT_ALIGN)
#define RT_INUSE ((size_t)1)

typedef struct Chunk {
    size_t size;              /* total bytes incl. header; bit 0 = in use   */
    size_t prevsz;            /* total bytes of the physically previous one */
    struct Chunk *fnext;      /* free list, meaningful only while free      */
    struct Chunk *fprev;
} Chunk;

#define CSZ(c)     ((c)->size & ~RT_INUSE)
#define PAYLOAD(c) ((void*)((unsigned char*)(c) + RT_HDR))
#define CHUNKOF(p) ((Chunk*)((unsigned char*)(p) - RT_HDR))

static Chunk *rt_free;        /* head of the free list                       */
static Chunk *rt_top;         /* sentinel: always in use, payload-less       */
static int    rt_ready;

static size_t rt_roundup(size_t n){
    size_t t = n + RT_HDR;
    if(t < n) return 0;                                   /* overflow */
    t = (t + RT_ALIGN - 1u) & ~(size_t)(RT_ALIGN - 1u);
    return t < RT_MIN ? RT_MIN : t;
}
static void fl_push(Chunk *c){
    c->fprev = 0; c->fnext = rt_free;
    if(rt_free) rt_free->fprev = c;
    rt_free = c;
}
static void fl_drop(Chunk *c){
    if(c->fprev) c->fprev->fnext = c->fnext; else rt_free = c->fnext;
    if(c->fnext) c->fnext->fprev = c->fprev;
    c->fnext = c->fprev = 0;
}
static Chunk *nextc(Chunk *c){ return (Chunk*)((unsigned char*)c + CSZ(c)); }
static Chunk *prevc(Chunk *c){
    return c->prevsz ? (Chunk*)((unsigned char*)c - c->prevsz) : 0;
}
static void rt_settop(unsigned char *at, size_t total, size_t prevsz){
    Chunk *c = (Chunk*)at;
    c->size = total; c->prevsz = prevsz;
    fl_push(c);
    rt_top = (Chunk*)(at + total);
    rt_top->size = RT_HDR | RT_INUSE;
    rt_top->prevsz = total;
}

static int rt_init(void){
    unsigned char *lo, *hi;
    size_t span;
    if(rt_ready) return 1;
    lo = sys_base();
    if(!lo) return 0;
    lo = (unsigned char*)(((size_t)lo + RT_ALIGN - 1u) & ~(size_t)(RT_ALIGN - 1u));
    hi = sys_end();
    if(hi < lo + RT_MIN + RT_HDR){
        if(!sys_grow((size_t)(lo + RT_MIN + RT_HDR - hi) + 65536u)) return 0;
        hi = sys_end();
        if(hi < lo + RT_MIN + RT_HDR) return 0;
    }
    span = ((size_t)(hi - lo) - RT_HDR) & ~(size_t)(RT_ALIGN - 1u);
    rt_free = 0;
    rt_settop(lo, span, 0);
    rt_ready = 1;
    return 1;
}

static int rt_extend(size_t need){
    Chunk *last = prevc(rt_top);
    unsigned char *at;
    size_t prevsz, span;
    if(last && !(last->size & RT_INUSE)){
        fl_drop(last);
        at = (unsigned char*)last;
        prevsz = last->prevsz;
    } else {
        at = (unsigned char*)rt_top;
        prevsz = rt_top->prevsz;
    }
    if(!sys_grow(need + RT_HDR + 65536u)) return 0;
    span = ((size_t)(sys_end() - at) - RT_HDR) & ~(size_t)(RT_ALIGN - 1u);
    if(span < need) return 0;
    rt_settop(at, span, prevsz);
    return 1;
}

void *malloc(size_t n){
    size_t need;
    Chunk *c;
    if(!rt_init()) return 0;
    need = rt_roundup(n ? n : 1u);
    if(!need) return 0;
    for(;;){
        for(c = rt_free; c; c = c->fnext) if(CSZ(c) >= need) break;
        if(c) break;
        if(!rt_extend(need)) return 0;
    }
    fl_drop(c);
    if(CSZ(c) - need >= RT_MIN){
        size_t rest = CSZ(c) - need;
        Chunk *r = (Chunk*)((unsigned char*)c + need);
        r->size = rest; r->prevsz = need;
        nextc(r)->prevsz = rest;
        fl_push(r);
        c->size = need;
    }
    c->size |= RT_INUSE;
    return PAYLOAD(c);
}

void free(void *p){
    Chunk *c, *n, *pv;
    if(!p) return;
    c = CHUNKOF(p);
    c->size &= ~RT_INUSE;
    n = nextc(c);
    if(!(n->size & RT_INUSE)){ fl_drop(n); c->size = CSZ(c) + CSZ(n); }
    pv = prevc(c);
    if(pv && !(pv->size & RT_INUSE)){
        fl_drop(pv); pv->size = CSZ(pv) + CSZ(c); c = pv;
    }
    nextc(c)->prevsz = CSZ(c);
    fl_push(c);
}

void *calloc(size_t n, size_t sz){
    size_t t = n * sz;
    void *p;
    if(sz && t / sz != n) return 0;
    p = malloc(t);
    if(p) memset(p, 0, t);
    return p;
}

void *realloc(void *p, size_t n){
    Chunk *c;
    size_t old;
    void *q;
    if(!p) return malloc(n);
    if(!n){ free(p); return 0; }
    c = CHUNKOF(p);
    old = CSZ(c) - RT_HDR;
    if(old >= n) return p;
    q = malloc(n);
    if(!q) return 0;
    memcpy(q, p, old);
    free(p);
    return q;
}

void *aligned_alloc(size_t align, size_t size){
    if(align <= RT_ALIGN) return malloc(size);   /* always true here */
    return 0;
}
#if defined(_WIN32)
/* the header takes the MSVC branch on Windows; every payload here is already
 * 64-byte aligned, so these are the same allocator under another name */
void *_aligned_malloc(size_t size, size_t align){
    return align <= RT_ALIGN ? malloc(size) : 0;
}
void _aligned_free(void *p){ free(p); }
#endif

/* ==========================================================================
 * 3. mem / str / qsort
 * ========================================================================== */

void *memcpy(void *d, const void *s, size_t n){
#if defined(__wasm__)
    if(n) __builtin_memcpy(d, s, n);             /* -mbulk-memory: memory.copy */
#else
    unsigned char *a = (unsigned char*)d;
    const unsigned char *b = (const unsigned char*)s;
    while(n >= sizeof(size_t)){ *(size_t*)a = *(const size_t*)b;
                                a += sizeof(size_t); b += sizeof(size_t);
                                n -= sizeof(size_t); }
    while(n--) *a++ = *b++;
#endif
    return d;
}
void *memmove(void *d, const void *s, size_t n){
    unsigned char *a = (unsigned char*)d;
    const unsigned char *b = (const unsigned char*)s;
    if(a == b || !n) return d;
    if(a < b){ while(n--) *a++ = *b++; }
    else { a += n; b += n; while(n--) *--a = *--b; }
    return d;
}
void *memset(void *d, int c, size_t n){
#if defined(__wasm__)
    if(n) __builtin_memset(d, c, n);             /* -mbulk-memory: memory.fill */
#else
    unsigned char *a = (unsigned char*)d;
    size_t w = (unsigned char)c, i;
    for(i = 1; i < sizeof(size_t); i++) w |= w << 8;
    while(n >= sizeof(size_t)){ *(size_t*)a = w; a += sizeof(size_t);
                                n -= sizeof(size_t); }
    while(n--) *a++ = (unsigned char)c;
#endif
    return d;
}
int memcmp(const void *x, const void *y, size_t n){
    const unsigned char *a = (const unsigned char*)x, *b = (const unsigned char*)y;
    while(n--){ if(*a != *b) return (int)*a - (int)*b; a++; b++; }
    return 0;
}
size_t strlen(const char *s){ const char *p = s; while(*p) p++; return (size_t)(p - s); }
int strcmp(const char *a, const char *b){
    while(*a && *a == *b){ a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
int abs(int v){ return v < 0 ? -v : v; }

static void rt_swap(unsigned char *a, unsigned char *b, size_t sz){
    while(sz--){ unsigned char t = *a; *a++ = *b; *b++ = t; }
}
static void rt_sift(unsigned char *a, size_t root, size_t end, size_t sz,
                    int (*cmp)(const void*, const void*)){
    for(;;){
        size_t child = 2*root + 1;
        if(child >= end) return;
        if(child + 1 < end && cmp(a + child*sz, a + (child+1)*sz) < 0) child++;
        if(cmp(a + root*sz, a + child*sz) >= 0) return;
        rt_swap(a + root*sz, a + child*sz, sz);
        root = child;
    }
}
/* heapsort: no recursion, no scratch, no quadratic worst case */
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void*, const void*)){
    unsigned char *a = (unsigned char*)base;
    size_t start, end;
    if(n < 2 || !sz) return;
    for(start = n/2; start-- > 0; ) rt_sift(a, start, n, sz, cmp);
    for(end = n; --end > 0; ){ rt_swap(a, a + end*sz, sz); rt_sift(a, 0, end, sz, cmp); }
}

void exit(int code){ (void)code; for(;;){} }
void abort(void){ for(;;){} }

/* ==========================================================================
 * 4. stdio over memory
 * ========================================================================== */

#define RT_FILES 8
#define RT_NAME  48
static FILE rt_tab[RT_FILES];
static char rt_names[RT_FILES][RT_NAME];
static FILE rt_err, rt_out;
FILE *stderr = &rt_err;
FILE *stdout = &rt_out;

static void rt_setname(int i, const char *p){
    size_t k = 0;
    while(p[k] && k < RT_NAME - 1u){ rt_names[i][k] = p[k]; k++; }
    rt_names[i][k] = 0;
    rt_tab[i].name = rt_names[i];
}
static int rt_find(const char *path){
    int i;
    for(i = 0; i < RT_FILES; i++)
        if(rt_tab[i].buf && rt_tab[i].name && strcmp(rt_tab[i].name, path) == 0)
            return i;
    return -1;
}
static int rt_slot(void){
    int i;
    for(i = 0; i < RT_FILES; i++) if(!rt_tab[i].buf) return i;
    return -1;
}

FILE *fopen(const char *path, const char *mode){
    int w = 0, i;
    const char *m = mode;
    while(*m){ if(*m == 'w' || *m == 'a' || *m == '+') w = 1; m++; }
    i = rt_find(path);
    if(!w){
        if(i < 0) return 0;
        rt_tab[i].pos = 0; rt_tab[i].writing = 0; rt_tab[i].err = 0; rt_tab[i].open = 1;
        return &rt_tab[i];
    }
    if(i >= 0){ free(rt_tab[i].buf); rt_tab[i].buf = 0; }
    else      { i = rt_slot(); if(i < 0) return 0; }
    rt_tab[i].cap = 4096;
    rt_tab[i].buf = (unsigned char*)malloc(rt_tab[i].cap);
    if(!rt_tab[i].buf){ rt_tab[i].cap = 0; return 0; }
    rt_tab[i].len = rt_tab[i].pos = 0;
    rt_tab[i].writing = 1; rt_tab[i].err = 0; rt_tab[i].open = 1;
    rt_setname(i, path);
    return &rt_tab[i];
}
int fclose(FILE *f){ if(f && f != &rt_err && f != &rt_out) f->open = 0; return 0; }

size_t fread(void *p, size_t sz, size_t n, FILE *f){
    size_t want, avail;
    if(!f || !f->buf || !sz) return 0;
    want  = sz * n;
    avail = f->len - f->pos;
    if(want > avail) want = avail - (avail % sz);
    memcpy(p, f->buf + f->pos, want);
    f->pos += want;
    return want / sz;
}
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f){
    size_t want;
    if(!f) return 0;
    if(f == &rt_err || f == &rt_out) return n;          /* diagnostics discarded */
    if(!f->buf || !sz) return 0;
    want = sz * n;
    if(f->pos + want > f->cap){
        size_t cap = f->cap ? f->cap : 4096;
        unsigned char *nb;
        while(cap < f->pos + want) cap += cap/2u + 4096u;
        nb = (unsigned char*)realloc(f->buf, cap);
        if(!nb){ f->err = 1; return 0; }
        f->buf = nb; f->cap = cap;
    }
    memcpy(f->buf + f->pos, p, want);
    f->pos += want;
    if(f->pos > f->len) f->len = f->pos;
    return n;
}
int fseek(FILE *f, long off, int whence){
    long base;
    if(!f || !f->buf) return -1;
    base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)f->pos : (long)f->len;
    if(base + off < 0) return -1;
    f->pos = (size_t)(base + off);
    return 0;
}
long ftell(FILE *f){ return f ? (long)f->pos : -1L; }
int  fflush(FILE *f){ (void)f; return 0; }
int  feof(FILE *f){ return (f && f->buf) ? (f->pos >= f->len) : 1; }
int  ferror(FILE *f){ return f ? f->err : 1; }
int  fprintf(FILE *f, const char *fmt, ...){ (void)f; (void)fmt; return 0; }
int  printf(const char *fmt, ...){ (void)fmt; return 0; }
int  puts(const char *s){ (void)s; return 0; }
int  fgetc(FILE *f){
    if(!f || !f->buf || f->pos >= f->len) return EOF;
    return (int)f->buf[f->pos++];
}
int  getc(FILE *f){ return fgetc(f); }
int  fputc(int c, FILE *f){ unsigned char b = (unsigned char)c;
    return fwrite(&b, 1, 1, f) == 1 ? c : EOF; }
int  remove(const char *path){
    int i = rt_find(path);
    if(i < 0) return -1;
    free(rt_tab[i].buf);
    rt_tab[i].buf = 0; rt_tab[i].len = rt_tab[i].cap = rt_tab[i].pos = 0;
    rt_names[i][0] = 0;
    return 0;
}

/* handles the page needs to reach the bytes the library wrote */
unsigned char *s2r_memfile_ptr(const char *path){
    int i = rt_find(path); return i < 0 ? 0 : rt_tab[i].buf;
}
size_t s2r_memfile_len(const char *path){
    int i = rt_find(path); return i < 0 ? 0 : rt_tab[i].len;
}
int s2r_memfile_put(const char *path, const unsigned char *src, size_t n){
    FILE *f = fopen(path, "wb");
    if(!f) return 0;
    if(n && fwrite(src, 1, n, f) != n){ fclose(f); return 0; }
    fclose(f);
    return 1;
}
