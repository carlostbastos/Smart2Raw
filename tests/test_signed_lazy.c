/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* Gap 1 test: signed lazy-carry (runnable on x86) */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include "smart2raw.h"

static int pass=0,fail=0;
#define CHECK(c,m) do{ if(c){printf("  [OK]   %s\n",m);pass++;} else {printf("  [FAIL] %s\n",m);fail++;} }while(0)

int main(void){
    printf("Gap 1 - SIGNED lazy-carry arithmetic\n\n");

    /* (a) add that pushes the MINIMUM beyond the class (a signed-only case) */
    printf("(a) add_scalar_signed_safe\n");
    {
        S2RPool p; s2r_pool_init(&p,S2R_I8,3);
        s2r_push(&p,(uint64_t)(int64_t)-100);
        s2r_push(&p,(uint64_t)(int64_t)0);
        s2r_push(&p,(uint64_t)(int64_t)50);
        /* -100 - 50 = -150 < -128 -> needs I16 */
        int cls=s2r_add_scalar_signed_safe(&p,-50);
        CHECK(cls==16, "promoted I8->I16 (minimum overflowed)");
        CHECK(s2r_get_signed(&p,0)==-150 && s2r_get_signed(&p,2)==0, "values: -150 ... 0 (no wrap)");
        s2r_pool_free(&p);

        /* contrast: the native add would wrap in I8 */
        S2RPool q; s2r_pool_init(&q,S2R_I8,1); s2r_push(&q,(uint64_t)(int64_t)-100);
        s2r_add_scalar(&q,(uint64_t)(int64_t)-50);  /* -150 does not fit in I8 */
        CHECK(s2r_get_signed(&q,0)!=-150, "native wraps (proof of the bug the safe path avoids)");
        printf("    (native -100 + -50 in I8 = %lld, should be -150)\n",(long long)s2r_get_signed(&q,0));
        s2r_pool_free(&q);
    }

    /* (b) mul by NEGATIVE scalar: inverts min/max */
    printf("\n(b) mul_scalar_signed_safe (negative scalar)\n");
    {
        S2RPool p; s2r_pool_init(&p,S2R_I8,3);
        s2r_push(&p,(uint64_t)(int64_t)-100);  /* *(-3) = 300  -> I16 */
        s2r_push(&p,(uint64_t)(int64_t)10);    /* *(-3) = -30        */
        s2r_push(&p,(uint64_t)(int64_t)40);    /* *(-3) = -120       */
        int cls=s2r_mul_scalar_signed_safe(&p,-3);
        CHECK(cls==16, "promoveu I8->I16 (produto excede I8)");
        CHECK(s2r_get_signed(&p,0)==300 && s2r_get_signed(&p,1)==-30 && s2r_get_signed(&p,2)==-120,
              "values: 300, -30, -120 (min/max inverted correctly)");
        s2r_pool_free(&p);
    }

    /* (c) signed deferred session: whole chain, 1 promotion */
    printf("\n(c) defer signed (cadeia)\n");
    {
        S2RPool p; s2r_pool_init(&p,S2R_I8,2);
        s2r_push(&p,(uint64_t)(int64_t)-50);
        s2r_push(&p,(uint64_t)(int64_t)100);
        S2RDeferredSigned d; s2r_defer_signed_begin(&d,&p);
        s2r_defer_signed_add(&d,-200);   /* range -250..-100 -> I16 */
        s2r_defer_signed_mul(&d,-500);    /* range 50000..125000 -> I32 */
        int8_t before=p.size;
        int ok=s2r_defer_signed_commit(&d);
        /* (-50-200)*-500 = 125000 ; (100-200)*-500 = 50000 */
        CHECK(ok && before==S2R_I8 && p.size==S2R_I32, "I8 -> I32 in a single promotion");
        CHECK(s2r_get_signed(&p,0)==125000 && s2r_get_signed(&p,1)==50000, "values: 125000, 50000");
        printf("    vmin previsto=%lld vmax=%lld\n",(long long)d.vmin,(long long)d.vmax);
        s2r_pool_free(&p);
    }

    /* (d) range que cruza zero amplo -> I64 */
    printf("\n(d) escala para I64\n");
    {
        S2RPool p; s2r_pool_init(&p,S2R_I16,2);
        s2r_push(&p,(uint64_t)(int64_t)-30000);
        s2r_push(&p,(uint64_t)(int64_t)30000);
        int cls=s2r_mul_scalar_signed_safe(&p,1000000);  /* +-3e10 -> I64 */
        CHECK(cls==64, "I16 -> I64");
        CHECK(s2r_get_signed(&p,0)==-30000000000LL && s2r_get_signed(&p,1)==30000000000LL, "values +-3e10");
        s2r_pool_free(&p);
    }

    /* (e) honest limit: int64 overflow must be refused */
    printf("\n(e) limit: int64 overflow\n");
    {
        S2RPool p; s2r_pool_init(&p,S2R_I64,1); s2r_push(&p,(uint64_t)(INT64_MAX-5));
        int cls=s2r_add_scalar_signed_safe(&p,100);  /* overflows int64 */
        CHECK(cls==0, "add refuses (returns 0) instead of corrupting");
        CHECK(s2r_get_signed(&p,0)==INT64_MAX-5, "value intact after refusal");
        s2r_pool_free(&p);
    }

    printf("\n=== %d OK, %d FAIL ===\n", pass, fail);
    return fail?1:0;
}
