/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* EMULATED riscv_vector.h - implements in portable C ONLY the RVV 1.0 intrinsics
 * used by Smart2Raw, with semantics faithful to the RVV specification. Vectors are
 * modeled as logical lane arrays; the emulated VLEN is fixed (S2R_RVV_EMU_K lanes
 * for e64m1) so the strip-mining and tail handling are exercised on this x86 host.
 * It lets us compile/RUN the header's real RVV functions here and validate that the
 * vector-length-agnostic logic computes the same total as the scalar path, FOR ANY
 * VLEN. It does NOT validate the real-hardware intrinsic binding or codegen - that
 * requires an rv64gcv toolchain (gcc/clang) plus QEMU or RISC-V hardware.
 *
 * This is NOT a complete RVV implementation; it is a minimal, auditable shim.
 *
 * Lane geometry. S2R_RVV_EMU_K is VLEN/64, i.e. the element count of e64m1.
 * The kernels accumulate in u64m8 fed from u8m1 / u16m2, and all three share the
 * same VLMAX = VLEN/8 = 8K elements:
 *     e8m1  : VLEN/8      = 8K
 *     e16m2 : 2 * VLEN/16 = 8K
 *     e64m8 : 8 * VLEN/64 = 8K
 * That equality is exactly what makes the vf8 / vf4 zero-extensions land in one
 * register group with a matching element count.
 */
#ifndef RISCV_VECTOR_EMU_H
#define RISCV_VECTOR_EMU_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Emulated VLEN/64. Overridable so the test can prove the logic for several
 * vector lengths (K=2 -> VLEN=128, K=8 -> VLEN=512, ...). */
#ifndef S2R_RVV_EMU_K
#define S2R_RVV_EMU_K 8
#endif

#define S2R_RVV_WIDE (S2R_RVV_EMU_K * 8)   /* VLMAX of e8m1 / e16m2 / e64m8 */

typedef struct { uint8_t  v[S2R_RVV_WIDE]; }  vuint8m1_t;
typedef struct { uint16_t v[S2R_RVV_WIDE]; }  vuint16m2_t;
typedef struct { uint64_t v[S2R_RVV_WIDE]; }  vuint64m8_t;
typedef struct { uint64_t v[S2R_RVV_EMU_K]; } vuint64m1_t;

/* legacy fractional-LMUL types, kept so older callers still compile */
typedef struct { uint8_t  v[S2R_RVV_EMU_K]; } vuint8mf8_t;
typedef struct { uint16_t v[S2R_RVV_EMU_K]; } vuint16mf4_t;

/* ---- vsetvl: clamp the requested application vector length to VLMAX ---- */
static inline size_t __riscv_vsetvlmax_e64m1(void){ return S2R_RVV_EMU_K; }
static inline size_t __riscv_vsetvlmax_e64m8(void){ return S2R_RVV_WIDE; }
static inline size_t __riscv_vsetvl_e8m1  (size_t avl){ return avl < S2R_RVV_WIDE  ? avl : S2R_RVV_WIDE; }
static inline size_t __riscv_vsetvl_e16m2 (size_t avl){ return avl < S2R_RVV_WIDE  ? avl : S2R_RVV_WIDE; }
static inline size_t __riscv_vsetvl_e8mf8 (size_t avl){ return avl < S2R_RVV_EMU_K ? avl : S2R_RVV_EMU_K; }
static inline size_t __riscv_vsetvl_e16mf4(size_t avl){ return avl < S2R_RVV_EMU_K ? avl : S2R_RVV_EMU_K; }

/* ---- unit-stride loads: only the first vl elements are read ---- */
static inline vuint8m1_t __riscv_vle8_v_u8m1(const uint8_t *p, size_t vl){
    vuint8m1_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=p[i];
    return r;
}
static inline vuint16m2_t __riscv_vle16_v_u16m2(const uint16_t *p, size_t vl){
    vuint16m2_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=p[i];
    return r;
}
static inline vuint8mf8_t __riscv_vle8_v_u8mf8(const uint8_t *p, size_t vl){
    vuint8mf8_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=p[i];
    return r;
}
static inline vuint16mf4_t __riscv_vle16_v_u16mf4(const uint16_t *p, size_t vl){
    vuint16mf4_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=p[i];
    return r;
}

/* ---- splat ---- */
static inline vuint64m1_t __riscv_vmv_v_x_u64m1(uint64_t x, size_t vl){
    vuint64m1_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=x;
    return r;
}
static inline vuint64m8_t __riscv_vmv_v_x_u64m8(uint64_t x, size_t vl){
    vuint64m8_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=x;
    return r;
}

/* ---- zero-extend: u8 -> u64 (vf8) and u16 -> u64 (vf4) ---- */
static inline vuint64m8_t __riscv_vzext_vf8_u64m8(vuint8m1_t a, size_t vl){
    vuint64m8_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=(uint64_t)a.v[i];
    return r;
}
static inline vuint64m8_t __riscv_vzext_vf4_u64m8(vuint16m2_t a, size_t vl){
    vuint64m8_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=(uint64_t)a.v[i];
    return r;
}
static inline vuint64m1_t __riscv_vzext_vf8_u64m1(vuint8mf8_t a, size_t vl){
    vuint64m1_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=(uint64_t)a.v[i];
    return r;
}
static inline vuint64m1_t __riscv_vzext_vf4_u64m1(vuint16mf4_t a, size_t vl){
    vuint64m1_t r; memset(&r,0,sizeof r);
    for(size_t i=0;i<vl;i++) r.v[i]=(uint64_t)a.v[i];
    return r;
}

/* ---- tail-undisturbed add: lanes [0,vl) = vs2+vs1 ; lanes [vl,VLMAX) keep vd.
 *      The _tu policy is load-bearing: under the default tail-agnostic policy the
 *      inactive lanes may be overwritten with an implementation-defined value,
 *      destroying the partial sums that every previous full-width iteration
 *      accumulated there. ---- */
static inline vuint64m1_t __riscv_vadd_vv_u64m1_tu(vuint64m1_t vd, vuint64m1_t vs2, vuint64m1_t vs1, size_t vl){
    vuint64m1_t r = vd;
    for(size_t i=0;i<vl;i++) r.v[i]=vs2.v[i]+vs1.v[i];
    return r;
}
static inline vuint64m8_t __riscv_vadd_vv_u64m8_tu(vuint64m8_t vd, vuint64m8_t vs2, vuint64m8_t vs1, size_t vl){
    vuint64m8_t r = vd;
    for(size_t i=0;i<vl;i++) r.v[i]=vs2.v[i]+vs1.v[i];
    return r;
}

/* ---- reduction sum: result lane 0 = scalar[0] + sum(vec[0..vl)) ---- */
static inline vuint64m1_t __riscv_vredsum_vs_u64m1_u64m1(vuint64m1_t vec, vuint64m1_t scal, size_t vl){
    uint64_t s = scal.v[0];
    for(size_t i=0;i<vl;i++) s+=vec.v[i];
    vuint64m1_t r; memset(&r,0,sizeof r); r.v[0]=s;
    return r;
}
static inline vuint64m1_t __riscv_vredsum_vs_u64m8_u64m1(vuint64m8_t vec, vuint64m1_t scal, size_t vl){
    uint64_t s = scal.v[0];
    for(size_t i=0;i<vl;i++) s+=vec.v[i];
    vuint64m1_t r; memset(&r,0,sizeof r); r.v[0]=s;
    return r;
}

static inline uint64_t __riscv_vmv_x_s_u64m1_u64(vuint64m1_t a){ return a.v[0]; }

#endif /* RISCV_VECTOR_EMU_H */
