/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include "smart2raw.h"   /* compiled with __ARM_NEON + emulated arm_neon.h */

static int pass=0,fail=0;
#define CHECK(c,m) do{ if(c){pass++;} else {printf("  [FAIL] %s\n",m);fail++;} }while(0)

int main(void){
    printf("Emulador NEON ativo: S2R_ARM_NEON=%d  S2R_X86_SIMD=%d\n", S2R_ARM_NEON, S2R_X86_SIMD);
    /* calls the header's real NEON functions DIRECTLY (not sum_fast) */
    for(int t=0;t<2;t++){
        int8_t cls=t?S2R_16:S2R_8;
        for(size_t N=0;N<=100000;N=N?N*3+1:1){
            S2RPool p; s2r_pool_init(&p,cls,N?N:1);
            for(size_t i=0;i<N;i++) s2r_set(&p,i,(cls==S2R_8)?(i*131u)&0xFF:(i*7919u)&0xFFFF);
            p.count=N;
            uint64_t ref=s2r_sum(&p);
            uint64_t neon = (cls==S2R_8)? s2r__sum_u8_neon((const uint8_t*)p.data,N)
                                        : s2r__sum_u16_neon((const uint16_t*)p.data,N);
            CHECK(neon==ref, "NEON sum mismatch");
            if(neon!=ref) printf("    cls=%d N=%zu ref=%llu neon=%llu\n",cls,N,(unsigned long long)ref,(unsigned long long)neon);
            s2r_pool_free(&p);
            if(N>100000) break;
        }
    }
    /* explicit tail cases (N not a multiple of 16/8) */
    { uint8_t a[19]; uint64_t s=0; for(int i=0;i<19;i++){a[i]=(uint8_t)(i*37+1); s+=a[i];}
      CHECK(s2r__sum_u8_neon(a,19)==(uint64_t)s, "u8 cauda N=19"); }
    { uint16_t a[13]; uint64_t s=0; for(int i=0;i<13;i++){a[i]=(uint16_t)(i*5000+3); s+=a[i];}
      CHECK(s2r__sum_u16_neon(a,13)==(uint64_t)s, "u16 cauda N=13"); }

    printf("=== %d OK, %d FAIL ===\n", pass, fail);
    return fail?1:0;
}
