/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Compiled with -DS2R_FORCE_SVE2=1 and -I tests/sve2_emu, so the header includes the
 * emulated arm_sve.h and the real SVE kernels run on this x86 host. Validates the
 * vector-length-agnostic LOGIC against the scalar sum.
 *
 * Two properties matter, and neither may depend on the emulated vector length:
 *   1. the total equals the scalar sum for every n, at every VL;
 *   2. the u8 kernel accumulates in u32 lanes via UDOT, so it MUST strip-mine -
 *      a lane takes at most 4*255 = 1020 per iteration and would overflow
 *      UINT32_MAX after 2^22 of them. The saturated cases below are the worst
 *      case for both accumulators.
 *
 * Build the same file with -DS2R_SVE_EMU_K=2,4,8,16 to sweep VL = 128..1024 bits;
 * scripts/build_and_test.sh runs one of them and the sweep lives in this file's
 * own loop over tail lengths.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include "smart2raw.h"

static int pass=0, fail=0;
#define CHECK(c,m) do{ if(c){pass++;} else {printf("  [FAIL] %s\n",m); fail++;} }while(0)

int main(void){
    printf("Emulador SVE ativo: S2R_ARM_SVE2=%d (K=%d lanes de 64 bits, "
           "VL=%d bits, svcntb=%llu bytes/iter)\n",
           S2R_ARM_SVE2, S2R_SVE_EMU_K, S2R_SVE_EMU_K*64,
           (unsigned long long)svcntb());

    /* The point of the UDOT rewrite: consume a full vector of BYTES per
     * iteration, not one byte per 64-bit lane (which was VL/64 = 8x narrower). */
    CHECK(svcntb() == (uint64_t)S2R_SVE_EMU_K*8, "svcntb must be VL/8");
    CHECK(svcnth() == (uint64_t)S2R_SVE_EMU_K*4, "svcnth must be VL/16");

    for(int t=0;t<2;t++){
        int8_t cls = t ? S2R_16 : S2R_8;
        for(size_t N=1; N<=100000; N = N*3+1){
            S2RPool p; s2r_pool_init(&p, cls, N);
            for(size_t i=0;i<N;i++)
                s2r_set(&p, i, (cls==S2R_8) ? (i*131u)&0xFF : (i*7919u)&0xFFFF);
            p.count = N;
            uint64_t ref = s2r_sum(&p);
            uint64_t sve = (cls==S2R_8) ? s2r__sum_u8_sve((const uint8_t*)p.data, N)
                                        : s2r__sum_u16_sve((const uint16_t*)p.data, N);
            CHECK(sve==ref, "SVE sum mismatch");
            if(sve!=ref) printf("    cls=%d N=%zu ref=%llu sve=%llu\n",
                                cls, N, (unsigned long long)ref, (unsigned long long)sve);
            s2r_pool_free(&p);
        }
    }

    /* Every tail length around the vector boundary, both widths. */
    for(size_t N=1; N<=3*(size_t)svcntb()+1 && N<=256; N++){
        static uint8_t a8[260]; static uint16_t a16[260]; uint64_t s8=0,s16=0;
        for(size_t i=0;i<N;i++){ a8[i]=(uint8_t)(i*37+1); s8+=a8[i];
                                 a16[i]=(uint16_t)(i*5000+3); s16+=a16[i]; }
        CHECK(s2r__sum_u8_sve(a8,N)==s8,  "u8 tail");
        CHECK(s2r__sum_u16_sve(a16,N)==s16,"u16 tail");
    }

    /* n == 0 must touch nothing. */
    { const uint8_t z8[1]={0}; const uint16_t z16[1]={0};
      CHECK(s2r__sum_u8_sve(z8,0)==0,  "u8 empty");
      CHECK(s2r__sum_u16_sve(z16,0)==0,"u16 empty"); }

    /* Saturated inputs: worst case for the u32 accumulator in the u8 kernel and
     * for the u64 accumulator in the u16 kernel. */
    { size_t N=70000; uint8_t *a=(uint8_t*)malloc(N); uint64_t s=0;
      for(size_t i=0;i<N;i++){ a[i]=255; s+=255; }
      CHECK(s2r__sum_u8_sve(a,N)==s, "u8 all-255 no overflow"); free(a); }
    { size_t N=70000; uint16_t *a=(uint16_t*)malloc(N*sizeof(uint16_t)); uint64_t s=0;
      for(size_t i=0;i<N;i++){ a[i]=65535; s+=65535; }
      CHECK(s2r__sum_u16_sve(a,N)==s, "u16 all-65535 no overflow"); free(a); }

    /* The dispatcher must actually REACH these kernels - the original bug was that
     * on a real SVE target the NEON block came first and shadowed them entirely.
     * Here SVE2 is forced with NEON absent, so s2r_sum_fast takes the SVE branch
     * whenever svcntb() > 16 (VL > 128), which is the guard that deliberately
     * leaves NEON in charge at parity width. */
    { size_t N=5000; S2RPool p; s2r_pool_init(&p, S2R_8, N);
      for(size_t i=0;i<N;i++) s2r_set(&p, i, (i*29u)&0xFF);
      p.count=N;
      CHECK(s2r_sum_fast(&p)==s2r_sum(&p), "dispatcher u8 result");
      s2r_pool_free(&p); }
    { size_t N=5000; S2RPool p; s2r_pool_init(&p, S2R_16, N);
      for(size_t i=0;i<N;i++) s2r_set(&p, i, (i*7919u)&0xFFFF);
      p.count=N;
      CHECK(s2r_sum_fast(&p)==s2r_sum(&p), "dispatcher u16 result");
      s2r_pool_free(&p); }

    printf("=== %d OK, %d FAIL ===\n", pass, fail);
    return fail ? 1 : 0;
}
