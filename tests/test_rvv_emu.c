/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Compiled with -DS2R_FORCE_RVV=1 and -I tests/rvv_emu, so the header includes
 * the emulated riscv_vector.h and the real RVV kernels run on this x86 host.
 * Validates the vector-length-agnostic LOGIC against the scalar sum. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include "smart2raw.h"

static int pass=0, fail=0;
#define CHECK(c,m) do{ if(c){pass++;} else {printf("  [FAIL] %s\n",m); fail++;} }while(0)

int main(void){
    printf("Emulador RVV ativo: S2R_RISCV_RVV=%d (K=%d lanes)\n", S2R_RISCV_RVV, S2R_RVV_EMU_K);

    /* sweep sizes (incl. strip boundaries and tails) for u8 and u16 */
    for(int t=0;t<2;t++){
        int8_t cls = t ? S2R_16 : S2R_8;
        for(size_t N=1; N<=100000; N = N*3+1){
            S2RPool p; s2r_pool_init(&p, cls, N);
            for(size_t i=0;i<N;i++)
                s2r_set(&p, i, (cls==S2R_8) ? (i*131u)&0xFF : (i*7919u)&0xFFFF);
            p.count = N;
            uint64_t ref = s2r_sum(&p);
            uint64_t rvv = (cls==S2R_8) ? s2r__sum_u8_rvv((const uint8_t*)p.data, N)
                                        : s2r__sum_u16_rvv((const uint16_t*)p.data, N);
            CHECK(rvv==ref, "RVV sum mismatch");
            if(rvv!=ref) printf("    cls=%d N=%zu ref=%llu rvv=%llu\n",
                                cls, N, (unsigned long long)ref, (unsigned long long)rvv);
            s2r_pool_free(&p);
        }
    }

    /* explicit tails around the emulated lane count K (bounded to the buffers) */
    for(size_t N=1; N<=3*S2R_RVV_EMU_K+1 && N<=60; N++){
        uint8_t a8[64]; uint16_t a16[64]; uint64_t s8=0,s16=0;
        for(size_t i=0;i<N;i++){ a8[i]=(uint8_t)(i*37+1); s8+=a8[i];
                                 a16[i]=(uint16_t)(i*5000+3); s16+=a16[i]; }
        CHECK(s2r__sum_u8_rvv(a8,N)==s8,  "u8 tail");
        CHECK(s2r__sum_u16_rvv(a16,N)==s16,"u16 tail");
    }

    /* max-byte stress: many 255s must not overflow the u64 accumulator */
    { size_t N=70000; uint8_t *a=(uint8_t*)malloc(N); uint64_t s=0;
      for(size_t i=0;i<N;i++){ a[i]=255; s+=255; }
      CHECK(s2r__sum_u8_rvv(a,N)==s, "u8 all-255 no overflow"); free(a); }

    printf("=== %d OK, %d FAIL ===\n", pass, fail);
    return fail ? 1 : 0;
}
