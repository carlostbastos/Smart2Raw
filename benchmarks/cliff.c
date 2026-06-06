/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
static double now(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
#define REP 7
volatile uint64_t SINK;
int main(){
  size_t Ns[]={4096,16384,65536,262144,1048576,4194304,16777216,67108864};
  int nN=sizeof(Ns)/sizeof(Ns[0]);
  size_t maxN=Ns[nN-1];
  uint8_t *b8; uint64_t *b64;
  posix_memalign((void**)&b8,64,maxN);
  posix_memalign((void**)&b64,64,maxN*8);
  for(size_t i=0;i<maxN;i++){ uint8_t v=(uint8_t)(i*1103515245u>>16); b8[i]=v; b64[i]=v; }
  printf("%10s %9s %9s | %10s %10s %8s | %10s %10s %8s\n",
    "N","WS_u8","WS_u64","u8 GE/s","u64 GE/s","spd(sum)","u8max","u64max","spd(max)");
  for(int k=0;k<nN;k++){
    size_t N=Ns[k];
    double best8=1e9,best64=1e9,bm8=1e9,bm64=1e9;
    for(int r=0;r<REP;r++){
      // sum u8
      double t=now(); uint64_t s=0; for(size_t i=0;i<N;i++) s+=b8[i]; SINK=s; double d=now()-t; if(d<best8)best8=d;
      // sum u64
      t=now(); uint64_t s2=0; for(size_t i=0;i<N;i++) s2+=b64[i]; SINK=s2; d=now()-t; if(d<best64)best64=d;
      // max u8
      t=now(); uint8_t m=0; for(size_t i=0;i<N;i++){ if(b8[i]>m)m=b8[i]; } SINK=m; d=now()-t; if(d<bm8)bm8=d;
      // max u64
      t=now(); uint64_t m2=0; for(size_t i=0;i<N;i++){ if(b64[i]>m2)m2=b64[i]; } SINK=m2; d=now()-t; if(d<bm64)bm64=d;
    }
    double ge8=N/best8/1e9, ge64=N/best64/1e9, gm8=N/bm8/1e9, gm64=N/bm64/1e9;
    char w8[16],w64[16];
    snprintf(w8,16,"%zuKB",(N)/1024); snprintf(w64,16,"%zuKB",(N*8)/1024);
    printf("%10zu %9s %9s | %10.2f %10.2f %8.2f | %10.2f %10.2f %8.2f\n",
      N,w8,w64,ge8,ge64,ge8/ge64,gm8,gm64,gm8/gm64);
  }
  // cache info
  printf("\n--- cache ---\n"); 
  return 0;
}
