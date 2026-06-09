/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
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
 * This is NOT a complete RVV implementation; it is a minimal, auditable shim. */
#ifndef RISCV_VECTOR_EMU_H
#define RISCV_VECTOR_EMU_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Emulated VLEN/64: number of 64-bit lanes in e64m1. With the fractional LMULs
 * used (e8mf8, e16mf4) the element count matches e64m1, so all three share K.
 * Overridable so the test can prove the logic for several vector lengths. */
#ifndef S2R_RVV_EMU_K
#define S2R_RVV_EMU_K 8
#endif

typedef struct { uint8_t  v[S2R_RVV_EMU_K]; } vuint8mf8_t;
typedef struct { uint16_t v[S2R_RVV_EMU_K]; } vuint16mf4_t;
typedef struct { uint64_t v[S2R_RVV_EMU_K]; } vuint64m1_t;

static inline size_t __riscv_vsetvlmax_e64m1(void){ return S2R_RVV_EMU_K; }
static inline size_t __riscv_vsetvl_e8mf8 (size_t avl){ return avl < S2R_RVV_EMU_K ? avl : S2R_RVV_EMU_K; }
static inline size_t __riscv_vsetvl_e16mf4(size_t avl){ return avl < S2R_RVV_EMU_K ? avl : S2R_RVV_EMU_K; }

static inline vuint8mf8_t  __riscv_vle8_v_u8mf8 (const uint8_t  *p, size_t vl){
    vuint8mf8_t r; memset(&r,0,sizeof r); for(size_t i=0;i<vl;i++) r.v[i]=p[i]; return r; }
static inline vuint16mf4_t __riscv_vle16_v_u16mf4(const uint16_t *p, size_t vl){
    vuint16mf4_t r; memset(&r,0,sizeof r); for(size_t i=0;i<vl;i++) r.v[i]=p[i]; return r; }

static inline vuint64m1_t __riscv_vmv_v_x_u64m1(uint64_t x, size_t vl){
    vuint64m1_t r; memset(&r,0,sizeof r); for(size_t i=0;i<vl;i++) r.v[i]=x; return r; }

/* zero-extend: u8 -> u64 (vf8) and u16 -> u64 (vf4) */
static inline vuint64m1_t __riscv_vzext_vf8_u64m1(vuint8mf8_t a, size_t vl){
    vuint64m1_t r; memset(&r,0,sizeof r); for(size_t i=0;i<vl;i++) r.v[i]=(uint64_t)a.v[i]; return r; }
static inline vuint64m1_t __riscv_vzext_vf4_u64m1(vuint16mf4_t a, size_t vl){
    vuint64m1_t r; memset(&r,0,sizeof r); for(size_t i=0;i<vl;i++) r.v[i]=(uint64_t)a.v[i]; return r; }

/* tail-undisturbed add: lanes [0,vl) = vs2+vs1 ; lanes [vl,K) keep vd */
static inline vuint64m1_t __riscv_vadd_vv_u64m1_tu(vuint64m1_t vd, vuint64m1_t vs2, vuint64m1_t vs1, size_t vl){
    vuint64m1_t r = vd; for(size_t i=0;i<vl;i++) r.v[i]=vs2.v[i]+vs1.v[i]; return r; }

/* reduction sum: result lane 0 = scalar[0] + sum(vec[0..vl)) */
static inline vuint64m1_t __riscv_vredsum_vs_u64m1_u64m1(vuint64m1_t vec, vuint64m1_t scal, size_t vl){
    uint64_t s = scal.v[0]; for(size_t i=0;i<vl;i++) s+=vec.v[i];
    vuint64m1_t r; memset(&r,0,sizeof r); r.v[0]=s; return r; }

static inline uint64_t __riscv_vmv_x_s_u64m1_u64(vuint64m1_t a){ return a.v[0]; }

#endif /* RISCV_VECTOR_EMU_H */
