/*
 * Smart2Raw Analytics v2 tests
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "smart2raw.h"
#include <stdio.h>
#include <stdint.h>

#define CHECK(x) do { if(!(x)){ printf("FAIL:%d: %s\n", __LINE__, #x); fails++; } else ok++; } while(0)

int main(void){
    int ok=0, fails=0;

    S2RPool u; CHECK(s2r_pool_init(&u, S2R_8, 0));
    uint64_t vals[]={5,1,3,5,2,1,255,0};
    for(size_t i=0;i<sizeof(vals)/sizeof(vals[0]);i++) CHECK(s2r_push_adaptive(&u, vals[i])==S2R_OK);
    CHECK(!s2r_is_sorted(&u));
    CHECK(s2r_sort(&u)==S2R_OK);
    CHECK(s2r_is_sorted(&u));
    uint64_t want_sorted[]={0,1,1,2,3,5,5,255};
    for(size_t i=0;i<u.count;i++) CHECK(s2r_get(&u,i)==want_sorted[i]);
    size_t nuniq=0; CHECK(s2r_nunique(&u,&nuniq)==S2R_OK); CHECK(nuniq==6);
    CHECK(s2r_unique_sorted(&u)==6); CHECK(u.count==6);
    uint64_t want_unique[]={0,1,2,3,5,255};
    for(size_t i=0;i<u.count;i++) CHECK(s2r_get(&u,i)==want_unique[i]);
    uint64_t counts[256]; CHECK(s2r_value_counts_u8(&u,counts)==S2R_OK);
    CHECK(counts[0]==1 && counts[1]==1 && counts[5]==1 && counts[255]==1);
    s2r_pool_free(&u);

    S2RPool s; CHECK(s2r_pool_init(&s, S2R_I8, 0));
    int64_t sv[]={-1,-128,10,-1,0,10};
    for(size_t i=0;i<sizeof(sv)/sizeof(sv[0]);i++) CHECK(s2r_push_signed_adaptive(&s, sv[i])==S2R_OK);
    CHECK(s2r_sort(&s)==S2R_OK); CHECK(s2r_is_sorted(&s));
    int64_t swant[]={-128,-1,-1,0,10,10};
    for(size_t i=0;i<s.count;i++) CHECK(s2r_get_signed(&s,i)==swant[i]);
    CHECK(s2r_nunique(&s,&nuniq)==S2R_OK); CHECK(nuniq==4);
    CHECK(s2r_unique_sorted(&s)==4);
    int64_t sunique[]={-128,-1,0,10};
    for(size_t i=0;i<s.count;i++) CHECK(s2r_get_signed(&s,i)==sunique[i]);
    CHECK(s2r_value_counts_u8(&s,counts)==S2R_OK);
    CHECK(counts[(uint8_t)-128]==1 && counts[(uint8_t)-1]==1 && counts[0]==1 && counts[10]==1);
    s2r_pool_free(&s);

    S2RPool big; CHECK(s2r_pool_init(&big, S2R_32, 0));
    uint64_t bv[]={70000,1,70000,2,3,3,4};
    for(size_t i=0;i<sizeof(bv)/sizeof(bv[0]);i++) CHECK(s2r_push_adaptive(&big,bv[i])==S2R_OK);
    CHECK(s2r_nunique(&big,&nuniq)==S2R_OK); CHECK(nuniq==5);
    CHECK(s2r_sort(&big)==S2R_OK); CHECK(s2r_unique_sorted(&big)==5);
    CHECK(s2r_get(&big,0)==1 && s2r_get(&big,4)==70000);
    s2r_pool_free(&big);

    printf("%d OK, %d FAIL\n", ok, fails);
    return fails?1:0;
}
