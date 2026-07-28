/*
 * s2r_probe.c - one column in, one report out.
 *
 * This is the engine behind both the browser page and the Windows executable.
 * It does NOT reimplement anything: every number it reports comes from calling
 * smart2raw.h, and every number that can be checked against a naive loop IS
 * checked before it is reported. A disagreement sets an error flag instead of
 * printing a pretty number.
 *
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdint.h>
#include <stddef.h>
#include "smart2raw.h"

/* ---- the report is a flat array of u64 so no struct layout has to be agreed
 * between C and JavaScript -------------------------------------------------- */
enum {
    R_N=0, R_SIGNED, R_CLS, R_ELEM_BITS,
    R_MIN, R_MAX, R_SUM, R_DISTINCT, R_RUNS,
    R_RAW, R_FLAT, R_AFFINE, R_AF_BASE, R_AF_STRIDE, R_CONST,
    R_BLOCKED, R_BLOCK, R_NBLOCKS, R_NCONST, R_HAS_STRIDE,
    R_BEST, R_BEST_BYTES,
    R_IDX_OK, R_IDX_BYTES,
    R_SORTED, R_SUMMARY_OK,
    R_DICT, R_DICT_K, R_RLE, R_RLE_RUNS, R_BITMAP, R_BITMAP_OK,
    R_FILE_BYTES, R_FILE_FMT, R_ROUNDTRIP, R_CRC_OK,
    R_NEVER_EXPANDS, R_VERIFIED, R_ERR,
    R_NPLAN, R_PLAN_BLK, R_PLAN_BYTES = R_PLAN_BLK + 12,
    R_SLOTS = R_PLAN_BYTES + 12
};

uint64_t s2r_report[R_SLOTS];

/* live state, kept between calls so the page can time queries */
static uint64_t  *g_v;            /* the column, as the user gave it       */
static size_t     g_n;
static int        g_signed;
static S2RBlocked g_blk;   static int g_blk_ok;
static S2RAffine  g_aff;   static int g_aff_ok;
static S2RPool    g_pool;  static int g_pool_ok;
static S2RIndex   g_idx;   static int g_idx_ok;
static int        g_best;         /* 0 flat, 1 affine, 2 blocked           */

unsigned char *s2r_memfile_ptr(const char *path);
size_t         s2r_memfile_len(const char *path);

/* ---- helpers -------------------------------------------------------------- */
static int cmp_u64(const void *a, const void *b){
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return x < y ? -1 : x > y ? 1 : 0;
}
/* LSD radix on 8 bytes: distinct-counting must not be the slow part */
static int radix_u64(uint64_t *a, size_t n){
    uint64_t *tmp = (uint64_t*)malloc(n * sizeof(uint64_t));
    size_t cnt[256], pos[256], i, p;
    if(!tmp) return 0;
    for(p = 0; p < 8; p++){
        for(i = 0; i < 256; i++) cnt[i] = 0;
        for(i = 0; i < n; i++) cnt[(a[i] >> (8*p)) & 0xFF]++;
        if(cnt[(a[0] >> (8*p)) & 0xFF] == n) continue;      /* pass is a no-op */
        pos[0] = 0;
        for(i = 1; i < 256; i++) pos[i] = pos[i-1] + cnt[i-1];
        for(i = 0; i < n; i++) tmp[pos[(a[i] >> (8*p)) & 0xFF]++] = a[i];
        for(i = 0; i < n; i++) a[i] = tmp[i];
    }
    free(tmp);
    return 1;
}
static void reset_state(void){
    if(g_blk_ok){ s2r_blocked_free(&g_blk); g_blk_ok = 0; }
    if(g_aff_ok){ s2r_affine_free(&g_aff);  g_aff_ok = 0; }
    if(g_idx_ok){ s2r_index_free(&g_idx);   g_idx_ok = 0; }
    if(g_pool_ok){ s2r_pool_free(&g_pool);  g_pool_ok = 0; }
}

/* ---- allocation of the input buffer, filled by the caller ----------------- */
uint64_t *s2r_probe_input(uint32_t n){
    reset_state();
    if(g_v){ free(g_v); g_v = 0; g_n = 0; }
    if(!n) return 0;
    g_v = (uint64_t*)malloc((size_t)n * sizeof(uint64_t));
    if(g_v) g_n = n;
    return g_v;
}
uint64_t *s2r_probe_report(void){ return s2r_report; }
uint32_t  s2r_probe_slots(void){ return (uint32_t)R_SLOTS; }

/* ---- the analysis --------------------------------------------------------- */
int s2r_probe_run(uint32_t n, int is_signed)
{
    size_t i;
    const uint64_t *vu;
    const int64_t  *vs;
    uint64_t mnu = 0, mxu = 0, sum = 0, distinct = 0, runs = 1;
    int64_t  mns = 0, mxs = 0;
    int      verified = 1;

    for(i = 0; i < R_SLOTS; i++) s2r_report[i] = 0;
    if(!g_v || !n || n > g_n){ s2r_report[R_ERR] = 1; return 0; }

    reset_state();
    g_n = n; g_signed = is_signed ? 1 : 0;
    vu = g_v; vs = (const int64_t*)g_v;

    /* --- ground truth, by the most naive loop there is --------------------- */
    if(g_signed){
        mns = mxs = vs[0];
        for(i = 0; i < n; i++){
            if(vs[i] < mns) mns = vs[i];
            if(vs[i] > mxs) mxs = vs[i];
            sum += (uint64_t)vs[i];
            if(i && vs[i] != vs[i-1]) runs++;
        }
    } else {
        mnu = mxu = vu[0];
        for(i = 0; i < n; i++){
            if(vu[i] < mnu) mnu = vu[i];
            if(vu[i] > mxu) mxu = vu[i];
            sum += vu[i];
            if(i && vu[i] != vu[i-1]) runs++;
        }
    }
    s2r_report[R_N]      = n;
    s2r_report[R_SIGNED] = (uint64_t)g_signed;
    s2r_report[R_MIN]    = g_signed ? (uint64_t)mns : mnu;
    s2r_report[R_MAX]    = g_signed ? (uint64_t)mxs : mxu;
    s2r_report[R_SUM]    = sum;
    s2r_report[R_RUNS]   = runs;
    s2r_report[R_RAW]    = (uint64_t)n * 8u;

    /* distinct values: exact, by sorting a copy */
    {
        uint64_t *c = (uint64_t*)malloc((size_t)n * sizeof(uint64_t));
        if(c){
            memcpy(c, g_v, (size_t)n * sizeof(uint64_t));
            if(g_signed){
                /* shift into unsigned order so the radix pass is monotone */
                for(i = 0; i < n; i++) c[i] ^= 0x8000000000000000ull;
            }
            if(!radix_u64(c, n)) qsort(c, n, sizeof(uint64_t), cmp_u64);
            distinct = 1;
            for(i = 1; i < n; i++) if(c[i] != c[i-1]) distinct++;
            free(c);
        }
        s2r_report[R_DISTINCT] = distinct;
    }

    /* --- 1. classification: the flat pool --------------------------------- */
    {
        int8_t cls = g_signed ? s2r_classify_signed_array(vs, n)
                              : (int8_t)s2r_classify_array(vu, n);
        s2r_report[R_CLS]       = (uint64_t)(int64_t)cls;
        s2r_report[R_ELEM_BITS] = (uint64_t)s2r_abs_size(cls);
        if(s2r_pool_init(&g_pool, cls, n)){
            g_pool_ok = 1;
            for(i = 0; i < n; i++){
                if(g_signed) s2r_set_signed(&g_pool, i, vs[i]);
                else         s2r_set(&g_pool, i, vu[i]);
            }
            g_pool.count = n;
            s2r_report[R_FLAT] = (uint64_t)s2r_pool_bytes(&g_pool);
            /* the pool must give back exactly what went in */
            for(i = 0; i < n; i++){
                uint64_t got = g_signed ? (uint64_t)s2r_get_signed(&g_pool, i)
                                        : s2r_get(&g_pool, i);
                if(got != g_v[i]){ verified = 0; break; }
            }
            s2r_report[R_SUMMARY_OK] = (uint64_t)(s2r_summarize(&g_pool) ? 1 : 0);
            s2r_report[R_SORTED]     = (uint64_t)(s2r_is_sorted(&g_pool) ? 1 : 0);
            if(s2r_report[R_SORTED]) s2r_mark_sorted(&g_pool);
            if(s2r_index_build(&g_idx, &g_pool)){
                g_idx_ok = 1;
                s2r_report[R_IDX_OK]    = 1;
                s2r_report[R_IDX_BYTES] = (uint64_t)s2r_index_bytes(&g_idx);
            }
        } else {
            s2r_report[R_FLAT] = (uint64_t)n * 8u;
        }
    }

    /* --- 2. affine factoring ---------------------------------------------- */
    {
        int ok = g_signed ? s2r_affine_build_signed(&g_aff, vs, n)
                          : s2r_affine_build(&g_aff, vu, n);
        if(ok){
            g_aff_ok = 1;
            s2r_report[R_AFFINE]    = (uint64_t)s2r_affine_bytes(&g_aff);
            s2r_report[R_AF_BASE]   = (uint64_t)g_aff.base;
            s2r_report[R_AF_STRIDE] = g_aff.stride;
            s2r_report[R_CONST]     = (uint64_t)g_aff.is_const;
            for(i = 0; i < n; i++){
                uint64_t got = g_signed ? (uint64_t)s2r_affine_get_signed(&g_aff, i)
                                        : s2r_affine_get(&g_aff, i);
                if(got != g_v[i]){ verified = 0; break; }
            }
        } else {
            s2r_report[R_AFFINE] = s2r_report[R_FLAT];
        }
    }

    /* --- 3. the planner, then the block-wise layer ------------------------- */
    {
        S2RBlockPlan pl[S2R_PLAN_MAX];
        int k = s2r_blocked_plan(g_signed ? 0 : vu, g_signed ? vs : 0,
                                 n, g_signed, pl, S2R_PLAN_MAX), j;
        size_t block;
        if(k > 12) k = 12;
        s2r_report[R_NPLAN] = (uint64_t)k;
        for(j = 0; j < k; j++){
            s2r_report[R_PLAN_BLK + j]   = (uint64_t)pl[j].block;
            s2r_report[R_PLAN_BYTES + j] = (uint64_t)pl[j].bytes;
        }
        block = s2r_blocked_choose_block(g_signed ? 0 : vu, g_signed ? vs : 0, n, g_signed);
        if(g_signed ? s2r_blocked_build_signed(&g_blk, vs, n, block)
                    : s2r_blocked_build(&g_blk, vu, n, block)){
            g_blk_ok = 1;
            s2r_report[R_BLOCKED]    = (uint64_t)s2r_blocked_bytes(&g_blk);
            s2r_report[R_BLOCK]      = (uint64_t)g_blk.block;
            s2r_report[R_NBLOCKS]    = (uint64_t)g_blk.nblocks;
            s2r_report[R_HAS_STRIDE] = (uint64_t)g_blk.has_stride;
            for(i = 0; i < g_blk.nblocks; i++)
                if(g_blk.bclass[i] == 0) s2r_report[R_NCONST]++;
            for(i = 0; i < n; i++){
                uint64_t got = g_signed ? (uint64_t)s2r_blocked_get_signed(&g_blk, i)
                                        : s2r_blocked_get(&g_blk, i);
                if(got != g_v[i]){ verified = 0; break; }
            }
            /* the aggregate the block metadata answers, against the naive sum */
            if(s2r_blocked_sum_fast(&g_blk) != sum) verified = 0;
        } else {
            s2r_report[R_BLOCKED] = s2r_report[R_FLAT];
        }
    }

    /* --- 4. which one, and the promise that none of them expands ----------- */
    {
        uint64_t best = s2r_report[R_FLAT];
        g_best = 0;
        if(s2r_report[R_AFFINE]  && s2r_report[R_AFFINE]  < best){ best = s2r_report[R_AFFINE];  g_best = 1; }
        if(s2r_report[R_BLOCKED] && s2r_report[R_BLOCKED] < best){ best = s2r_report[R_BLOCKED]; g_best = 2; }
        s2r_report[R_BEST]       = (uint64_t)g_best;
        s2r_report[R_BEST_BYTES] = best;
        s2r_report[R_NEVER_EXPANDS] = (best <= s2r_report[R_RAW]) ? 1 : 0;
    }

    /* --- 5. the classical peers, at their theoretical floor ---------------- */
    {
        uint64_t k = distinct ? distinct : 1, cb = 1;
        while((k >> cb) && cb < 64) cb++;                  /* ceil(log2(k))    */
        if(((uint64_t)1 << (cb-1)) >= k) cb--;
        if(cb < 1) cb = 1;
        s2r_report[R_DICT]      = (uint64_t)n * cb / 8u + k * 8u;
        s2r_report[R_DICT_K]    = k;
        s2r_report[R_RLE]       = runs * 8u;
        s2r_report[R_RLE_RUNS]  = runs;
        s2r_report[R_BITMAP_OK] = (k == 2) ? 1 : 0;
        s2r_report[R_BITMAP]    = (k == 2) ? (uint64_t)((n + 63u) / 64u * 8u) : 0;
    }

    /* --- 6. the real serializer, into memory, and back out ---------------- */
    if(g_blk_ok){
        if(s2r_blocked_save(&g_blk, "col.s2r") == S2R_OK){
            unsigned char *fp = s2r_memfile_ptr("col.s2r");
            size_t fl = s2r_memfile_len("col.s2r");
            S2RBlocked back;
            s2r_report[R_FILE_BYTES] = (uint64_t)fl;
            if(fp && fl > 7) s2r_report[R_FILE_FMT] = fp[6];
            if(s2r_blocked_load(&back, "col.s2r") == S2R_OK){
                int same = 1;
                s2r_report[R_CRC_OK] = 1;            /* load validates the CRC */
                for(i = 0; i < n; i++){
                    uint64_t got = g_signed ? (uint64_t)s2r_blocked_get_signed(&back, i)
                                            : s2r_blocked_get(&back, i);
                    if(got != g_v[i]){ same = 0; break; }
                }
                s2r_report[R_ROUNDTRIP] = (uint64_t)same;
                if(!same) verified = 0;
                s2r_blocked_free(&back);
            }
        }
    }

    s2r_report[R_VERIFIED] = (uint64_t)verified;
    return verified;
}

/* ---- the .s2r bytes, for download ---------------------------------------- */
unsigned char *s2r_probe_file(void){ return s2r_memfile_ptr("col.s2r"); }
uint32_t       s2r_probe_file_len(void){ return (uint32_t)s2r_memfile_len("col.s2r"); }

/* ---- live queries, so the page can time them itself ---------------------- */
#define S2R_NA 0xFFFFFFFFu

uint32_t s2r_probe_count_gt_naive(uint64_t t){
    size_t i, c = 0;
    if(!g_v) return S2R_NA;
    if(g_signed){ const int64_t *v = (const int64_t*)g_v;
        for(i = 0; i < g_n; i++) c += (v[i] > (int64_t)t); }
    else { for(i = 0; i < g_n; i++) c += (g_v[i] > t); }
    return (uint32_t)c;
}
/* Each path is a separate entry point on purpose: which representation is
 * SMALLEST and which one ANSWERS FASTEST are different questions, and a page
 * that only times the winner of the first hides the answer to the second. */
uint32_t s2r_probe_count_gt_flat(uint64_t t){
    if(!g_pool_ok) return S2R_NA;
    return (uint32_t)(g_signed ? s2r_count_gt_signed_fast(&g_pool, (int64_t)t)
                               : s2r_count_gt_fast(&g_pool, t));
}
uint32_t s2r_probe_count_gt_affine(uint64_t t){
    if(!g_aff_ok || g_signed) return S2R_NA;
    return (uint32_t)s2r_affine_count_gt(&g_aff, (int64_t)t);
}
uint32_t s2r_probe_count_gt_blocked(uint64_t t){
    if(!g_blk_ok || g_signed) return S2R_NA;
    return (uint32_t)s2r_blocked_count_gt(&g_blk, t);
}
uint32_t s2r_probe_count_gt_s2r(uint64_t t){
    if(!g_signed){
        if(g_best == 2 && g_blk_ok) return (uint32_t)s2r_blocked_count_gt(&g_blk, t);
        if(g_best == 1 && g_aff_ok) return (uint32_t)s2r_affine_count_gt(&g_aff, (int64_t)t);
    }
    return s2r_probe_count_gt_flat(t);
}
uint32_t s2r_probe_count_range_index(uint64_t lo, uint64_t hi){
    int ok = 0;
    if(g_idx_ok){
        size_t c = s2r_index_count_range(&g_idx, &g_pool, (int64_t)lo, (int64_t)hi, &ok);
        if(ok) return (uint32_t)c;
    }
    return S2R_NA;                   /* o indice recusa em vez de adivinhar */
}
uint32_t s2r_probe_count_range_flat(uint64_t lo, uint64_t hi){
    if(!g_pool_ok) return S2R_NA;
    return (uint32_t)(g_signed
        ? s2r_count_range_signed_fast(&g_pool, (int64_t)lo, (int64_t)hi)
        : s2r_count_range_fast(&g_pool, lo, hi));
}
uint32_t s2r_probe_count_range_naive(uint64_t lo, uint64_t hi){
    size_t i, c = 0;
    if(!g_v) return S2R_NA;
    if(g_signed){ const int64_t *v = (const int64_t*)g_v;
        for(i = 0; i < g_n; i++) c += (v[i] >= (int64_t)lo && v[i] <= (int64_t)hi); }
    else { for(i = 0; i < g_n; i++) c += (g_v[i] >= lo && g_v[i] <= hi); }
    return (uint32_t)c;
}
const char *s2r_probe_version(void){ return S2R_VERSION_STRING; }
