/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Gap 3 (the LE-testable part): the BE path uses MAP_PRIVATE + mprotect +
 * an in-place swap. Here we prove this does NOT change the on-disk file (COW),
 * which is the safety guarantee a big-endian host relies on.
 * The correctness of the VALUES on BE depends on s2r_swap_payload (tested in both
 * directions) + CRC-before-swap; this does not run on LE hardware. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "smart2raw.h"

static int pass=0,fail=0;
#define CHECK(c,m) do{ if(c){printf("  [OK]   %s\n",m);pass++;} else {printf("  [FAIL] %s\n",m);fail++;} }while(0)

int main(void){
    const char*path="cow.s2r";
    /* create a v3.3 file */
    S2RPool a; s2r_pool_init(&a,S2R_32,500);
    for(int i=0;i<500;i++) s2r_push(&a,(uint64_t)(i*0x01020304u+7));
    s2r_save_portable(&a,path);

    /* read the file's original bytes */
    FILE*f=fopen(path,"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char*orig=malloc((size_t)sz); fread(orig,1,(size_t)sz,f); fclose(f);

    /* replica EXATAMENTE o que o ramo BE faz: MAP_PRIVATE + mprotect + swap in-place */
    int fd=open(path,O_RDONLY);
    void*base=mmap(NULL,(size_t)sz,PROT_READ,MAP_PRIVATE,fd,0);
    CHECK(base!=MAP_FAILED,"mmap MAP_PRIVATE");
    unsigned char*payload=(unsigned char*)base+16;       /* offset do payload v3.3 */
    size_t bytes=(size_t)sz-16-4;
    CHECK(mprotect(base,(size_t)sz,PROT_READ|PROT_WRITE)==0,"mprotect +WRITE em pagina COW");
    s2r_swap_payload(payload,4,bytes/4);                 /* dirties pages (COW), not the file */
    /* check that after the swap the bytes in MEMORY changed */
    CHECK(memcmp(payload,orig+16,bytes)!=0,"swap changed the in-memory copy (COW)");
    munmap(base,(size_t)sz); close(fd);

    /* RE-READ the on-disk file: it must be INTACT */
    f=fopen(path,"rb"); unsigned char*after=malloc((size_t)sz); fread(after,1,(size_t)sz,f); fclose(f);
    CHECK(memcmp(orig,after,(size_t)sz)==0,"on-disk file UNCHANGED after COW+swap");

    /* sanity: double swap restores (the logic BE uses to get native values) */
    s2r_swap_payload(orig+16,4,bytes/4); s2r_swap_payload(orig+16,4,bytes/4);
    unsigned char*ref=malloc((size_t)sz); f=fopen(path,"rb"); fread(ref,1,(size_t)sz,f); fclose(f);
    CHECK(memcmp(orig,ref,(size_t)sz)==0,"double swap is involutive (LE<->BE round-trip)");

    /* regression: the LE path (zero-copy) is still correct */
    S2RMap m;
    CHECK(s2r_map_open(&m,path,1)==S2R_OK,"map_open LE zero-copy ok");
    int eq=(m.pool.count==500); for(size_t i=0;eq&&i<500;i++) eq=(s2r_get(&m.pool,i)==s2r_get(&a,i));
    CHECK(eq,"LE values check out (zero-copy)");
    s2r_map_close(&m);

    free(orig); free(after); free(ref); s2r_pool_free(&a); remove(path);
    printf("\n=== %d OK, %d FAIL ===\n",pass,fail);
    return fail?1:0;
}
