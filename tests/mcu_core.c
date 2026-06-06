/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "smart2raw.h"
/* no I/O libc: exercises the adaptive core directly */
int main(void){
  S2RPool p; if(!s2r_pool_init(&p,S2R_8,8)) return 1;
  uint64_t vals[]={25,30,40,1000,70000,5000000000ULL};   /* forces u8->u64 */
  for(unsigned i=0;i<sizeof(vals)/sizeof(vals[0]);i++) s2r_push_adaptive(&p,vals[i]);
  uint64_t s=s2r_sum(&p); int8_t cls=p.size;
  s2r_pool_free(&p);
  /* a non-zero return signals an error; the expected sum 5000071095 mod 256 is not useful, so I check via cls and count */
  return !(cls==64 && s==5000071095ULL);
}
