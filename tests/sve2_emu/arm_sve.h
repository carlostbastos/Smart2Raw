/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* EMULATED arm_sve.h - implements in portable C ONLY the SVE ACLE intrinsics used by
 * Smart2Raw, with semantics faithful to the SVE specification. Vectors are modeled as
 * logical lane arrays and the predicate as a per-lane mask; the emulated vector length
 * is fixed (S2R_SVE_EMU_K 64-bit lanes) so strip-mining and tails are exercised on x86.
 * It lets us compile/RUN the header's real SVE functions here and validate that the
 * vector-length-agnostic logic gives the same total as the scalar path FOR ANY VL.
 * It does NOT validate the real-hardware intrinsic binding/codegen - that needs an
 * AArch64 SVE toolchain plus QEMU or SVE hardware.
 *
 * This is NOT a complete SVE implementation; it is a minimal, auditable shim.
 *
 * Lane geometry: the emulated vector is S2R_SVE_EMU_K 64-bit lanes, i.e.
 * VL = K*64 bits, so svcntd()=K, svcntw()=2K, svcnth()=4K, svcntb()=8K.
 * A single svbool_t models the predicate as one flag per ELEMENT of whatever
 * width the consuming intrinsic uses; svwhilelt_bN fills the lane count for that
 * width and the matching svld1 / reduction reads exactly that many. This mirrors
 * how a real SVE predicate register is reinterpreted per element size.
 */
#ifndef ARM_SVE_EMU_H
#define ARM_SVE_EMU_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef S2R_SVE_EMU_K
#define S2R_SVE_EMU_K 4   /* emulated 64-bit lanes (e.g. 256-bit SVE). Overridable. */
#endif

#define S2R_SVE_D (S2R_SVE_EMU_K)        /* u64 lanes */
#define S2R_SVE_W (S2R_SVE_EMU_K * 2)    /* u32 lanes */
#define S2R_SVE_H (S2R_SVE_EMU_K * 4)    /* u16 lanes */
#define S2R_SVE_B (S2R_SVE_EMU_K * 8)    /* u8  lanes */

typedef struct { uint8_t  v[S2R_SVE_B]; } svuint8_t;
typedef struct { uint16_t v[S2R_SVE_H]; } svuint16_t;
typedef struct { uint32_t v[S2R_SVE_W]; } svuint32_t;
typedef struct { uint64_t v[S2R_SVE_D]; } svuint64_t;
typedef struct { int      p[S2R_SVE_B]; } svbool_t;   /* sized to the widest lane count */

/* ---- element counts (vector-length agnostic in real SVE; fixed here) ---- */
static inline uint64_t svcntb(void){ return (uint64_t)S2R_SVE_B; }
static inline uint64_t svcnth(void){ return (uint64_t)S2R_SVE_H; }
static inline uint64_t svcntw(void){ return (uint64_t)S2R_SVE_W; }
static inline uint64_t svcntd(void){ return (uint64_t)S2R_SVE_D; }

/* ---- all-true predicates ---- */
static inline svbool_t svptrue_b8 (void){ svbool_t r; memset(&r,0,sizeof r); for(int j=0;j<S2R_SVE_B;j++) r.p[j]=1;
    return r;
}
static inline svbool_t svptrue_b16(void){ svbool_t r; memset(&r,0,sizeof r); for(int j=0;j<S2R_SVE_H;j++) r.p[j]=1;
    return r;
}
static inline svbool_t svptrue_b32(void){ svbool_t r; memset(&r,0,sizeof r); for(int j=0;j<S2R_SVE_W;j++) r.p[j]=1;
    return r;
}
static inline svbool_t svptrue_b64(void){ svbool_t r; memset(&r,0,sizeof r); for(int j=0;j<S2R_SVE_D;j++) r.p[j]=1;
    return r;
}

/* ---- WHILELT: lane j active iff i + j < n ---- */
static inline svbool_t svwhilelt_b8(uint64_t i, uint64_t n){
    svbool_t r; memset(&r,0,sizeof r);
    for(int j=0;j<S2R_SVE_B;j++) r.p[j] = (i+(uint64_t)j < n) ? 1 : 0;
    return r;
}
static inline svbool_t svwhilelt_b16(uint64_t i, uint64_t n){
    svbool_t r; memset(&r,0,sizeof r);
    for(int j=0;j<S2R_SVE_H;j++) r.p[j] = (i+(uint64_t)j < n) ? 1 : 0;
    return r;
}
static inline svbool_t svwhilelt_b32(uint64_t i, uint64_t n){
    svbool_t r; memset(&r,0,sizeof r);
    for(int j=0;j<S2R_SVE_W;j++) r.p[j] = (i+(uint64_t)j < n) ? 1 : 0;
    return r;
}
static inline svbool_t svwhilelt_b64(uint64_t i, uint64_t n){
    svbool_t r; memset(&r,0,sizeof r);
    for(int j=0;j<S2R_SVE_D;j++) r.p[j] = (i+(uint64_t)j < n) ? 1 : 0;
    return r;
}

/* ---- broadcast ---- */
static inline svuint8_t  svdup_n_u8 (uint8_t  x){ svuint8_t  r; for(int j=0;j<S2R_SVE_B;j++) r.v[j]=x;
    return r;
}
static inline svuint16_t svdup_n_u16(uint16_t x){ svuint16_t r; for(int j=0;j<S2R_SVE_H;j++) r.v[j]=x;
    return r;
}
static inline svuint32_t svdup_n_u32(uint32_t x){ svuint32_t r; for(int j=0;j<S2R_SVE_W;j++) r.v[j]=x;
    return r;
}
static inline svuint64_t svdup_n_u64(uint64_t x){ svuint64_t r; for(int j=0;j<S2R_SVE_D;j++) r.v[j]=x;
    return r;
}

/* ---- contiguous loads. SVE svld1 is ZEROING: inactive lanes read as 0, and the
 *      memory behind them is never accessed, so a partial final vector cannot
 *      fault past the end of the array. ---- */
static inline svuint8_t svld1_u8(svbool_t pg, const uint8_t *base){
    svuint8_t r; memset(&r,0,sizeof r);
    for(int j=0;j<S2R_SVE_B;j++) if(pg.p[j]) r.v[j]=base[j];
    return r; }
static inline svuint16_t svld1_u16(svbool_t pg, const uint16_t *base){
    svuint16_t r; memset(&r,0,sizeof r);
    for(int j=0;j<S2R_SVE_H;j++) if(pg.p[j]) r.v[j]=base[j];
    return r; }

/* ---- extending loads: bytes/halfwords -> u64, zero-extended, inactive lanes 0 ---- */
static inline svuint64_t svld1ub_u64(svbool_t pg, const uint8_t *base){
    svuint64_t r; memset(&r,0,sizeof r);
    for(int j=0;j<S2R_SVE_D;j++) r.v[j] = pg.p[j] ? (uint64_t)base[j] : 0;
    return r;
}
static inline svuint64_t svld1uh_u64(svbool_t pg, const uint16_t *base){
    svuint64_t r; memset(&r,0,sizeof r);
    for(int j=0;j<S2R_SVE_D;j++) r.v[j] = pg.p[j] ? (uint64_t)base[j] : 0;
    return r;
}

/* ---- merge add: active lanes = op1+op2 ; inactive lanes keep op1 ---- */
static inline svuint64_t svadd_u64_m(svbool_t pg, svuint64_t op1, svuint64_t op2){
    svuint64_t r; for(int j=0;j<S2R_SVE_D;j++) r.v[j] = pg.p[j] ? op1.v[j]+op2.v[j] : op1.v[j];
    return r;
}

/* ---- UDOT: 4-way widening dot product, unpredicated.
 *      u8 -> u32: destination lane k accumulates source elements 4k..4k+3.
 *      u16 -> u64: the same shape, one width up.
 *      Wraps modulo the destination width, exactly like the hardware, so a kernel
 *      that over-accumulates will produce a wrong total here too. ---- */
static inline svuint32_t svdot_u32(svuint32_t acc, svuint8_t op1, svuint8_t op2){
    svuint32_t r = acc;
    for(int k=0;k<S2R_SVE_W;k++){
        uint32_t s = 0;
        for(int e=0;e<4;e++) s += (uint32_t)op1.v[4*k+e] * (uint32_t)op2.v[4*k+e];
        r.v[k] = (uint32_t)(r.v[k] + s);
    }
    return r; }
static inline svuint64_t svdot_u64(svuint64_t acc, svuint16_t op1, svuint16_t op2){
    svuint64_t r = acc;
    for(int k=0;k<S2R_SVE_D;k++){
        uint64_t s = 0;
        for(int e=0;e<4;e++) s += (uint64_t)op1.v[4*k+e] * (uint64_t)op2.v[4*k+e];
        r.v[k] = r.v[k] + s;
    }
    return r; }

/* ---- UADDV: horizontal add over active lanes into a 64-bit scalar.
 *      The u32 form widening to uint64_t is the real ACLE signature, and it is
 *      what keeps the strip-mined u8 kernel from losing carries at the fold. ---- */
static inline uint64_t svaddv_u32(svbool_t pg, svuint32_t a){
    uint64_t s=0; for(int j=0;j<S2R_SVE_W;j++) if(pg.p[j]) s+=(uint64_t)a.v[j];
    return s;
}
static inline uint64_t svaddv_u64(svbool_t pg, svuint64_t a){
    uint64_t s=0; for(int j=0;j<S2R_SVE_D;j++) if(pg.p[j]) s+=a.v[j];
    return s;
}

#endif /* ARM_SVE_EMU_H */
