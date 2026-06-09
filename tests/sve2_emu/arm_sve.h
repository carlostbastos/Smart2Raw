/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
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
 * This is NOT a complete SVE implementation; it is a minimal, auditable shim. */
#ifndef ARM_SVE_EMU_H
#define ARM_SVE_EMU_H
#include <stdint.h>
#include <stddef.h>

#ifndef S2R_SVE_EMU_K
#define S2R_SVE_EMU_K 4   /* emulated 64-bit lanes (e.g. 256-bit SVE). Overridable. */
#endif

typedef struct { uint64_t v[S2R_SVE_EMU_K]; } svuint64_t;
typedef struct { int      p[S2R_SVE_EMU_K]; } svbool_t;

static inline uint64_t svcntd(void){ return S2R_SVE_EMU_K; }
static inline svbool_t svptrue_b64(void){ svbool_t r; for(int j=0;j<S2R_SVE_EMU_K;j++) r.p[j]=1; return r; }
static inline svbool_t svwhilelt_b64(uint64_t i, uint64_t n){
    svbool_t r; for(int j=0;j<S2R_SVE_EMU_K;j++) r.p[j] = (i+(uint64_t)j < n) ? 1 : 0; return r; }
static inline svuint64_t svdup_n_u64(uint64_t x){ svuint64_t r; for(int j=0;j<S2R_SVE_EMU_K;j++) r.v[j]=x; return r; }

/* extending loads: bytes/halfwords -> u64, zero-extended; inactive lanes = 0 */
static inline svuint64_t svld1ub_u64(svbool_t pg, const uint8_t *base){
    svuint64_t r; for(int j=0;j<S2R_SVE_EMU_K;j++) r.v[j] = pg.p[j] ? (uint64_t)base[j] : 0; return r; }
static inline svuint64_t svld1uh_u64(svbool_t pg, const uint16_t *base){
    svuint64_t r; for(int j=0;j<S2R_SVE_EMU_K;j++) r.v[j] = pg.p[j] ? (uint64_t)base[j] : 0; return r; }

/* merge add: active lanes = op1+op2 ; inactive lanes keep op1 */
static inline svuint64_t svadd_u64_m(svbool_t pg, svuint64_t op1, svuint64_t op2){
    svuint64_t r; for(int j=0;j<S2R_SVE_EMU_K;j++) r.v[j] = pg.p[j] ? op1.v[j]+op2.v[j] : op1.v[j]; return r; }

/* horizontal add over active lanes */
static inline uint64_t svaddv_u64(svbool_t pg, svuint64_t a){
    uint64_t s=0; for(int j=0;j<S2R_SVE_EMU_K;j++) if(pg.p[j]) s+=a.v[j]; return s; }

#endif /* ARM_SVE_EMU_H */
