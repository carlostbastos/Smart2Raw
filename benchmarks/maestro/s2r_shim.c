/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdint.h>
#include <stddef.h>
#include "smart2raw.h"   /* define S2R_X86_SIMD; se 1, traz os kernels s2r__sum_* */

/* No Windows todo simbolo precisa ser exportado explicitamente (MSVC, mingw e
   clang). Em ELF/Mach-O a visibilidade padrao ja exporta. */
#if defined(_WIN32)
  #define S2RB_EXPORT __declspec(dllexport)
#else
  #define S2RB_EXPORT
#endif

/* soma n elementos de `width` bytes -> uint64. width==1/2 com use_lib usa o
   kernel SIMD real do Smart2Raw (so existe quando S2R_X86_SIMD, i.e. gcc/clang
   em x86); senao usa laco nativo, que o compilador auto-vetoriza. */
S2RB_EXPORT uint64_t s2rb_sum(const void *p, size_t n, int width, int use_lib){
    (void)use_lib;
    if(width==1){ const uint8_t *a=(const uint8_t*)p;
#if S2R_X86_SIMD
        if(use_lib){ if(s2r_has_avx512bw()) return s2r__sum_u8_avx512(a,n);
                     if(s2r_has_avx2())     return s2r__sum_u8_avx2(a,n); }
#endif
        uint64_t s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
    if(width==2){ const uint16_t *a=(const uint16_t*)p;
#if S2R_X86_SIMD
        if(use_lib && s2r_has_avx2()) return s2r__sum_u16_avx2(a,n);
#endif
        uint64_t s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
    if(width==4){ const uint32_t *a=(const uint32_t*)p;
        uint64_t s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
    const int64_t *a=(const int64_t*)p;
    uint64_t s=0; for(size_t i=0;i<n;i++) s+=(uint64_t)a[i]; return s;
}
/* conta elementos > k (filtro de faixa), width-aware. */
S2RB_EXPORT uint64_t s2rb_count_gt(const void *p, size_t n, int width, int64_t k){
    uint64_t c=0;
    if(width==1){ const uint8_t  *a=(const uint8_t*)p;  for(size_t i=0;i<n;i++) c+=((int64_t)a[i]>k); }
    else if(width==2){ const uint16_t *a=(const uint16_t*)p; for(size_t i=0;i<n;i++) c+=((int64_t)a[i]>k); }
    else if(width==4){ const uint32_t *a=(const uint32_t*)p; for(size_t i=0;i<n;i++) c+=((int64_t)a[i]>k); }
    else { const int64_t *a=(const int64_t*)p; for(size_t i=0;i<n;i++) c+=(a[i]>k); }
    return c;
}
