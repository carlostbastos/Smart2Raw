/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* test_format_hardening.c - the .s2r reader must obey its own spec.
 *
 * SPEC_s2r_format.md, "Compatibility contract", says a conforming reader must
 * reject unknown magic, bad classes, unsupported fmt, a nonzero reserved byte,
 * a file length different from 16+payload+4, and a bad CRC. The C core used to
 * check only magic, class and CRC. Three consequences, all covered here:
 *
 *   1. flags were adopted verbatim from disk. S2R_FLAG_EXTERNAL (bit 2) makes
 *      s2r_pool_free skip the free and null the pointer -> heap leak from a
 *      crafted file (confirmed under ASan). S2R_FLAG_READONLY (bit 1) makes the
 *      loaded pool permanently immutable.
 *   2. fmt and the reserved byte were never read, so C accepted files that the
 *      Go/JS/Python ports reject -> the "portable contract" was not one.
 *   3. trailing bytes after the CRC were silently tolerated.
 *
 * Also covers the symmetric write side: ownership bits must never reach disk.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smart2raw.h"

static int pass = 0, fail = 0;
#define CHECK(c, m) do{ if(c){pass++;} else {printf("  [FAIL] %s\n",(m)); fail++;} }while(0)

/* Writes a well-formed v3.3 file whose header bytes can be poisoned one at a
 * time. CRC is always computed correctly, so every rejection below is due to
 * the header check under test and not to a CRC mismatch. */
static int write_file(const char *fn, uint8_t cls, uint8_t flags, uint8_t fmt,
                      uint8_t rsvd, uint64_t count_field,
                      const uint8_t *payload, size_t payload_len, size_t trailing)
{
    FILE *f = fopen(fn, "wb");
    if(!f) return 0;
    const uint8_t magic[4] = { 0x53, 0x52, 0x33, 0x33 };   /* "SR33" */
    const uint8_t hdr[4]   = { cls, flags, fmt, rsvd };
    uint8_t cnt[8], crcb[4];
    for(int i=0;i<8;i++) cnt[i]  = (uint8_t)(count_field >> (8*i));
    uint32_t crc = payload_len ? s2r_crc32(payload, payload_len, 0) : 0;
    for(int i=0;i<4;i++) crcb[i] = (uint8_t)(crc >> (8*i));
    fwrite(magic,4,1,f);
    fwrite(hdr,4,1,f);
    fwrite(cnt,8,1,f);
    if(payload_len) fwrite(payload,payload_len,1,f);
    fwrite(crcb,4,1,f);
    for(size_t i=0;i<trailing;i++) fputc(0x00, f);
    fclose(f);
    return 1;
}

/* Loads and reports whether it was accepted, freeing on success. */
static int loads_ok(const char *fn)
{
    S2RPool p;
    if(s2r_load_portable(&p, fn) != S2R_OK) return 0;
    s2r_pool_free(&p);
    return 1;
}

/* ---- block-wise files, written by hand so the header can lie ---------------
 *
 * v3.5.1 fixes a heap overflow found by reading the loader against its own
 * arithmetic. The body length is nblocks*K + bytes; both terms come off disk
 * and the sum was done in plain size_t. A file that declares nblocks = 2^22
 * and bytes = 2^64 - 2^22*K + 16 makes the sum WRAP to 16, so the loader
 * malloc'd 16 bytes and then memcpy'd 4 MB into it. Everything else about the
 * file is honest - magic, fmt, classes, nb == ceil(cnt/blk), CRC over the real
 * body, exact EOF - which is why nothing caught it. Confirmed under ASan as a
 * heap-buffer-overflow of 4 MB, and as a plain segfault at -O2.
 *
 * These write such files by hand. They must all be refused. */
static void put32(FILE *f, uint32_t v){ int i; for(i=0;i<4;i++) fputc((int)((v>>(8*i))&0xff), f); }
static void put64(FILE *f, uint64_t v){ int i; for(i=0;i<8;i++) fputc((int)((v>>(8*i))&0xff), f); }

/* count/block/nblocks/bytes are written verbatim - that is the whole point.
 * body_len bytes of zeroes follow, with a CRC that is always correct. */
static int write_blocked(const char *fn, uint64_t cnt, uint64_t blk,
                         uint64_t nblocks, uint64_t bytes_field, size_t body_len)
{
    FILE *f = fopen(fn, "wb");
    uint8_t *body;
    if(!f) return 0;
    body = (uint8_t*)calloc(body_len ? body_len : 1, 1);
    if(!body){ fclose(f); return 0; }
    put32(f, (uint32_t)S2R_MAGIC_V33);
    fputc(0, f); fputc(0, f); fputc((int)S2R_BLK_FMT, f); fputc(0, f);
    put64(f, cnt); put64(f, blk); put64(f, nblocks);
    fputc((int)(uint8_t)S2R_8, f); fputc((int)(uint8_t)S2R_8, f);
    fputc((int)(uint8_t)S2R_8, f); fputc((int)(uint8_t)S2R_8, f);
    put64(f, bytes_field);
    if(body_len) fwrite(body, 1, body_len, f);
    put32(f, body_len ? s2r_crc32(body, body_len, 0) : 0);
    fclose(f);
    free(body);
    return 1;
}

static int blocked_loads_ok(const char *fn)
{
    S2RBlocked b;
    if(s2r_blocked_load(&b, fn) != S2R_OK) return 0;
    s2r_blocked_free(&b);
    return 1;
}

int main(void)
{
    const uint8_t body[3] = { 10, 20, 30 };
    const uint64_t SUM = 60;

    /* ---- baseline: a valid file must still load, and load correctly ---- */
    write_file("/tmp/s2r_h_ok.s2r", 8, 0, 1, 0, 3, body, 3, 0);
    {
        S2RPool p;
        CHECK(s2r_load_portable(&p, "/tmp/s2r_h_ok.s2r") == S2R_OK, "valid file rejected");
        CHECK(p.count == 3, "valid file wrong count");
        CHECK(s2r_sum(&p) == SUM, "valid file wrong sum");
        CHECK(p.flags == 0, "valid file should carry no flags");
        s2r_pool_free(&p);
    }

    /* ---- signed flag is a real format bit and must survive ---- */
    {
        const uint8_t sbody[2] = { 0xFF, 0x80 };   /* -1, -128 as i8 */
        S2RPool p;
        write_file("/tmp/s2r_h_sig.s2r", (uint8_t)(int8_t)-8, S2R_FLAG_SIGNED, 1, 0, 2, sbody, 2, 0);
        CHECK(s2r_load_portable(&p, "/tmp/s2r_h_sig.s2r") == S2R_OK, "signed file rejected");
        CHECK(s2r_is_signed(&p), "signed flag lost on load");
        CHECK(s2r_sum_signed(&p) == -129, "signed payload misread");
        s2r_pool_free(&p);
    }

    /* ---- 1. ownership bits from disk must be masked off ---- */
    write_file("/tmp/s2r_h_ext.s2r", 8, S2R_FLAG_EXTERNAL, 1, 0, 3, body, 3, 0);
    {
        S2RPool p;
        CHECK(s2r_load_portable(&p, "/tmp/s2r_h_ext.s2r") == S2R_OK, "EXTERNAL file should load");
        CHECK(!(p.flags & S2R_FLAG_EXTERNAL), "EXTERNAL adopted from disk (heap leak)");
        /* If EXTERNAL had been adopted, pool_free would leak instead of freeing.
         * We cannot observe the leak from inside the process, but we can observe
         * the flag that causes it, and that the pool is still writable. */
        CHECK(s2r_push_adaptive(&p, 40) == S2R_OK, "pool from disk should be writable");
        s2r_pool_free(&p);
    }
    write_file("/tmp/s2r_h_ro.s2r", 8, S2R_FLAG_READONLY, 1, 0, 3, body, 3, 0);
    {
        S2RPool p;
        CHECK(s2r_load_portable(&p, "/tmp/s2r_h_ro.s2r") == S2R_OK, "READONLY file should load");
        CHECK(!(p.flags & S2R_FLAG_READONLY), "READONLY adopted from disk (pool frozen)");
        CHECK(s2r_push_adaptive(&p, 40) == S2R_OK, "pool from disk should be writable");
        s2r_pool_free(&p);
    }
    /* every non-format bit at once */
    write_file("/tmp/s2r_h_all.s2r", 8, 0xFE, 1, 0, 3, body, 3, 0);
    {
        S2RPool p;
        CHECK(s2r_load_portable(&p, "/tmp/s2r_h_all.s2r") == S2R_OK, "flags=0xFE should load");
        CHECK(p.flags == 0, "non-format flag bits leaked into the pool");
        s2r_pool_free(&p);
    }

    /* ---- 2. fmt and reserved byte (spec rules 4 and 5) ---- */
    write_file("/tmp/s2r_h_fmt0.s2r", 8, 0, 0,  0, 3, body, 3, 0);
    CHECK(!loads_ok("/tmp/s2r_h_fmt0.s2r"), "fmt=0 accepted");
    write_file("/tmp/s2r_h_fmt2.s2r", 8, 0, 2,  0, 3, body, 3, 0);
    CHECK(!loads_ok("/tmp/s2r_h_fmt2.s2r"), "fmt=2 accepted (ports reject it)");
    write_file("/tmp/s2r_h_fmt99.s2r", 8, 0, 99, 0, 3, body, 3, 0);
    CHECK(!loads_ok("/tmp/s2r_h_fmt99.s2r"), "fmt=99 accepted (ports reject it)");
    write_file("/tmp/s2r_h_rsvd.s2r", 8, 0, 1,  7, 3, body, 3, 0);
    CHECK(!loads_ok("/tmp/s2r_h_rsvd.s2r"), "nonzero reserved byte accepted");

    /* ---- 3. exact file length (spec rule 6) ---- */
    write_file("/tmp/s2r_h_tail.s2r", 8, 0, 1, 0, 3, body, 3, 8);
    CHECK(!loads_ok("/tmp/s2r_h_tail.s2r"), "trailing bytes accepted");

    /* ---- still-enforced pre-existing rules (guard against regressions) ---- */
    write_file("/tmp/s2r_h_cls.s2r", 7, 0, 1, 0, 3, body, 3, 0);
    CHECK(!loads_ok("/tmp/s2r_h_cls.s2r"), "invalid class 7 accepted");
    {   /* corrupt one payload byte after the fact -> CRC must catch it */
        FILE *f = fopen("/tmp/s2r_h_crc.s2r", "wb");
        if(f){ fclose(f); }
        write_file("/tmp/s2r_h_crc.s2r", 8, 0, 1, 0, 3, body, 3, 0);
        f = fopen("/tmp/s2r_h_crc.s2r", "r+b");
        if(f){ fseek(f, 16, SEEK_SET); fputc(0x99, f); fclose(f); }
        CHECK(!loads_ok("/tmp/s2r_h_crc.s2r"), "CRC corruption accepted");
    }
    {   /* count that overflows when multiplied by the element size */
        write_file("/tmp/s2r_h_ovf.s2r", 16, 0, 1, 0, 0x8000000000000002ULL, body, 3, 0);
        CHECK(!loads_ok("/tmp/s2r_h_ovf.s2r"), "count overflow accepted");
    }

    /* ---- 4. write side: ownership bits must never reach disk ---- */
    {
        S2RPool p;
        s2r_pool_init(&p, S2R_8, 4);
        s2r_push_adaptive(&p, 7);
        p.flags |= S2R_FLAG_EXTERNAL;              /* dirty in-memory state */
        CHECK(s2r_save_portable(&p, "/tmp/s2r_h_wr.s2r") == S2R_OK, "save failed");
        p.flags &= (uint8_t)~S2R_FLAG_EXTERNAL;    /* so pool_free really frees */
        s2r_pool_free(&p);

        FILE *f = fopen("/tmp/s2r_h_wr.s2r", "rb");
        uint8_t h[8] = {0};
        if(f){ if(fread(h,1,8,f) != 8) h[5] = 0xFF; fclose(f); }
        CHECK(h[5] == 0, "EXTERNAL bit written to disk");
        CHECK(h[6] == 1, "fmt byte not 1 on write");
        CHECK(h[7] == 0, "reserved byte not 0 on write");
        CHECK(loads_ok("/tmp/s2r_h_wr.s2r"), "our own file failed to reload");
    }


    /* ---- 6. class and signed flag must agree (spec "Class rules") ---- */
    {
        /* signed class, flag missing: C, JS and Python all accepted this; Go did not */
        const uint8_t sb[1] = { 0xFF };
        write_file("/tmp/s2r_h_sgm.s2r", (uint8_t)(int8_t)-8, 0, 1, 0, 1, sb, 1, 0);
        CHECK(!loads_ok("/tmp/s2r_h_sgm.s2r"), "signed class with flag clear accepted");
        /* unsigned class carrying the signed flag */
        write_file("/tmp/s2r_h_usf.s2r", 8, S2R_FLAG_SIGNED, 1, 0, 3, body, 3, 0);
        CHECK(!loads_ok("/tmp/s2r_h_usf.s2r"), "unsigned class with signed flag accepted");
    }

#if S2R_HAS_MMAP
    /* ---- 5. the mmap reader must enforce the same rules ---- */
    {
        S2RMap m;
        CHECK(s2r_map_open(&m, "/tmp/s2r_h_ok.s2r", 1) == S2R_OK, "mmap: valid file rejected");
        CHECK(s2r_sum(&m.pool) == SUM, "mmap: wrong sum");
        /* READONLY|EXTERNAL are correct here - the buffer lives in the mapping. */
        CHECK((m.pool.flags & S2R_FLAG_READONLY) != 0, "mmap: pool must be read-only");
        CHECK((m.pool.flags & S2R_FLAG_EXTERNAL) != 0, "mmap: buffer must be external");
        s2r_map_close(&m);

        CHECK(s2r_map_open(&m, "/tmp/s2r_h_fmt99.s2r", 1) != S2R_OK, "mmap: fmt=99 accepted");
        CHECK(s2r_map_open(&m, "/tmp/s2r_h_rsvd.s2r", 1) != S2R_OK, "mmap: bad reserved accepted");
        CHECK(s2r_map_open(&m, "/tmp/s2r_h_tail.s2r", 1) != S2R_OK, "mmap: trailing bytes accepted");
        CHECK(s2r_map_open(&m, "/tmp/s2r_h_crc.s2r",  1) != S2R_OK, "mmap: CRC corruption accepted");
        CHECK(s2r_map_open(&m, "/tmp/s2r_h_ovf.s2r",  1) != S2R_OK, "mmap: count overflow accepted");
        CHECK(s2r_map_open(&m, "/tmp/s2r_h_sgm.s2r",  1) != S2R_OK, "mmap: class/flag disagreement accepted");
    }
#endif

    /* ---- 6. block-wise loader: the body length must be real ----
     * Regression for the v3.5.1 heap overflow. See write_blocked() above. */
    {
        const uint64_t K = 6;               /* 2 + four 1-byte metadata classes */
        const uint64_t NB = 1ull << 22;     /* malloc(NB) succeeds, so the copy runs */
        S2RBlocked rb;
        uint64_t src[512];
        size_t i;

        /* a. the length wraps: nblocks*K + bytes == 16 in size_t arithmetic */
        write_blocked("/tmp/s2r_h_bwrap.s2r", NB, 1, NB, (uint64_t)0 - NB*K + 16, 16);
        CHECK(!blocked_loads_ok("/tmp/s2r_h_bwrap.s2r"),
              "blocked: wrapped body length accepted (heap overflow)");

        /* b. the length does not wrap, it is simply not in the file. Rejecting
         *    this also means a 64-byte file can no longer ask for a gigabyte. */
        write_blocked("/tmp/s2r_h_bbig.s2r", 1, 1, 1, 1ull << 30, 8);
        CHECK(!blocked_loads_ok("/tmp/s2r_h_bbig.s2r"),
              "blocked: payload larger than the file accepted");

        /* c. the multiplication itself overflows, before bytes is even added */
        write_blocked("/tmp/s2r_h_bnb.s2r", (uint64_t)(SIZE_MAX/4), 1,
                      (uint64_t)(SIZE_MAX/4), 0, 8);
        CHECK(!blocked_loads_ok("/tmp/s2r_h_bnb.s2r"),
              "blocked: nblocks*metadata overflow accepted");

        /* d. and an honest file written by the library must still load. A guard
         *    that also rejects real data is not a fix. */
        for(i=0;i<512;i++) src[i] = 1000 + (i % 7) * 3;
        if(s2r_blocked_build(&rb, src, 512, 64)){
            uint64_t sum_before = 0, sum_after = 0;
            S2RBlocked lb;
            for(i=0;i<512;i++) sum_before += src[i];
            CHECK(s2r_blocked_save(&rb, "/tmp/s2r_h_bok.s2r") == S2R_OK,
                  "blocked: honest file failed to save");
            s2r_blocked_free(&rb);
            CHECK(s2r_blocked_load(&lb, "/tmp/s2r_h_bok.s2r") == S2R_OK,
                  "blocked: honest file rejected by the new guard");
            for(i=0;i<512;i++) sum_after += s2r_blocked_get(&lb, i);
            CHECK(sum_after == sum_before, "blocked: honest file misread");
            s2r_blocked_free(&lb);
        } else CHECK(0, "blocked: could not build the baseline");
    }

    printf("=== %d OK, %d FAIL ===\n", pass, fail);
    return fail ? 1 : 0;
}
