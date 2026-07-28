/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* test_summary.c - o pool plano ganha o mesmo tratamento de metadados que a
 * camada em blocos tem desde a 3.4.0, o tamanho de bloco passa a ser
 * classificado a partir do dado, e a propriedade "nunca expande" vira uma
 * asserção em vez de uma frase.
 *
 * Três riscos, e o teste existe por causa deles:
 *
 *   1. RESUMO OBSOLETO. min/max/soma guardados num pool mutável têm o mesmo
 *      perigo que a flag de ordem: se sobreviverem a uma escrita, o predicado
 *      responde errado em silêncio. A seção 1 chama toda função pública que
 *      escreve e exige a invalidação - por execução, não por inspeção.
 *
 *   2. ÍNDICE OBSOLETO. O índice acumulado vive fora do pool, então nem a
 *      disciplina de flags o protege. Ele carrega a época em que foi construído
 *      e tem de RECUSAR responder depois de qualquer escrita. A seção 2 escreve
 *      no pool e exige a recusa.
 *
 *   3. MODELO DE CUSTO QUE MENTE. O planejador escolhe o tamanho de bloco a
 *      partir de uma previsão; se a previsão divergir do que o build produz, a
 *      escolha é arbitrária. A seção 3 compara previsão com o tamanho real em
 *      toda combinação de forma e candidato.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smart2raw.h"

static long pass=0, fail=0;
#define CHECK(c, ...) do{ if(c){pass++;} else { if(fail<25){printf("  [FAIL] "); printf(__VA_ARGS__); printf("\n");} fail++; } }while(0)

static uint64_t rs=0x14057B7EF767814Full;
static uint64_t rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return rs; }
static uint64_t bump(uint64_t v, void *c){ (void)c; return v+1; }

/* ================= 1. o resumo é mantido, nunca presumido ================= */
static void fill_sum(S2RPool *p, size_t n){
    s2r_pool_init(p,S2R_16,n);
    for(size_t i=0;i<n;i++) s2r_set(p,i,(uint64_t)(rnd()%1000));
    p->count=n;
    s2r_summarize(p);
}

static void test_summary_lifecycle(void)
{
    printf("-- 1. o resumo cai em TODA escrita --\n");
    const size_t N=800;
#define INVALIDATES(label, ...) do{ \
    S2RPool p; fill_sum(&p,N); \
    CHECK(s2r_has_summary(&p), "%s: resumo posto antes", label); \
    uint32_t e0=p.epoch; \
    { __VA_ARGS__; } \
    CHECK(!s2r_has_summary(&p), "%s: derruba o resumo", label); \
    CHECK(p.epoch!=e0, "%s: avança a época", label); \
    s2r_pool_free(&p); }while(0)

    INVALIDATES("s2r_set",             s2r_set(&p,0,1));
    INVALIDATES("s2r_set_signed",      s2r_set_signed(&p,0,1));
    INVALIDATES("s2r_add_scalar",      s2r_add_scalar(&p,3));
    INVALIDATES("s2r_mul_scalar",      s2r_mul_scalar(&p,2));
    INVALIDATES("s2r_add_scalar_safe", (void)s2r_add_scalar_safe(&p,3));
    INVALIDATES("s2r_mul_scalar_safe", (void)s2r_mul_scalar_safe(&p,2));
    INVALIDATES("s2r_transform",       s2r_transform(&p,bump,NULL));
    INVALIDATES("s2r_remove_swap",     s2r_remove_swap(&p,0));
    INVALIDATES("s2r_clear",           s2r_clear(&p));
#undef INVALIDATES

    /* um APPEND mantém: é a única escrita que uma regra incremental acompanha */
    {   S2RPool p; s2r_pool_init(&p,S2R_32,16);
        s2r_summarize_empty(&p);
        uint64_t mn=~0ull, mx=0, sm=0; int kept=1;
        for(uint64_t i=0;i<20000;i++){
            uint64_t v=rnd()%1000000;
            s2r_push(&p,v);
            if(v<mn) mn=v;
            if(v>mx) mx=v;
            sm+=v;
            if(!s2r_has_summary(&p)){ kept=0; break; }
        }
        CHECK(kept, "20000 pushes mantêm o resumo");
        CHECK(p.smin==mn && p.smax==mx && p.ssum==sm, "resumo incremental == referência");
        CHECK(s2r_sum_fast(&p)==sm, "e bate com a soma varrida");
        s2r_pool_free(&p); }

    /* o resumo calculado bate com a referência ingênua, com e sem sinal */
    {   S2RPool p; s2r_pool_init(&p,S2R_32,3000);
        uint64_t mn=~0ull, mx=0, sm=0;
        for(size_t i=0;i<3000;i++){ uint64_t v=rnd()%100000; s2r_set(&p,i,v);
            if(v<mn) mn=v;
            if(v>mx) mx=v;
            sm+=v; }
        p.count=3000; s2r_summarize(&p);
        CHECK(p.smin==mn && p.smax==mx && p.ssum==sm, "summarize sem sinal");
        s2r_pool_free(&p); }
    {   S2RPool p; s2r_pool_init(&p,S2R_I32,3000);
        int64_t mn=INT64_MAX, mx=INT64_MIN; uint64_t sm=0;
        for(size_t i=0;i<3000;i++){ int64_t v=(int64_t)(rnd()%200000)-100000;
            s2r_set_signed(&p,i,v);
            if(v<mn) mn=v;
            if(v>mx) mx=v;
            sm+=(uint64_t)v; }
        p.count=3000; s2r_summarize(&p);
        CHECK((int64_t)p.smin==mn && (int64_t)p.smax==mx && p.ssum==sm, "summarize com sinal");
        s2r_pool_free(&p); }
}

/* o predicado com resumo tem de dar exatamente o mesmo que sem */
static void sweep_summary(const char *nm, size_t n, uint64_t mod, int signed_pool)
{
    S2RPool a,b;
    if(signed_pool){ s2r_pool_init(&a,S2R_I32,n); s2r_pool_init(&b,S2R_I32,n); }
    else           { s2r_pool_init(&a,S2R_32,n);  s2r_pool_init(&b,S2R_32,n); }
    for(size_t i=0;i<n;i++){
        if(signed_pool){ int64_t v=(int64_t)(rnd()%mod)-(int64_t)(mod/2);
            s2r_set_signed(&a,i,v); s2r_set_signed(&b,i,v); }
        else { uint64_t v=rnd()%mod; s2r_set(&a,i,v); s2r_set(&b,i,v); }
    }
    a.count=n; b.count=n;
    s2r_summarize(&a);                       /* a tem resumo, b não */
    CHECK(s2r_has_summary(&a) && !s2r_has_summary(&b), "%s: os dois caminhos armados", nm);
    int okg=1,okl=1,okr=1;
    int64_t lo=signed_pool? -(int64_t)mod : -2, hi=(int64_t)mod+2;
    for(int64_t t=lo;t<=hi;t++){
        if(signed_pool){
            if(s2r_count_gt_signed_fast(&a,t)!=s2r_count_gt_signed_fast(&b,t)) okg=0;
            if(s2r_count_lt_signed_fast(&a,t)!=s2r_count_lt_signed_fast(&b,t)) okl=0;
        } else {
            if(t<0) continue;
            if(s2r_count_gt_fast(&a,(uint64_t)t)!=s2r_count_gt_fast(&b,(uint64_t)t)) okg=0;
            if(s2r_count_lt_fast(&a,(uint64_t)t)!=s2r_count_lt_fast(&b,(uint64_t)t)) okl=0;
        }
    }
    CHECK(okg, "%s: count_gt com resumo == sem", nm);
    CHECK(okl, "%s: count_lt com resumo == sem", nm);
    for(int64_t l=lo;l<=hi && okr;l+=3){
    for(int64_t h=l;h<=hi && okr;h+=5){
        if(signed_pool){ if(s2r_count_range_signed_fast(&a,l,h)!=s2r_count_range_signed_fast(&b,l,h)) okr=0; }
        else { if(l<0||h<0) continue;
               if(s2r_count_range_fast(&a,(uint64_t)l,(uint64_t)h)!=s2r_count_range_fast(&b,(uint64_t)l,(uint64_t)h)) okr=0; }
    } }
    CHECK(okr, "%s: count_range com resumo == sem", nm);
    s2r_pool_free(&a); s2r_pool_free(&b);
}

/* ================= 2. índice acumulado ================= */
static void test_index(void)
{
    printf("-- 2. índice acumulado: exato, e recusa quando obsoleto --\n");
    {   const size_t N=50000;
        S2RPool p; s2r_pool_init(&p,S2R_8,N);
        for(size_t i=0;i<N;i++) s2r_set(&p,i,rnd()%201);
        p.count=N;
        S2RIndex ix;
        CHECK(s2r_index_build(&ix,&p), "u8: build");
        CHECK(s2r_index_bytes(&ix)==257*sizeof(uint64_t), "u8: 2 KB, e não cresce com n");
        int ok=1, allok=1;
        for(uint64_t l=0;l<=256 && allok;l++){
        for(uint64_t h=l;h<=256 && allok;h++){
            size_t byix=s2r_index_count_range(&ix,&p,(int64_t)l,(int64_t)h,&ok);
            if(!ok || byix!=s2r_count_range_fast(&p,l,h)) allok=0;
        } }
        CHECK(allok, "u8: count_range idêntico em TODO par de extremos (33.153 pares)");
        int okg=1;
        for(uint64_t t=0;t<=256;t++){
            size_t a=s2r_index_count_gt(&ix,&p,(int64_t)t,&ok);
            if(!ok || a!=s2r_count_gt_fast(&p,t)) okg=0;
            size_t b=s2r_index_count_lt(&ix,&p,(int64_t)t,&ok);
            if(!ok || b!=s2r_count_lt_fast(&p,t)) okg=0;
            size_t c=s2r_index_count_eq(&ix,&p,(int64_t)t,&ok);
            if(!ok || c!=s2r_count_eq_fast(&p,t)) okg=0;
        }
        CHECK(okg, "u8: gt/lt/eq idênticos em todo limiar");
        /* uma escrita torna o índice obsoleto: ele TEM de recusar */
        s2r_set(&p,0,7);
        CHECK(!s2r_index_valid(&ix,&p), "escrita invalida o índice");
        (void)s2r_index_count_range(&ix,&p,0,255,&ok);
        CHECK(!ok, "índice obsoleto RECUSA responder em vez de mentir");
        s2r_index_free(&ix); s2r_pool_free(&p); }

    {   const size_t N=40000;
        S2RPool p; s2r_pool_init(&p,S2R_16,N);
        for(size_t i=0;i<N;i++) s2r_set(&p,i,rnd()%50000);
        p.count=N;
        S2RIndex ix; CHECK(s2r_index_build(&ix,&p), "u16: build");
        int ok=1, allok=1;
        for(uint64_t l=0;l<50000 && allok;l+=997) for(uint64_t h=l;h<50000 && allok;h+=1231){
            size_t byix=s2r_index_count_range(&ix,&p,(int64_t)l,(int64_t)h,&ok);
            if(!ok || byix!=s2r_count_range_fast(&p,l,h)) allok=0;
        }
        CHECK(allok, "u16: count_range idêntico na amostra de pares");
        s2r_index_free(&ix); s2r_pool_free(&p); }

    {   const size_t N=30000;
        S2RPool p; s2r_pool_init(&p,S2R_I8,N);
        for(size_t i=0;i<N;i++) s2r_set_signed(&p,i,(int64_t)(rnd()%200)-100);
        p.count=N;
        S2RIndex ix; CHECK(s2r_index_build(&ix,&p), "i8: build");
        int ok=1, allok=1;
        for(int64_t l=-130;l<=130 && allok;l++) for(int64_t h=l;h<=130 && allok;h++){
            size_t byix=s2r_index_count_range(&ix,&p,l,h,&ok);
            if(!ok || byix!=s2r_count_range_signed_fast(&p,l,h)) allok=0;
        }
        CHECK(allok, "i8: count_range com sinal idêntico em todo par");
        s2r_index_free(&ix); s2r_pool_free(&p); }

    {   S2RPool p; s2r_pool_init(&p,S2R_32,100);
        for(size_t i=0;i<100;i++) s2r_set(&p,i,i);
        p.count=100;
        S2RIndex ix;
        CHECK(!s2r_index_build(&ix,&p), "u32: recusa construir (a tabela seria de 16 GB)");
        s2r_pool_free(&p); }
}

/* ================= 3. planejador de bloco ================= */
static void test_planner(void)
{
    printf("-- 3. o planejador prevê exatamente o que o build produz --\n");
    const size_t N=120000;
    uint64_t *v=(uint64_t*)malloc(N*8);
    const char *nm[7]={"timestamps 60 s","medicao +-30","12 distintos","constante",
                       "u64 aleatorio","0..200","crescente denso"};
    int tot=0, bad=0;
    for(int c=0;c<7;c++){
        for(size_t i=0;i<N;i++){
            switch(c){ case 0: v[i]=1700000000ull+(uint64_t)i*60; break;
                       case 1: v[i]=100000+(rnd()%61); break;
                       case 2: v[i]=500+(rnd()%12)*1000; break;
                       case 3: v[i]=42; break;
                       case 4: v[i]=rnd(); break;
                       case 5: v[i]=rnd()%201; break;
                       default: v[i]=(uint64_t)i; }
        }
        S2RBlockPlan pl[S2R_PLAN_MAX];
        int k=s2r_blocked_plan(v,NULL,N,0,pl,S2R_PLAN_MAX);
        CHECK(k>0, "%s: o planejador devolve candidatos", nm[c]);
        for(int i=0;i<k;i++){
            S2RBlocked b;
            if(!s2r_blocked_build(&b,v,N,pl[i].block)) continue;
            tot++;
            if(s2r_blocked_bytes(&b)!=pl[i].bytes) bad++;
            s2r_blocked_free(&b);
        }
        /* a escolha automática constrói uma coluna correta */
        S2RBlocked b;
        CHECK(s2r_blocked_build_auto(&b,v,N), "%s: build_auto", nm[c]);
        int rt=1; for(size_t i=0;i<N;i++) if(s2r_blocked_get(&b,i)!=v[i]){ rt=0; break; }
        CHECK(rt, "%s: build_auto round trip", nm[c]);
        uint64_t sum=0; for(size_t i=0;i<N;i++) sum+=v[i];
        CHECK(s2r_blocked_sum(&b)==sum, "%s: build_auto sum", nm[c]);
        /* e nunca é pior que o padrão */
        S2RBlocked d; s2r_blocked_build(&d,v,N,S2R_BLOCK_DEFAULT);
        CHECK(s2r_blocked_bytes(&b)<=s2r_blocked_bytes(&d),
              "%s: a escolha automática nunca perde para o padrão (%zu <= %zu)",
              nm[c], s2r_blocked_bytes(&b), s2r_blocked_bytes(&d));
        s2r_blocked_free(&b); s2r_blocked_free(&d);
    }
    CHECK(bad==0, "previsão exata em %d/%d combinações", tot-bad, tot);

    /* com sinal também */
    {   int64_t *sv=(int64_t*)malloc(N*8);
        for(size_t i=0;i<N;i++) sv[i]=-500000+(int64_t)(rnd()%1000)*250;
        S2RBlocked b;
        CHECK(s2r_blocked_build_signed_auto(&b,sv,N), "com sinal: build_auto");
        int rt=1; for(size_t i=0;i<N;i++) if(s2r_blocked_get_signed(&b,i)!=sv[i]){ rt=0; break; }
        CHECK(rt, "com sinal: round trip");
        s2r_blocked_free(&b); free(sv); }
    free(v);
}

/* ================= 4. nunca expande ================= */
static void test_never_expands(void)
{
    printf("-- 4. nenhuma forma excede a linha de base int64 --\n");
    const size_t N=60000;
    uint64_t *v=(uint64_t*)malloc(N*8);
    const char *nm[8]={"u64 aleatorio","0..200","12 distintos","constante",
                       "timestamps","booleano","ids 0..1e6","tudo UINT64_MAX"};
    for(int c=0;c<8;c++){
        for(size_t i=0;i<N;i++){
            switch(c){ case 0: v[i]=rnd(); break;
                       case 1: v[i]=rnd()%201; break;
                       case 2: v[i]=500+(rnd()%12)*1000; break;
                       case 3: v[i]=42; break;
                       case 4: v[i]=1700000000ull+(uint64_t)i*60; break;
                       case 5: v[i]=rnd()&1; break;
                       case 6: v[i]=rnd()%1000000; break;
                       default: v[i]=UINT64_MAX; }
        }
        S2RAdvice ad; memset(&ad,0,sizeof ad);
        CHECK(s2r_recommend(v,N,&ad), "%s: recommend", nm[c]);
        CHECK(ad.flat_bytes<=ad.raw_bytes,
              "%s: o pool plano nunca excede int64 (%zu <= %zu)", nm[c], ad.flat_bytes, ad.raw_bytes);
        CHECK(ad.best_bytes<=ad.raw_bytes,
              "%s: a melhor forma nunca excede int64 (%zu <= %zu)", nm[c], ad.best_bytes, ad.raw_bytes);
        CHECK(ad.best!=NULL && ad.block>0, "%s: a recomendação vem nomeada", nm[c]);
    }
    free(v);
}

int main(void)
{
    printf("=== test_summary: resumo do pool, índice, planejador, nunca expande ===\n");
    test_summary_lifecycle();
    printf("-- 1b. predicado com resumo == sem resumo --\n");
    sweep_summary("sem sinal 0..500", 4000, 500, 0);
    sweep_summary("sem sinal 0..20 (faixa curta)", 2000, 20, 0);
    sweep_summary("com sinal -250..250", 4000, 500, 1);
    test_index();
    test_planner();
    test_never_expands();
    printf("------------------------------------------------------------\n");
    printf("  %ld OK, %ld FAIL\n", pass, fail);
    return fail? 1:0;
}
