/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Big-endian emulator: proves that, on a BE host, the mmap path
 * (read LE bytes from disk -> s2r_swap_payload -> NATIVE BE read) recovers
 * the original values. Uses the REAL swap function from the header. Deterministic on x86. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include "smart2raw.h"

static int pass=0,fail=0;
#define CHECK(c,m) do{ if(c){printf("  [OK]   %s\n",m);pass++;} else {printf("  [FAIL] %s\n",m);fail++;} }while(0)

/* NATIVE read on a big-endian CPU: byte[0] = most significant */
static uint64_t be_native_load(const uint8_t*p, size_t eb){
    uint64_t v=0; for(size_t i=0;i<eb;i++) v=(v<<8)|p[i]; return v;
}

static int emula_be(int8_t cls, size_t N){
    size_t eb=s2r_abs_size(cls)>>3;
    /* 1. producer writes a v3.3 file (canonical LE) */
    S2RPool a; s2r_pool_init(&a,cls,N);
    for(size_t i=0;i<N;i++){
        uint64_t v = (cls<0) ? (uint64_t)(int64_t)((int64_t)(i*2654435u)*(i&1?-1:1))
                             : ((uint64_t)i*2654435761u);
        s2r_set(&a,i, v & s2r_max_value(cls));
    }
    a.count=N;
    s2r_save_portable(&a,"be.s2r");

    /* 2. BE host mmaps: reads the PAYLOAD bytes from disk (= canonical LE in memory) */
    FILE*f=fopen("be.s2r","rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*buf=malloc((size_t)sz); if(fread(buf,1,(size_t)sz,f)!=(size_t)sz){fclose(f);return 0;} fclose(f);
    uint8_t*payload=buf+16; (void)0;

    /* 3. ramo BE do s2r_map_open: aplica o swap REAL do header */
    s2r_swap_payload(payload, eb, N);

    /* 4. the BE CPU reads each element NATIVELY and we compare with the original */
    int ok=1;
    for(size_t i=0;i<N;i++){
        uint64_t be = be_native_load(payload + i*eb, eb);
        uint64_t orig = s2r_get(&a,i);   /* original logical value */
        if(be!=orig){ ok=0; printf("    cls=%d i=%zu orig=%llu be=%llu\n",cls,i,(unsigned long long)orig,(unsigned long long)be); break; }
    }
    free(buf); s2r_pool_free(&a); remove("be.s2r");
    return ok;
}

int main(void){
    printf("Big-endian emulator (current host is LE; we simulate a BE read)\n\n");
    CHECK(emula_be(S2R_16, 1000), "u16: BE mmap recovers correct values");
    CHECK(emula_be(S2R_32, 1000), "u32: BE mmap recovers correct values");
    CHECK(emula_be(S2R_64, 1000), "u64: BE mmap recovers correct values");
    CHECK(emula_be(S2R_I16, 500), "i16: idem (bits preservados)");
    CHECK(emula_be(S2R_I32, 500), "i32: idem");
    /* u8 needs no swap (1 byte) - sanity */
    { S2RPool a; s2r_pool_init(&a,S2R_8,100); for(int i=0;i<100;i++)s2r_push(&a,(uint64_t)(i*7&0xFF));
      s2r_save_portable(&a,"b8.s2r");
      FILE*f=fopen("b8.s2r","rb"); fseek(f,16,SEEK_SET); uint8_t p[100]; size_t r=fread(p,1,100,f); fclose(f);
      int ok=(r==100); for(int i=0;ok&&i<100;i++) ok=(p[i]==s2r_get(&a,(size_t)i));
      CHECK(ok,"u8: no swap, identical on any host"); s2r_pool_free(&a); remove("b8.s2r"); }

    printf("\n=== %d OK, %d FAIL ===\n", pass, fail);
    return fail?1:0;
}
