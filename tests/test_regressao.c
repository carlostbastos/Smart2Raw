/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Integrated test Smart2Raw v3.3.0 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include "smart2raw.h"

static int pass=0,fail=0;
#define CHECK(c,m) do{ if(c){pass++;} else {printf("  [FAIL] %s\n",m);fail++;} }while(0)

int main(void){
    printf("Smart2Raw %s  | AVX2 runtime: %s | mmap: %d | stdio: %d\n\n",
        S2R_VERSION_STRING, s2r_has_avx2()?"yes":"no", S2R_HAS_MMAP, S2R_HAS_STDIO);

    /* 1. auto-adaptive push: grows the type by itself */
    printf("1. push_adaptive\n");
    {
        S2RPool p; s2r_pool_init(&p,S2R_8,4);
        s2r_push_adaptive(&p,200);        /* fits u8 */
        s2r_push_adaptive(&p,5000);       /* forca u16 */
        s2r_push_adaptive(&p,100000);     /* forca u32 */
        s2r_push_adaptive(&p,5000000000ULL); /* forca u64 */
        CHECK(s2r_abs_size(p.size)==64, "class grew to u64");
        CHECK(s2r_get(&p,0)==200 && s2r_get(&p,1)==5000 && s2r_get(&p,3)==5000000000ULL, "values preserved across promotion");
        s2r_pool_free(&p);

        S2RPool s; s2r_pool_init(&s,S2R_I8,4);
        s2r_push_signed_adaptive(&s,-100);
        s2r_push_signed_adaptive(&s,-30000);   /* forca I16 */
        s2r_push_signed_adaptive(&s,-2000000000);/* forca I32 */
        CHECK(p.size==S2R_64 || s.size==S2R_I32, "signed adaptive p/ I32");
        CHECK(s2r_get_signed(&s,0)==-100 && s2r_get_signed(&s,2)==-2000000000, "signed values preserved");
        s2r_pool_free(&s);
    }

    /* 2. SIMD sum == scalar sum (qualquer caminho de dispatch) */
    printf("2. s2r_sum_fast == s2r_sum\n");
    {
        for(int t=0;t<2;t++){
            int8_t cls=t?S2R_16:S2R_8;
            for(size_t N=0;N<=100003;N=N? N*7+1 : 1){
                S2RPool p; s2r_pool_init(&p,cls,N?N:1);
                for(size_t i=0;i<N;i++) s2r_set(&p,i,(cls==S2R_8)?(i*131u)&0xFF:(i*7919u)&0xFFFF);
                p.count=N;
                CHECK(s2r_sum(&p)==s2r_sum_fast(&p), "sum mismatch");
                s2r_pool_free(&p);
                if(N>100000) break;
            }
        }
    }

    /* 3. lazy carry deferido */
    printf("3. lazy carry (defer)\n");
    {
        S2RPool p; s2r_pool_init(&p,S2R_8,1); s2r_push(&p,200);
        S2RDeferred d; s2r_defer_begin(&d,&p);
        s2r_defer_add(&d,100); s2r_defer_mul(&d,300);
        CHECK(s2r_defer_commit(&d)==1, "commit ok");
        CHECK(p.size==S2R_32 && s2r_get(&p,0)==90000, "8->32 in one promotion, (200+100)*300=90000");
        s2r_pool_free(&p);
    }

    /* 4. portable I/O + CRC + zero-copy mmap */
    printf("4. portable FS + mmap\n");
    {
        S2RPool a; s2r_pool_init(&a,S2R_32,1000);
        for(int i=0;i<1000;i++) s2r_push(&a,(uint64_t)(i*2654435u));
        CHECK(s2r_save_portable(&a,"data.s2r")==S2R_OK, "save_portable ok");

        /* load por copia */
        S2RPool b; CHECK(s2r_load_portable(&b,"data.s2r")==S2R_OK, "load_portable ok");
        int eq=(b.count==1000); for(size_t i=0;eq&&i<1000;i++) eq=(s2r_get(&a,i)==s2r_get(&b,i));
        CHECK(eq, "load_portable round-trip");
        s2r_pool_free(&b);

#if S2R_HAS_MMAP
        /* mmap zero-copy read-only */
        S2RMap m; S2RError e=s2r_map_open(&m,"data.s2r",/*verify_crc=*/1);
        CHECK(e==S2R_OK, "map_open with CRC check");
        if(e==S2R_OK){
            CHECK(m.pool.count==1000, "mmap count");
            CHECK((m.pool.flags & S2R_FLAG_READONLY) && (m.pool.flags & S2R_FLAG_EXTERNAL), "mmap pool e READONLY+EXTERNAL");
            int meq=1; for(size_t i=0;i<1000;i++) if(s2r_get(&m.pool,i)!=s2r_get(&a,i)){ meq=0; break; }
            CHECK(meq, "mmap reads the same values (zero-copy)");
            /* opera direto no mapeamento */
            CHECK(s2r_sum_fast(&m.pool)==s2r_sum(&a), "sum_fast direto no mmap == original");
            /* writes must be rejected (read-only) */
            CHECK(s2r_push(&m.pool, 1)==0, "push no mmap read-only e rejeitado");
            s2r_map_close(&m);
        }
#endif
        s2r_pool_free(&a);
        remove("data.s2r");
    }

    /* 5. mmap detects a corrupted file */
#if S2R_HAS_MMAP
    printf("5. mmap + CRC detecta corrupcao\n");
    {
        S2RPool a; s2r_pool_init(&a,S2R_16,100);
        for(int i=0;i<100;i++) s2r_push(&a,(uint64_t)(i*123));
        s2r_save_portable(&a,"c.s2r"); s2r_pool_free(&a);
        FILE*f=fopen("c.s2r","r+b"); fseek(f,16+20,SEEK_SET); int ch=fgetc(f); fseek(f,16+20,SEEK_SET); fputc(ch^0xFF,f); fclose(f);
        S2RMap m; CHECK(s2r_map_open(&m,"c.s2r",1)==S2R_ERR_CORRUPT, "map_open rejeita CRC invalido");
        remove("c.s2r");
    }
#endif

    printf("6. promotion with an empty pool does not overflow (regression count==0)\n");
    {
        /* small init; the first value already needs a larger class; fill up to the initial capacity */
        S2RPool p; s2r_pool_init(&p,S2R_I8,1000);
        for(int i=0;i<1000;i++) s2r_push_signed_adaptive(&p,-5000000000LL+i);
        CHECK(p.count==1000, "signed: 1000 elements after empty-pool promotion");
        CHECK(s2r_abs_size(p.size)==64, "signed: class grew to 64 bits");
        CHECK(s2r_get_signed(&p,999)==-5000000000LL+999, "signed: round-trip do ultimo");
        CHECK(s2r_used_bytes(&p)==8000, "signed: consistent bytes (1000*8)");
        s2r_pool_free(&p);
        S2RPool q; s2r_pool_init(&q,S2R_8,1000);
        for(int i=0;i<1000;i++) s2r_push_adaptive(&q,(uint64_t)(40000+i));
        CHECK(q.count==1000 && s2r_get(&q,999)==(uint64_t)(40000+999), "unsigned: empty-pool promotion ok");
        s2r_pool_free(&q);
    }

    printf("\n=== %d OK, %d FAIL ===\n", pass, fail);
    return fail?1:0;
}
