/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* EMULATED arm_neon.h - implements in portable C ONLY the intrinsics used by
 * Smart2Raw, with semantics faithful to the ACLE/ARM specification. Lanes are modeled
 * as logical arrays indexed 0..N-1 (lane 0 = lowest address in vld1q),
 * so they do not depend on the x86 host's endianness. It lets us compile/RUN the
 * header's real NEON functions on this machine and validate lanes, widths and tail.
 *
 * This is NOT the SIMDe project; it is a minimal, auditable shim for testing. */
#ifndef ARM_NEON_EMU_H
#define ARM_NEON_EMU_H
#include <stdint.h>
#include <string.h>

typedef struct { uint8_t  v[16]; } uint8x16_t;
typedef struct { uint16_t v[8];  } uint16x8_t;
typedef struct { uint32_t v[4];  } uint32x4_t;
typedef struct { uint64_t v[2];  } uint64x2_t;

static inline uint8x16_t vld1q_u8 (const uint8_t  *p){ uint8x16_t r; memcpy(r.v,p,16); return r; }
static inline uint16x8_t vld1q_u16(const uint16_t *p){ uint16x8_t r; memcpy(r.v,p,16); return r; }

/* Unsigned Add Long Pairwise: out[i] = a[2i] + a[2i+1], width doubles. */
static inline uint16x8_t vpaddlq_u8 (uint8x16_t a){ uint16x8_t r; for(int i=0;i<8;i++) r.v[i]=(uint16_t)a.v[2*i]+a.v[2*i+1]; return r; }
static inline uint32x4_t vpaddlq_u16(uint16x8_t a){ uint32x4_t r; for(int i=0;i<4;i++) r.v[i]=(uint32_t)a.v[2*i]+a.v[2*i+1]; return r; }
static inline uint64x2_t vpaddlq_u32(uint32x4_t a){ uint64x2_t r; for(int i=0;i<2;i++) r.v[i]=(uint64_t)a.v[2*i]+a.v[2*i+1]; return r; }

static inline uint64x2_t vaddq_u64(uint64x2_t a, uint64x2_t b){ uint64x2_t r; r.v[0]=a.v[0]+b.v[0]; r.v[1]=a.v[1]+b.v[1]; return r; }
static inline uint64x2_t vdupq_n_u64(uint64_t x){ uint64x2_t r; r.v[0]=r.v[1]=x; return r; }
static inline uint64_t   vgetq_lane_u64(uint64x2_t a, int lane){ return a.v[lane]; }

#endif
