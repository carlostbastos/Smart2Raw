/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/*
 * s2r.c - CLI do Smart2Raw.
 * Build:  gcc -O2 -I ../include s2r.c -o s2r
 *
 * Subcomandos:
 *   pack   <in.txt> <out.s2r> [--signed]   text integers -> .s2r (classifies)
 *   unpack <in.s2r> <out.txt>              .s2r -> text integers
 *   info   <file.s2r>                          file metadata
 *   verify <file.s2r>                          integrity (magic, class, count, CRC)
 *   agg    <file.s2r> <sum|min|max|count-gt N|count-range A B>
 *
 * Text input is a sequence of decimal integers separated by spaces
 * or newlines (use "-" to read from stdin in pack).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "smart2raw.h"

static int usage(void) {
    fprintf(stderr,
      "usage: s2r <command> ...\n"
      "  pack   <in.txt|-> <out.s2r> [--signed]\n"
      "  unpack <in.s2r> <out.txt>\n"
      "  info   <file.s2r>\n"
      "  verify <file.s2r>\n"
      "  agg    <file.s2r> sum|min|max|count-gt N|count-range A B\n");
    return 2;
}

/* read (decimal) integers from a file/stdin into an int64 vector */
static int64_t *read_ints(const char *path, size_t *n_out) {
    FILE *f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "r");
    if (!f) { perror("open input"); return NULL; }
    size_t cap = 1024, n = 0;
    int64_t *v = (int64_t *)malloc(cap * sizeof(int64_t));
    while (fscanf(f, "%" SCNd64, &v[n]) == 1) {
        if (++n == cap) { cap *= 2; v = (int64_t *)realloc(v, cap * sizeof(int64_t)); }
    }
    if (f != stdin) fclose(f);
    *n_out = n;
    return v;
}

static int cmd_pack(int argc, char **argv) {
    if (argc < 4) return usage();
    int is_signed = (argc >= 5 && strcmp(argv[4], "--signed") == 0);
    size_t n; int64_t *v = read_ints(argv[2], &n);
    if (!v) return 1;
    S2RPool p;
    if (is_signed) {
        s2r_pool_init(&p, S2R_I8, n ? n : 1);
        for (size_t i = 0; i < n; i++) s2r_push_signed_adaptive(&p, v[i]);
    } else {
        for (size_t i = 0; i < n; i++) if (v[i] < 0) {
            fprintf(stderr, "negative value found; use --signed\n"); free(v); return 1; }
        s2r_pool_init(&p, S2R_8, n ? n : 1);
        for (size_t i = 0; i < n; i++) s2r_push_adaptive(&p, (uint64_t)v[i]);
    }
    free(v);
    S2RError e = s2r_save_portable(&p, argv[3]);
    printf("pack: %zu integers, class %d bits%s -> %s (%s)\n",
           (size_t)p.count, (int)(p.size < 0 ? -p.size : p.size),
           p.size < 0 ? " signed" : "", argv[3], s2r_strerror(e));
    s2r_pool_free(&p);
    return e == S2R_OK ? 0 : 1;
}

static int cmd_unpack(int argc, char **argv) {
    if (argc < 4) return usage();
    S2RPool p;
    S2RError e = s2r_load_portable(&p, argv[2]);
    if (e != S2R_OK) { fprintf(stderr, "load: %s\n", s2r_strerror(e)); return 1; }
    FILE *f = fopen(argv[3], "w");
    if (!f) { perror("open output"); s2r_pool_free(&p); return 1; }
    int sg = (p.size < 0);
    for (size_t i = 0; i < p.count; i++) {
        if (sg) fprintf(f, "%" PRId64 "\n", s2r_get_signed(&p, i));
        else    fprintf(f, "%" PRIu64 "\n", s2r_get(&p, i));
    }
    fclose(f);
    printf("unpack: %zu integers -> %s\n", (size_t)p.count, argv[3]);
    s2r_pool_free(&p);
    return 0;
}

/* read the fixed 16-byte header (LE) for diagnostics */
static int read_header(const char *path, int *size, unsigned *flags, unsigned *fmt,
                       uint64_t *count, long *filesz) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("open"); return -1; }
    unsigned char h[16];
    if (fread(h, 1, 16, f) != 16) { fclose(f); return -2; }
    fseek(f, 0, SEEK_END); *filesz = ftell(f); fclose(f);
    uint32_t magic = (uint32_t)h[0] | ((uint32_t)h[1]<<8) | ((uint32_t)h[2]<<16) | ((uint32_t)h[3]<<24);
    if (magic != S2R_MAGIC_V33 && magic != S2R_MAGIC && magic != S2R_MAGIC_V32) return -3;
    *size = (int8_t)h[4]; *flags = h[5]; *fmt = h[6];
    *count = 0; for (int i = 0; i < 8; i++) *count |= (uint64_t)h[8+i] << (8*i);
    return 0;
}

static int cmd_info(int argc, char **argv) {
    if (argc < 3) return usage();
    int size; unsigned flags, fmt; uint64_t count; long fsz;
    int r = read_header(argv[2], &size, &flags, &fmt, &count, &fsz);
    if (r) { fprintf(stderr, "invalid header (%d)\n", r); return 1; }
    int eb = (size < 0 ? -size : size) / 8;
    printf("file    : %s\n", argv[2]);
    printf("magic   : portable v3.3 (0x33335253) | fmt %u\n", fmt);
    printf("class   : %d bits%s (%d byte/elem)\n", size<0?-size:size, size<0?" signed":"", eb);
    printf("flags   : 0x%02x\n", flags);
    printf("count   : %" PRIu64 " elements\n", count);
    printf("size : %ld bytes  (expected %llu = 16 + %" PRIu64 "*%d + 4)\n",
           fsz, (unsigned long long)(16 + count*(uint64_t)eb + 4), count, eb);
    return 0;
}

static int cmd_verify(int argc, char **argv) {
    if (argc < 3) return usage();
#if S2R_HAS_MMAP
    S2RMap m;
    S2RError e = s2r_map_open(&m, argv[2], 1);   /* 1 = verify CRC */
    if (e == S2R_OK) {
        printf("INTACT: %s (%zu elements, class %d bits, CRC ok)\n",
               argv[2], (size_t)m.pool.count, (int)(m.pool.size<0?-m.pool.size:m.pool.size));
        s2r_map_close(&m);
        return 0;
    }
    fprintf(stderr, "FAIL: %s -> %s\n", argv[2], s2r_strerror(e));
    return 1;
#else
    S2RPool p;
    S2RError e = s2r_load_portable(&p, argv[2]);  /* load also validates CRC */
    if (e == S2R_OK) { printf("INTACT: %s (%zu elements)\n", argv[2], (size_t)p.count); s2r_pool_free(&p); return 0; }
    fprintf(stderr, "FAIL: %s -> %s\n", argv[2], s2r_strerror(e));
    return 1;
#endif
}

static int cmd_agg(int argc, char **argv) {
    if (argc < 4) return usage();
    S2RPool p;
    S2RError e = s2r_load_portable(&p, argv[2]);
    if (e != S2R_OK) { fprintf(stderr, "load: %s\n", s2r_strerror(e)); return 1; }
    const char *op = argv[3];
    int sg = (p.size < 0);
    if (!strcmp(op, "sum")) {
        if (sg) printf("%" PRId64 "\n", (int64_t)s2r_sum(&p));   /* sum in two's complement */
        else    printf("%" PRIu64 "\n", s2r_sum_fast(&p));
    } else if (!strcmp(op, "min")) {
        if (sg) printf("%" PRId64 "\n", s2r_min_signed_val(&p)); else printf("%" PRIu64 "\n", s2r_min(&p));
    } else if (!strcmp(op, "max")) {
        if (sg) printf("%" PRId64 "\n", s2r_max_signed_val(&p)); else printf("%" PRIu64 "\n", s2r_max(&p));
    } else if (!strcmp(op, "count-gt") && argc >= 5) {
        printf("%zu\n", s2r_count_gt(&p, strtoull(argv[4], NULL, 10)));
    } else if (!strcmp(op, "count-range") && argc >= 6) {
        printf("%zu\n", s2r_count_range(&p, strtoull(argv[4], NULL, 10), strtoull(argv[5], NULL, 10)));
    } else { s2r_pool_free(&p); return usage(); }
    s2r_pool_free(&p);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (!strcmp(argv[1], "pack"))   return cmd_pack(argc, argv);
    if (!strcmp(argv[1], "unpack")) return cmd_unpack(argc, argv);
    if (!strcmp(argv[1], "info"))   return cmd_info(argc, argv);
    if (!strcmp(argv[1], "verify")) return cmd_verify(argc, argv);
    if (!strcmp(argv[1], "agg"))    return cmd_agg(argc, argv);
    return usage();
}
