/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* s2r_seg2.c - segment layer, v3.
 *
 * The two ideas are unchanged:
 *   1. CONSTANT SEGMENT (delta_bytes = 0): stores NO payload; zone stats answer
 *      any predicate in O(1).
 *   2. SORTED FLAG: a non-decreasing segment answers count_gt by binary search.
 *
 * v3 turns the library's own thesis on the segment layer's own metadata.
 *
 * v2 stored, per segment, five int64 fields plus an offset and two bytes - 48 to
 * 56 bytes regardless of what the data needed. That is precisely the habit the
 * README's "Core principle" exists to break ("Use int64 everywhere just to be
 * safe"), and it is not academic: at seg_size = 256 on a run-shaped column the
 * payload collapses to 4.5 KB while the metadata sits at 2.1 MB - the metadata is
 * 488x the data it describes, and the zone-map walk over it becomes the entire
 * cost of a query.
 *
 * So the metadata now goes through the same treatment as the payload:
 *
 *   - `base` is GONE. The frame of reference is the segment minimum, so base was
 *     a byte-for-byte duplicate of zmin.
 *   - `count` is GONE. Every segment holds seg_size elements except the last, so
 *     it is determined by the layout - the same argument s2r_blocked_bytes makes
 *     for PFOR offsets.
 *   - zmin/zmax, zsum and off are each stored in the SMALLEST NATIVE CLASS that
 *     holds their real range, chosen by s2r_classify / s2r_classify_signed_range
 *     - the library's own classifier, applied to the library's own bookkeeping.
 *   - zmin and zmax are interleaved in one array: the zone-map test reads both,
 *     so they belong on the same cache line.
 *
 * Two more things follow from v3.4.0 of the core:
 *
 *   - the residual scan no longer carries its own SIMD kernels. It borrows the
 *     shipped dispatch by wrapping each segment in a stack-local S2RPool view,
 *     exactly as s2r_blocked_sum_fast does. Less code here, and the segment layer
 *     inherits every future improvement to the core for free.
 *   - the same view gives sum over residuals via s2r_sum_fast.
 *
 * And the layer can finally persist: s2r2_save / s2r2_load implement the
 * "block-wise .s2r serialization" the ROADMAP asks for, as fmt = 2. A v3.3 reader
 * rejects it cleanly (both the class sentinel and the fmt byte fail its checks),
 * which is the correct outcome - a segmented file is not a flat pool.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "smart2raw.h"

#define SEG_SORTED 0x01
#define S2R_SEG_FMT 2u          /* format version of the segmented file */
#define S2R_SEG_CLASS_SENTINEL 0 /* class byte 0 = "segmented, per-segment classes" */

typedef struct {
    /* --- metadata, each array in the smallest class that fits --- */
    void    *zone;       /* interleaved zmin,zmax - 2*nseg elements, class zcls  */
    void    *zsum;       /* nseg elements, class scls                            */
    void    *zoff;       /* nseg elements, class ocls (unsigned)                 */
    uint8_t *dbytes;     /* nseg bytes: residual width, 0 = constant             */
    uint8_t *flags;      /* nseg bytes                                           */
    int8_t   zcls;       /* signed class of zone   (negative = signed)           */
    int8_t   scls;       /* signed class of zsum                                 */
    int8_t   ocls;       /* unsigned class of zoff                               */

    uint8_t *payload;    /* one contiguous, aligned allocation                   */
    size_t   payload_bytes;
    uint32_t nseg, nconst, nsorted;
    uint64_t nvals;
    uint32_t seg_size;
} Col2;

/* ------------------------------------------------------------------ */
/* typed metadata access. The class is per ARRAY, not per element, so every
 * loop below hoists its switch instead of paying one per access. */

static inline int64_t md_get_s(const void *a, int8_t cls, size_t i){
    switch(s2r_abs_size(cls)){
        case 8:  return ((const int8_t *)a)[i];
        case 16: return ((const int16_t*)a)[i];
        case 32: return ((const int32_t*)a)[i];
        default: return ((const int64_t*)a)[i];
    }
}
static inline void md_set_s(void *a, int8_t cls, size_t i, int64_t v){
    switch(s2r_abs_size(cls)){
        case 8:  ((int8_t *)a)[i]=(int8_t )v; break;
        case 16: ((int16_t*)a)[i]=(int16_t)v; break;
        case 32: ((int32_t*)a)[i]=(int32_t)v; break;
        default: ((int64_t*)a)[i]=v; break;
    }
}
static inline uint64_t md_get_u(const void *a, int8_t cls, size_t i){
    switch(s2r_abs_size(cls)){
        case 8:  return ((const uint8_t *)a)[i];
        case 16: return ((const uint16_t*)a)[i];
        case 32: return ((const uint32_t*)a)[i];
        default: return ((const uint64_t*)a)[i];
    }
}
static inline void md_set_u(void *a, int8_t cls, size_t i, uint64_t v){
    switch(s2r_abs_size(cls)){
        case 8:  ((uint8_t *)a)[i]=(uint8_t )v; break;
        case 16: ((uint16_t*)a)[i]=(uint16_t)v; break;
        case 32: ((uint32_t*)a)[i]=(uint32_t)v; break;
        default: ((uint64_t*)a)[i]=v; break;
    }
}
static inline size_t md_bytes(int8_t cls){ return s2r_abs_size(cls) >> 3; }

static size_t seg_count(const Col2 *c, uint32_t i){
    size_t off = (size_t)i * c->seg_size;
    size_t rest = (size_t)c->nvals - off;
    return rest < c->seg_size ? rest : c->seg_size;
}

static uint8_t width_for2(uint64_t maxd){
    if (maxd == 0)             return 0;
    if (maxd <= 0xFFull)       return 1;
    if (maxd <= 0xFFFFull)     return 2;
    if (maxd <= 0xFFFFFFFFull) return 4;
    return 8;
}

static void col_release(Col2 *c){
    if (!c) return;
    if (c->payload) S2R_ALIGNED_FREE(c->payload);
    free(c->zone); free(c->zsum); free(c->zoff);
    free(c->dbytes); free(c->flags);
    free(c);
}
void s2r2_free(Col2 *c){ col_release(c); }

/* ------------------------------------------------------------------ */

Col2 *s2r2_build(const int64_t *v, size_t n, uint32_t seg_size){
    if (seg_size == 0) seg_size = 65536;
    if (n && !v) return NULL;

    Col2 *c = (Col2*)calloc(1, sizeof(Col2));
    if (!c) return NULL;
    c->nvals = n;
    c->seg_size = seg_size;
    c->nseg = (uint32_t)((n + seg_size - 1) / seg_size);
    c->zcls = S2R_I8; c->scls = S2R_I8; c->ocls = S2R_8;
    if (c->nseg == 0) return c;                 /* empty column is legal */

    /* Pass 1: gather each segment's range and sum into scratch, and learn the
     * real ranges of the METADATA itself so it can be classified too. */
    int64_t *tmin = (int64_t*)malloc((size_t)c->nseg*sizeof(int64_t));
    int64_t *tmax = (int64_t*)malloc((size_t)c->nseg*sizeof(int64_t));
    int64_t *tsum = (int64_t*)malloc((size_t)c->nseg*sizeof(int64_t));
    size_t  *toff = (size_t*) malloc((size_t)c->nseg*sizeof(size_t));
    c->dbytes = (uint8_t*)malloc(c->nseg);
    c->flags  = (uint8_t*)malloc(c->nseg);
    if(!tmin||!tmax||!tsum||!toff||!c->dbytes||!c->flags){
        free(tmin);free(tmax);free(tsum);free(toff); col_release(c); return NULL;
    }

    int64_t gmin=0,gmax=0,smin=0,smax=0;
    size_t total=0;
    for (uint32_t si = 0; si < c->nseg; si++){
        size_t off = (size_t)si * seg_size;
        size_t cnt = (off + seg_size <= n) ? seg_size : (n - off);
        const int64_t *p = v + off;
        int64_t mn=p[0], mx=p[0];
        uint64_t sum=0;                          /* unsigned: signed overflow is UB */
        int sorted = 1;
        for (size_t i = 0; i < cnt; i++){
            int64_t x = p[i];
            if (x < mn) mn = x;
            if (x > mx) mx = x;
            sum += (uint64_t)x;
            if (i && p[i] < p[i-1]) sorted = 0;
        }
        int64_t ssum = (int64_t)sum;
        tmin[si]=mn; tmax[si]=mx; tsum[si]=ssum;
        c->flags[si] = sorted ? SEG_SORTED : 0;
        if (sorted) c->nsorted++;
        uint8_t w = width_for2((uint64_t)mx - (uint64_t)mn);   /* unsigned: no UB */
        c->dbytes[si] = w;
        if (w == 0){ c->nconst++; toff[si]=0; }
        else { size_t at = s2r_align_up(total, 8); toff[si]=at; total = at + cnt*(size_t)w; }
        if (si == 0){ gmin=mn; gmax=mx; smin=ssum; smax=ssum; }
        else {
            if(mn<gmin)gmin=mn;
            if(mx>gmax)gmax=mx;
            if(ssum<smin)smin=ssum;
            if(ssum>smax)smax=ssum;
        }
    }

    /* THE POINT OF v3: classify the metadata with the library's own classifier. */
    c->zcls = s2r_classify_signed_range(gmin, gmax);
    c->scls = s2r_classify_signed_range(smin, smax);
    c->ocls = (int8_t)s2r_classify(total ? (uint64_t)total : 0);

    c->zone = calloc((size_t)c->nseg*2, md_bytes(c->zcls));
    c->zsum = calloc((size_t)c->nseg,   md_bytes(c->scls));
    c->zoff = calloc((size_t)c->nseg,   md_bytes(c->ocls));
    if(!c->zone||!c->zsum||!c->zoff){
        free(tmin);free(tmax);free(tsum);free(toff); col_release(c); return NULL;
    }
    for (uint32_t si = 0; si < c->nseg; si++){
        md_set_s(c->zone, c->zcls, (size_t)si*2,   tmin[si]);
        md_set_s(c->zone, c->zcls, (size_t)si*2+1, tmax[si]);
        md_set_s(c->zsum, c->scls, si, tsum[si]);
        md_set_u(c->zoff, c->ocls, si, (uint64_t)toff[si]);
    }

    if (total){
        c->payload = (uint8_t*)S2R_ALIGNED_ALLOC(s2r_align_up(total,S2R_ALIGNMENT), S2R_ALIGNMENT);
        if(!c->payload){ free(tmin);free(tmax);free(tsum);free(toff); col_release(c); return NULL; }
    }
    c->payload_bytes = total;

    /* Pass 2: write residuals, width switch hoisted out of the element loop. */
    for (uint32_t si = 0; si < c->nseg; si++){
        uint8_t w = c->dbytes[si];
        if (!w) continue;
        size_t off = (size_t)si*seg_size, cnt = seg_count(c, si);
        const int64_t *p = v + off;
        uint64_t base = (uint64_t)tmin[si];
        void *d = c->payload + toff[si];
        switch(w){
            case 1: { uint8_t  *a=(uint8_t*) d; for(size_t i=0;i<cnt;i++) a[i]=(uint8_t) ((uint64_t)p[i]-base); } break;
            case 2: { uint16_t *a=(uint16_t*)d; for(size_t i=0;i<cnt;i++) a[i]=(uint16_t)((uint64_t)p[i]-base); } break;
            case 4: { uint32_t *a=(uint32_t*)d; for(size_t i=0;i<cnt;i++) a[i]=(uint32_t)((uint64_t)p[i]-base); } break;
            default:{ uint64_t *a=(uint64_t*)d; for(size_t i=0;i<cnt;i++) a[i]=          ((uint64_t)p[i]-base); } break;
        }
    }
    free(tmin); free(tmax); free(tsum); free(toff);
    return c;
}

/* ------------------------------------------------------------------ */
/* metadata-only aggregates: O(nseg), never touch the payload */

int64_t s2r2_sum(const Col2 *c){
    if (!c) return 0;
    uint64_t s = 0;                              /* unsigned: signed overflow is UB */
    switch (s2r_abs_size(c->scls)){              /* switch hoisted out of the loop */
        case 8:  { const int8_t *a=(const int8_t*) c->zsum; for(uint32_t i=0;i<c->nseg;i++) s+=(uint64_t)a[i]; } break;
        case 16: { const int16_t*a=(const int16_t*)c->zsum; for(uint32_t i=0;i<c->nseg;i++) s+=(uint64_t)a[i]; } break;
        case 32: { const int32_t*a=(const int32_t*)c->zsum; for(uint32_t i=0;i<c->nseg;i++) s+=(uint64_t)a[i]; } break;
        default: { const int64_t*a=(const int64_t*)c->zsum; for(uint32_t i=0;i<c->nseg;i++) s+=(uint64_t)a[i]; } break;
    }
    return (int64_t)s;
}

void s2r2_minmax(const Col2 *c, int64_t *mn, int64_t *mx){
    if (!mn || !mx) return;
    if (!c || c->nseg == 0){ *mn = 0; *mx = 0; return; }
    int64_t lo, hi;
#define S2R_MM_LOOP(T) do{ const T *a=(const T*)c->zone; lo=a[0]; hi=a[1]; \
        for(uint32_t i=1;i<c->nseg;i++){ if(a[2*i]<lo) lo=a[2*i]; if(a[2*i+1]>hi) hi=a[2*i+1]; } }while(0)
    switch (s2r_abs_size(c->zcls)){
        case 8:  S2R_MM_LOOP(int8_t);  break;
        case 16: S2R_MM_LOOP(int16_t); break;
        case 32: S2R_MM_LOOP(int32_t); break;
        default: S2R_MM_LOOP(int64_t); break;
    }
#undef S2R_MM_LOOP
    *mn = lo; *mx = hi;
}

/* ------------------------------------------------------------------ */
/* binary search over a sorted segment.
 * Deltas are v - base with base constant per segment, so they are non-decreasing
 * exactly when the values are. `lines` receives the number of DISTINCT 64-byte
 * lines actually probed - counted, never modelled. */

static size_t upper_idx(const Col2 *c, uint32_t si, uint64_t kp, uint64_t *lines){
    size_t off = (size_t)md_get_u(c->zoff, c->ocls, si);
    const uint8_t *d = c->payload + off;
    uint8_t w = c->dbytes[si];
    size_t lo = 0, hi = seg_count(c, si);
    size_t seen[64]; unsigned nseen = 0;
    while (lo < hi){
        size_t mid = lo + ((hi - lo) >> 1);
        if (lines){                              /* instrumentation costs nothing
                                                  * when nobody asked for it */
            size_t line = (off + mid*(size_t)w) >> 6;
            unsigned j = 0;
            while (j < nseen && seen[j] != line) j++;
            if (j == nseen && nseen < 64) seen[nseen++] = line;
        }
        uint64_t val;
        switch (w){
            case 1:  val = ((const uint8_t *)d)[mid]; break;
            case 2:  val = ((const uint16_t*)d)[mid]; break;
            case 4:  val = ((const uint32_t*)d)[mid]; break;
            default: val = ((const uint64_t*)d)[mid]; break;
        }
        if (val > kp) hi = mid; else lo = mid + 1;
    }
    if (lines) *lines += nseen;
    return lo;
}

/* Residual scan: borrow the core's shipped predicate dispatch (v3.4.0) through a
 * stack-local S2RPool view, the same zero-copy trick as s2r_blocked_sum_fast.
 * No SIMD kernels live in this file any more. */
static uint64_t seg_scan_gt(const Col2 *c, uint32_t si, uint64_t kp){
    S2RPool view; memset(&view, 0, sizeof view);
    size_t cnt = seg_count(c, si);
    view.data     = c->payload + (size_t)md_get_u(c->zoff, c->ocls, si);
    view.size     = (int8_t)(c->dbytes[si] * 8);
    view.count    = cnt;
    view.capacity = cnt;
    view.byte_cap = cnt * c->dbytes[si];
    view.flags    = S2R_FLAG_EXTERNAL;
    return (uint64_t)s2r_count_gt_fast(&view, kp);
}

/* scanned_bytes is an EXACT count of what the predicate read: the zone entries
 * the scan walks, the cold metadata of every segment the zone map could not
 * resolve, the distinct cache lines each binary search probed, and the residual
 * bytes actually compared. */
uint64_t s2r2_count_gt(const Col2 *c, int64_t k, uint64_t *scanned_bytes){
    if (!c || c->nseg == 0){ if(scanned_bytes) *scanned_bytes = 0; return 0; }
    uint64_t cnt = 0, lines = 0;
    const size_t zb = md_bytes(c->zcls);
    uint64_t touched = (uint64_t)c->nseg * 2 * zb;    /* the zone-map walk */
    const size_t ob = md_bytes(c->ocls);

#define S2R_ZONE_SCAN(T) do{                                                    \
    const T *z = (const T*)c->zone;                                             \
    for (uint32_t i = 0; i < c->nseg; i++){                                     \
        int64_t zmin = z[2*i], zmax = z[2*i+1];                                 \
        if (zmax <= k) continue;                                                \
        if (zmin >  k){ cnt += seg_count(c, i); continue; }                     \
        touched += 2 + ob;                       /* dbytes+flags+off */         \
        uint64_t kp = (uint64_t)k - (uint64_t)zmin;                             \
        if (c->flags[i] & SEG_SORTED){                                          \
            size_t idx = upper_idx(c, i, kp, scanned_bytes ? &lines : NULL);    \
            cnt += (uint64_t)seg_count(c, i) - idx;                             \
            continue;                                                           \
        }                                                                       \
        touched += (uint64_t)seg_count(c, i) * c->dbytes[i];                    \
        cnt += seg_scan_gt(c, i, kp);                                           \
    } }while(0)

    switch (s2r_abs_size(c->zcls)){
        case 8:  S2R_ZONE_SCAN(int8_t);  break;
        case 16: S2R_ZONE_SCAN(int16_t); break;
        case 32: S2R_ZONE_SCAN(int32_t); break;
        default: S2R_ZONE_SCAN(int64_t); break;
    }
#undef S2R_ZONE_SCAN
    if (scanned_bytes) *scanned_bytes = touched + lines * 64;
    return cnt;
}

/* Sum by scanning the residuals, for cross-checking the metadata-only sum.
 * SUM(base + delta) = base*count + SUM(delta); the frame of reference folds out
 * into one multiply per segment, and SUM(delta) comes from the core's dispatch. */
int64_t s2r2_sum_scan(const Col2 *c){
    if (!c) return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < c->nseg; i++){
        size_t cnt = seg_count(c, i);
        int64_t base = md_get_s(c->zone, c->zcls, (size_t)i*2);
        total += (uint64_t)base * (uint64_t)cnt;
        if (c->dbytes[i] == 0) continue;
        S2RPool view; memset(&view, 0, sizeof view);
        view.data     = c->payload + (size_t)md_get_u(c->zoff, c->ocls, i);
        view.size     = (int8_t)(c->dbytes[i] * 8);
        view.count    = cnt; view.capacity = cnt;
        view.byte_cap = cnt * c->dbytes[i];
        view.flags    = S2R_FLAG_EXTERNAL;
        total += s2r_sum_fast(&view);
    }
    return (int64_t)total;
}

/* ------------------------------------------------------------------ */
/* Block-wise .s2r serialization (ROADMAP: "block-wise .s2r serialization").
 *
 * fmt = 2, class byte = 0. Both are rejected by a v3.3 reader - deliberately: a
 * segmented file is not a flat pool and must not be silently misread as one.
 * All multibyte fields are canonical little-endian; CRC32 covers everything after
 * the fixed header, metadata included, so a corrupted zone map is caught too.
 *
 *   0   4  magic "SR33"
 *   4   1  class    = 0 (segmented sentinel)
 *   5   1  flags    bit 0 = signed data
 *   6   1  fmt      = 2
 *   7   1  rsvd     = 0
 *   8   8  nvals    u64
 *  16   4  seg_size u32
 *  20   4  nseg     u32
 *  24   1  zcls     int8   (signed class of the interleaved zmin/zmax array)
 *  25   1  scls     int8   (signed class of zsum)
 *  26   1  ocls     int8   (unsigned class of the offsets)
 *  27   1  rsvd2    = 0
 *  28   8  payload_bytes u64
 *  36   .  zone[2*nseg] · zsum[nseg] · zoff[nseg] · dbytes[nseg] · flags[nseg]
 *       .  payload[payload_bytes]
 *       4  crc32 of everything from offset 36 to the end of the payload
 */
#if S2R_HAS_STDIO

static int wr_le(FILE *f, uint64_t v, int bytes){
    uint8_t b[8]; for(int i=0;i<bytes;i++) b[i]=(uint8_t)(v>>(8*i));
    return fwrite(b,(size_t)bytes,1,f)==1;
}
static int rd_le(FILE *f, uint64_t *v, int bytes){
    uint8_t b[8]; if(fread(b,(size_t)bytes,1,f)!=1) return 0;
    *v=0; for(int i=0;i<bytes;i++) *v |= (uint64_t)b[i]<<(8*i);
    return 1;
}
/* CRC is computed over a staged buffer so the streaming stays simple and the
 * value is identical on any host. */
static uint8_t *stage_body(const Col2 *c, size_t *out_len){
    size_t zb=md_bytes(c->zcls), sb=md_bytes(c->scls), ob=md_bytes(c->ocls);
    size_t len = (size_t)c->nseg*2*zb + (size_t)c->nseg*sb + (size_t)c->nseg*ob
               + (size_t)c->nseg*2 + c->payload_bytes;
    uint8_t *buf = (uint8_t*)malloc(len ? len : 1);
    if(!buf) return NULL;
    size_t at = 0;
    for(size_t i=0;i<(size_t)c->nseg*2;i++){
        uint64_t raw=(uint64_t)md_get_s(c->zone,c->zcls,i);
        for(size_t j=0;j<zb;j++) buf[at++]=(uint8_t)(raw>>(8*j));
    }
    for(size_t i=0;i<c->nseg;i++){
        uint64_t raw=(uint64_t)md_get_s(c->zsum,c->scls,i);
        for(size_t j=0;j<sb;j++) buf[at++]=(uint8_t)(raw>>(8*j));
    }
    for(size_t i=0;i<c->nseg;i++){
        uint64_t raw=md_get_u(c->zoff,c->ocls,i);
        for(size_t j=0;j<ob;j++) buf[at++]=(uint8_t)(raw>>(8*j));
    }
    memcpy(buf+at, c->dbytes, c->nseg); at+=c->nseg;
    memcpy(buf+at, c->flags,  c->nseg); at+=c->nseg;
    if(c->payload_bytes){
        /* residuals are stored in native order; convert to canonical LE */
        memcpy(buf+at, c->payload, c->payload_bytes);
#if !S2R_LITTLE_ENDIAN
        {   size_t p=at;
            for(uint32_t si=0; si<c->nseg; si++){
                uint8_t w=c->dbytes[si]; if(!w) continue;
                size_t o=(size_t)md_get_u(c->zoff,c->ocls,si), cnt=seg_count(c,si);
                if(w>1) s2r_swap_payload(buf+p+o, w, cnt);
            }
        }
#endif
        at+=c->payload_bytes;
    }
    *out_len = at;
    return buf;
}

int s2r2_save(const Col2 *c, const char *path, int is_signed_data){
    if(!c||!path) return 0;
    size_t blen=0; uint8_t *body = stage_body(c, &blen);
    if(!body) return 0;
    FILE *f = fopen(path, "wb");
    if(!f){ free(body); return 0; }
    uint8_t hdr[4] = { (uint8_t)S2R_SEG_CLASS_SENTINEL,
                       (uint8_t)(is_signed_data ? S2R_FLAG_SIGNED : 0),
                       (uint8_t)S2R_SEG_FMT, 0 };
    int ok = wr_le(f, 0x33335253u, 4)
          && fwrite(hdr,4,1,f)==1
          && wr_le(f, c->nvals, 8)
          && wr_le(f, c->seg_size, 4)
          && wr_le(f, c->nseg, 4)
          && fwrite((uint8_t[]){ (uint8_t)c->zcls,(uint8_t)c->scls,(uint8_t)c->ocls,0 },4,1,f)==1
          && wr_le(f, c->payload_bytes, 8);
    if(ok && blen) ok = fwrite(body, blen, 1, f)==1;
    uint32_t crc = blen ? s2r_crc32(body, blen, 0) : 0;
    if(ok) ok = wr_le(f, crc, 4);
    free(body); fclose(f);
    return ok;
}

Col2 *s2r2_load(const char *path, int *is_signed_data){
    FILE *f = fopen(path,"rb"); if(!f) return NULL;
    uint64_t magic=0; uint8_t hdr[4];
    if(!rd_le(f,&magic,4) || magic!=0x33335253u){ fclose(f); return NULL; }
    if(fread(hdr,4,1,f)!=1){ fclose(f); return NULL; }
    if(hdr[0]!=S2R_SEG_CLASS_SENTINEL || hdr[2]!=S2R_SEG_FMT || hdr[3]!=0){ fclose(f); return NULL; }
    if(is_signed_data) *is_signed_data = (hdr[1] & S2R_FLAG_SIGNED) ? 1 : 0;
    uint64_t nvals=0, segsz=0, nseg=0, pbytes=0; uint8_t cls[4];
    if(!rd_le(f,&nvals,8)||!rd_le(f,&segsz,4)||!rd_le(f,&nseg,4)){ fclose(f); return NULL; }
    if(fread(cls,4,1,f)!=1 || cls[3]!=0){ fclose(f); return NULL; }
    if(!rd_le(f,&pbytes,8)){ fclose(f); return NULL; }
    if(segsz==0 || nseg > 0xFFFFFFFFull || nvals > (uint64_t)SIZE_MAX
       || pbytes > (uint64_t)SIZE_MAX){ fclose(f); return NULL; }
    /* nseg must agree with nvals and seg_size - do not trust the header */
    if(nseg != (nvals + segsz - 1)/segsz){ fclose(f); return NULL; }

    Col2 *c = (Col2*)calloc(1,sizeof(Col2));
    if(!c){ fclose(f); return NULL; }
    c->nvals=nvals; c->seg_size=(uint32_t)segsz; c->nseg=(uint32_t)nseg;
    c->zcls=(int8_t)cls[0]; c->scls=(int8_t)cls[1]; c->ocls=(int8_t)cls[2];
    c->payload_bytes=(size_t)pbytes;
    size_t za=s2r_abs_size(c->zcls), sa=s2r_abs_size(c->scls), oa=s2r_abs_size(c->ocls);
    if(!(za==8||za==16||za==32||za==64)||!(sa==8||sa==16||sa==32||sa==64)
       ||!(oa==8||oa==16||oa==32||oa==64)||c->zcls>0||c->scls>0||c->ocls<0){
        fclose(f); col_release(c); return NULL;
    }
    if(c->nseg==0){ fclose(f); return c; }

    size_t zb=md_bytes(c->zcls), sb=md_bytes(c->scls), ob=md_bytes(c->ocls);
    size_t blen=(size_t)c->nseg*2*zb+(size_t)c->nseg*sb+(size_t)c->nseg*ob
               +(size_t)c->nseg*2+c->payload_bytes;
    uint8_t *body=(uint8_t*)malloc(blen?blen:1);
    if(!body){ fclose(f); col_release(c); return NULL; }
    if(blen && fread(body,blen,1,f)!=1){ free(body); fclose(f); col_release(c); return NULL; }
    uint64_t crc_disk=0;
    if(!rd_le(f,&crc_disk,4)){ free(body); fclose(f); col_release(c); return NULL; }
    if(fgetc(f)!=EOF){ free(body); fclose(f); col_release(c); return NULL; }  /* exact length */
    fclose(f);
    if((uint32_t)crc_disk != (blen ? s2r_crc32(body,blen,0) : 0)){
        free(body); col_release(c); return NULL;
    }

    c->zone=calloc((size_t)c->nseg*2, zb);
    c->zsum=calloc((size_t)c->nseg, sb);
    c->zoff=calloc((size_t)c->nseg, ob);
    c->dbytes=(uint8_t*)malloc(c->nseg);
    c->flags =(uint8_t*)malloc(c->nseg);
    if(!c->zone||!c->zsum||!c->zoff||!c->dbytes||!c->flags){
        free(body); col_release(c); return NULL;
    }
    size_t at=0;
    for(size_t i=0;i<(size_t)c->nseg*2;i++){
        uint64_t raw=0; for(size_t j=0;j<zb;j++) raw|=(uint64_t)body[at+j]<<(8*j);
        at+=zb;
        /* sign-extend from the stored width */
        int64_t sv = (zb==8)?(int64_t)raw
                   : (int64_t)((raw & (1ull<<(zb*8-1))) ? (raw | ~((1ull<<(zb*8))-1)) : raw);
        md_set_s(c->zone,c->zcls,i,sv);
    }
    for(size_t i=0;i<c->nseg;i++){
        uint64_t raw=0; for(size_t j=0;j<sb;j++) raw|=(uint64_t)body[at+j]<<(8*j);
        at+=sb;
        int64_t sv = (sb==8)?(int64_t)raw
                   : (int64_t)((raw & (1ull<<(sb*8-1))) ? (raw | ~((1ull<<(sb*8))-1)) : raw);
        md_set_s(c->zsum,c->scls,i,sv);
    }
    for(size_t i=0;i<c->nseg;i++){
        uint64_t raw=0; for(size_t j=0;j<ob;j++) raw|=(uint64_t)body[at+j]<<(8*j);
        at+=ob;
        if(raw > (uint64_t)c->payload_bytes){ free(body); col_release(c); return NULL; }
        md_set_u(c->zoff,c->ocls,i,raw);
    }
    memcpy(c->dbytes, body+at, c->nseg); at+=c->nseg;
    memcpy(c->flags,  body+at, c->nseg); at+=c->nseg;
    for(uint32_t i=0;i<c->nseg;i++){
        uint8_t w=c->dbytes[i];
        if(!(w==0||w==1||w==2||w==4||w==8)){ free(body); col_release(c); return NULL; }
        if(w){
            size_t o=(size_t)md_get_u(c->zoff,c->ocls,i), cnt=seg_count(c,i);
            if(o + cnt*(size_t)w > c->payload_bytes){ free(body); col_release(c); return NULL; }
        }
        c->nconst += (w==0);
        c->nsorted += (c->flags[i] & SEG_SORTED) ? 1 : 0;
    }
    if(c->payload_bytes){
        c->payload=(uint8_t*)S2R_ALIGNED_ALLOC(s2r_align_up(c->payload_bytes,S2R_ALIGNMENT),S2R_ALIGNMENT);
        if(!c->payload){ free(body); col_release(c); return NULL; }
        memcpy(c->payload, body+at, c->payload_bytes);
#if !S2R_LITTLE_ENDIAN
        for(uint32_t si=0; si<c->nseg; si++){
            uint8_t w=c->dbytes[si]; if(w<2) continue;
            size_t o=(size_t)md_get_u(c->zoff,c->ocls,si), cnt=seg_count(c,si);
            s2r_swap_payload(c->payload+o, w, cnt);
        }
#endif
    }
    free(body);
    return c;
}
#endif /* S2R_HAS_STDIO */

/* ------------------------------------------------------------------ */

uint64_t s2r2_bytes(const Col2 *c){ return c ? c->payload_bytes : 0; }
uint32_t s2r2_nseg(const Col2 *c){ return c ? c->nseg : 0; }
uint32_t s2r2_nconst(const Col2 *c){ return c ? c->nconst : 0; }
uint32_t s2r2_nsorted(const Col2 *c){ return c ? c->nsorted : 0; }

/* Real resident metadata, measured from the classes actually chosen - not a
 * magic constant. v2 returned nseg*42 while its struct was 56 bytes wide. */
uint64_t s2r2_meta_bytes(const Col2 *c){
    if(!c) return 0;
    return (uint64_t)c->nseg * (2*md_bytes(c->zcls) + md_bytes(c->scls)
                                + md_bytes(c->ocls) + 2);
}
/* the classes the library's own classifier picked for the bookkeeping */
void s2r2_meta_classes(const Col2 *c, int *zbits, int *sbits, int *obits){
    if(!c) return;
    if(zbits) *zbits = (int)s2r_abs_size(c->zcls);
    if(sbits) *sbits = (int)s2r_abs_size(c->scls);
    if(obits) *obits = (int)s2r_abs_size(c->ocls);
}
