/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "smart2raw.h"
static double ms(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec*1e-6; }

int main(void){
  srand(7);
  const size_t T=2048, C=2048, N=T*C;   /* quantized activation: tokens x channels */
  uint64_t *cm=(uint64_t*)malloc(N*sizeof(uint64_t)); /* channel-major */
  uint64_t *rm=(uint64_t*)malloc(N*sizeof(uint64_t)); /* row-major     */
  /* ~1% OUTLIER CHANNELS (large values -> u16); the rest fits in u8 */
  int *isout=(int*)calloc(C,sizeof(int)); size_t nout=0;
  for(size_t c=0;c<C;c++){ if((rand()%100)==0){ isout[c]=1; nout++; } }
  for(size_t c=0;c<C;c++) for(size_t t=0;t<T;t++){
      uint64_t v = isout[c] ? (uint64_t)(2000+rand()%25000) : (uint64_t)(rand()%201);
      cm[c*T+t]=v;   /* contiguous channel */
      rm[t*C+c]=v;   /* contiguous token */
  }
  uint64_t mx=0; for(size_t i=0;i<N;i++) if(cm[i]>mx)mx=cm[i];
  size_t uniform = (size_t)(s2r_classify(mx)/8)*N;   /* safe single width (u16) */

  printf("=== (A) Activation with %.1f%% outlier channels (T=%zu, C=%zu) ===\n",100.0*nout/C,T,C);
  S2RBlocked bc; s2r_blocked_build(&bc,cm,N,T);   /* 1 block per channel (localized outlier) */
  S2RBlocked br; s2r_blocked_build(&br,rm,N,C);  /* 1 block per token (spread outlier) */
  /* correctness */
  uint64_t s=0; for(size_t i=0;i<N;i++) s+=cm[i];
  printf("  safe uniform (u16): %.1f MB\n", uniform/1e6);
  printf("  per-CHANNEL (localized outlier): %.1f MB -> %.2fx smaller  | exact naive sum: %s\n",
         s2r_blocked_bytes(&bc)/1e6, (double)uniform/s2r_blocked_bytes(&bc),
         s2r_blocked_sum(&bc)==s?"yes":"NO");
  printf("  per-TOKEN (spread outlier): %.1f MB -> %.2fx  (honest: layout matters!)\n",
         s2r_blocked_bytes(&br)/1e6, (double)uniform/s2r_blocked_bytes(&br));
  s2r_blocked_free(&bc); s2r_blocked_free(&br);

  printf("\n=== (B) KV-cache: %% outlier tokens, per-token blocks ===\n");
  size_t TOK=200000, D=128, M=TOK*D;
  uint64_t *kv=(uint64_t*)malloc(M*sizeof(uint64_t));
  size_t kout=0;
  for(size_t i=0;i<TOK;i++){ int o=(rand()%100)==0; if(o)kout++;
     for(size_t d=0;d<D;d++) kv[i*D+d]= o ? (uint64_t)(2000+rand()%25000) : (uint64_t)(rand()%201); }
  uint64_t kmx=0; for(size_t i=0;i<M;i++) if(kv[i]>kmx)kmx=kv[i];
  size_t kunif=(size_t)(s2r_classify(kmx)/8)*M;
  S2RBlocked bk; s2r_blocked_build(&bk,kv,M,D);  /* 1 block per token */
  printf("  %.1f%% outlier tokens | uniform(u16)=%.1f MB  per-token=%.1f MB -> %.2fx smaller\n",
         100.0*kout/TOK, kunif/1e6, s2r_blocked_bytes(&bk)/1e6, (double)kunif/s2r_blocked_bytes(&bk));
  s2r_blocked_free(&bk); free(kv);

  printf("\n=== (C) Block-accelerated zero-point correction (row-sum) ===\n");
  /* quantized int8 weight matrix [Mr x K], stored block-wise (rows) */
  size_t Mr=4096, K=4096, NN=Mr*K;
  uint64_t *w=(uint64_t*)malloc(NN*sizeof(uint64_t));
  for(size_t i=0;i<NN;i++) w[i]=(uint64_t)(rand()%256);  /* u8 */
  S2RBlocked bw; s2r_blocked_build(&bw,w,NN,K);          /* 1 block per row */
  uint64_t sref=0; for(size_t i=0;i<NN;i++) sref+=w[i];
  double bf=1e18,bs=1e18; volatile uint64_t sink=0;
  for(int r=0;r<10;r++){ double t=ms(); sink+=s2r_blocked_sum_fast(&bw); double d=ms()-t; if(d<bf)bf=d; }
  for(int r=0;r<10;r++){ double t=ms(); sink+=s2r_blocked_sum(&bw);      double d=ms()-t; if(d<bs)bs=d; }
  printf("  layer %zux%zu | sum_fast=%.2f ms (%.1f G/s)  scalar=%.2f ms (%.1f G/s)  %.2fx | exact: %s\n",
         Mr,K, bf, NN/1e6/bf, bs, NN/1e6/bs, bs/bf, (s2r_blocked_sum_fast(&bw)==sref)?"yes":"NO");
  s2r_blocked_free(&bw); free(w); (void)sink;
  free(cm); free(rm); free(isout);
  return 0;
}
