/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/*
 * s2r_verify.c - identity/integrity verifier for .s2r files.
 * Build: gcc -O2 -I ../include s2r_verify.c -o s2r_verify
 * Usage: s2r_verify <file.s2r>
 * Output: field-by-field report; exit code 0 = INTACT, !=0 = invalid/corrupt.
 *         Suitable for scripts and CI.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "smart2raw.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: s2r_verify <file.s2r>\n"); return 2; }
    const char *path = argv[1];

    /* 1) read and check the raw header (16 bytes) */
    FILE *f = fopen(path, "rb");
    if (!f) { perror("abrir"); return 3; }
    unsigned char h[16];
    if (fread(h, 1, 16, f) != 16) { fprintf(stderr, "[X] file too short\n"); fclose(f); return 4; }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fclose(f);

    uint32_t magic = (uint32_t)h[0] | ((uint32_t)h[1]<<8) | ((uint32_t)h[2]<<16) | ((uint32_t)h[3]<<24);
    int ok_magic = (magic==S2R_MAGIC_V33 || magic==S2R_MAGIC || magic==S2R_MAGIC_V32);
    int8_t size = (int8_t)h[4]; unsigned flags = h[5], fmt = h[6];
    uint64_t count = 0; for (int i=0;i<8;i++) count |= (uint64_t)h[8+i] << (8*i);
    int absbits = size<0?-size:size, eb = absbits/8;
    int ok_class = (absbits==8||absbits==16||absbits==32||absbits==64);
    uint64_t expected = ok_class ? 16 + count*(uint64_t)eb + 4 : 0;
    int ok_size = ok_class && ((uint64_t)fsz == expected);

    printf("verifying: %s\n", path);
    printf("  [%s] magic 0x%08x (flags 0x%02x)\n", ok_magic?"ok":"X ", magic, flags);
    printf("  [%s] fmt = %u\n",   fmt==1?"ok":"? ", fmt);
    printf("  [%s] class = %d bits%s\n", ok_class?"ok":"X ", absbits, size<0?" signed":"");
    printf("  [%s] size = %ld B (expected %" PRIu64 ")\n", ok_size?"ok":"X ", fsz, expected);

    if (!ok_magic || !ok_class || !ok_size) {
        printf("RESULT: INVALID (header)\n"); return 5;
    }
    /* 2) validate CRC32 by reopening via the library (recomputes the payload CRC) */
    S2RError e;
#if S2R_HAS_MMAP
    S2RMap m; e = s2r_map_open(&m, path, 1); if (e==S2R_OK) s2r_map_close(&m);
#else
    S2RPool p; e = s2r_load_portable(&p, path); if (e==S2R_OK) s2r_pool_free(&p);
#endif
    printf("  [%s] payload CRC32\n", e==S2R_OK?"ok":"X ");
    if (e != S2R_OK) { printf("RESULT: CORRUPT (%s)\n", s2r_strerror(e)); return 6; }

    printf("RESULT: INTACT  (%" PRIu64 " elements)\n", count);
    return 0;
}
