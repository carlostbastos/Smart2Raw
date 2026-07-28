/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* test_gaps.c - as quatro folgas que a auditoria de 3.5.0 mediu e fechou.
 *
 * 1. ORDEM ESTABELECIDA no pool plano. A camada em blocos responde por busca
 *    binária desde a 3.4.0 porque um bloco é imutável depois do build. Um pool
 *    plano não é: pode ser escrito em qualquer índice, então o fato tem de ser
 *    MANTIDO. O risco inteiro é uma flag obsoleta - ela faria a busca binária
 *    devolver a resposta errada em silêncio, que é a pior falha possível. Por
 *    isso a seção 1 chama TODA função pública que escreve e exige que a flag
 *    tenha caído, em vez de auditar por inspeção.
 *
 * 2. CURA DE SINAL. Uma coluna declarada com sinal que nunca recebe um negativo
 *    fica 2x mais larga do que precisa: 0..200 não cabe em i8 e vai para i16,
 *    quando u8 bastava. Curar isso troca o contrato - depois da cura um push
 *    negativo é RECUSADO - então é uma função separada, e o teste cobra esse
 *    preço explicitamente.
 *
 * 3. COLUNA CONSTANTE carrega zero bits. Guardar um byte por elemento para
 *    repetir o mesmo índice é exatamente o hábito que a biblioteca existe para
 *    quebrar.
 *
 * 4. OS QUATRO PREDICADOS que faltavam na camada em blocos. O mapa de zona vale
 *    MAIS para uma consulta por faixa do que para uma de um lado só, porque uma
 *    janela pode errar o bloco pelas duas pontas.
 *
 * O método em toda parte: mesmos dados, dois caminhos, resposta idêntica.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smart2raw.h"

static long pass=0, fail=0;
#define CHECK(c, ...) do{ if(c){pass++;} else { if(fail<25){printf("  [FAIL] "); printf(__VA_ARGS__); printf("\n");} fail++; } }while(0)

static uint64_t rs=0xB5026F5AA96619Eull;
static uint64_t rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return rs; }

static uint64_t bump(uint64_t v, void *ctx){ (void)ctx; return v+1; }

/* ============ 1. ciclo de vida da flag de ordem ============ */
static void fill_sorted(S2RPool *p, size_t n){
    s2r_pool_init(p,S2R_16,n);
    for(size_t i=0;i<n;i++) s2r_set(p,i,(uint64_t)(i%1000));
    p->count=n;
    /* s2r_set derrubou a flag; estabelece de novo por verificação */
    s2r_mark_sorted(p);
}

static void test_flag_lifecycle(void)
{
    printf("-- 1. a flag de ordem cai em TODA escrita --\n");
    const size_t N=1000;

#define MUTATES(label, ...) do{ \
    S2RPool p; fill_sorted(&p,N); \
    CHECK(s2r_is_known_sorted(&p), "%s: a flag estava posta antes", label); \
    { __VA_ARGS__; } \
    CHECK(!s2r_is_known_sorted(&p), "%s: derruba a flag", label); \
    s2r_pool_free(&p); }while(0)

    MUTATES("s2r_set",                 s2r_set(&p,0,999));
    MUTATES("s2r_set_signed",          s2r_set_signed(&p,0,3));
    MUTATES("s2r_push fora de ordem",  s2r_push(&p,0));
    MUTATES("s2r_push_checked",        (void)s2r_push_checked(&p,0));
    MUTATES("s2r_push_saturate",       (void)s2r_push_saturate(&p,0));
    MUTATES("s2r_push_many",           { uint64_t v[2]={0,1}; (void)s2r_push_many(&p,v,2); });
    MUTATES("s2r_push_adaptive",       (void)s2r_push_adaptive(&p,0));
    MUTATES("s2r_add_scalar",          s2r_add_scalar(&p,5));
    MUTATES("s2r_mul_scalar",          s2r_mul_scalar(&p,3));
    MUTATES("s2r_add_scalar_safe",     (void)s2r_add_scalar_safe(&p,5));
    MUTATES("s2r_mul_scalar_safe",     (void)s2r_mul_scalar_safe(&p,3));
    MUTATES("s2r_transform",           s2r_transform(&p,bump,NULL));
    MUTATES("s2r_remove_swap",         s2r_remove_swap(&p,0));
    MUTATES("s2r_clear",               s2r_clear(&p));
#undef MUTATES

    /* com sinal */
    {   S2RPool p; s2r_pool_init(&p,S2R_I16,200);
        for(size_t i=0;i<200;i++) s2r_set_signed(&p,i,(int64_t)i-100);
        p.count=200; s2r_mark_sorted(&p);
        CHECK(s2r_is_known_sorted(&p), "com sinal: flag posta");
        (void)s2r_add_scalar_signed_safe(&p,7);
        CHECK(!s2r_is_known_sorted(&p), "s2r_add_scalar_signed_safe derruba a flag");
        s2r_pool_free(&p); }
    {   S2RPool p; s2r_pool_init(&p,S2R_I16,200);
        for(size_t i=0;i<200;i++) s2r_set_signed(&p,i,(int64_t)i-100);
        p.count=200; s2r_mark_sorted(&p);
        (void)s2r_mul_scalar_signed_safe(&p,3);
        CHECK(!s2r_is_known_sorted(&p), "s2r_mul_scalar_signed_safe derruba a flag");
        s2r_pool_free(&p); }
    {   S2RPool p; s2r_pool_init(&p,S2R_I16,200);
        for(size_t i=0;i<200;i++) s2r_set_signed(&p,i,(int64_t)i-100);
        p.count=200; s2r_mark_sorted(&p);
        (void)s2r_push_signed_adaptive(&p,-500);
        CHECK(!s2r_is_known_sorted(&p), "s2r_push_signed_adaptive fora de ordem derruba");
        s2r_pool_free(&p); }

    /* apendar EM ORDEM preserva - é o padrão de ingestão que importa */
    {   S2RPool p; s2r_pool_init(&p,S2R_32,10);
        p.count=0; s2r_mark_sorted(&p);
        int kept=1;
        for(uint64_t i=0;i<5000;i++){ s2r_push(&p,1700000000ull+i*60);
            if(!s2r_is_known_sorted(&p)){ kept=0; break; } }
        CHECK(kept, "5000 pushes em ordem preservam a flag");
        s2r_push(&p,0);
        CHECK(!s2r_is_known_sorted(&p), "o primeiro push fora de ordem derruba");
        s2r_pool_free(&p); }

    /* s2r_sort estabelece; e a flag NÃO viaja no arquivo */
    {   S2RPool p; s2r_pool_init(&p,S2R_16,500);
        for(size_t i=0;i<500;i++) s2r_set(&p,i,rnd()%50000);
        p.count=500;
        CHECK(!s2r_is_known_sorted(&p), "dado aleatório: flag ausente");
        CHECK(s2r_sort(&p)==S2R_OK, "sort");
        CHECK(s2r_is_known_sorted(&p), "s2r_sort estabelece a flag");
        CHECK(s2r_unique_sorted(&p)>0, "unique_sorted");
#if S2R_HAS_STDIO
        CHECK(s2r_save_portable(&p,"g.s2r")==S2R_OK, "save");
        S2RPool q;
        CHECK(s2r_load_portable(&q,"g.s2r")==S2R_OK, "load");
        CHECK(!s2r_is_known_sorted(&q), "a flag NÃO viaja no arquivo (direção segura)");
        s2r_pool_free(&q); remove("g.s2r");
#endif
        s2r_pool_free(&p); }
}

/* ============ 2. predicado por ordem == predicado por varredura ============ */
static void sweep_sorted_u(const char *nm, size_t n, uint64_t mod)
{
    S2RPool a, b;
    s2r_pool_init(&a,S2R_32,n); s2r_pool_init(&b,S2R_32,n);
    uint64_t *ref=(uint64_t*)malloc(n*sizeof(uint64_t));
    for(size_t i=0;i<n;i++) ref[i]=rnd()%mod;
    for(size_t i=0;i<n;i++){ s2r_set(&a,i,ref[i]); s2r_set(&b,i,ref[i]); }
    a.count=n; b.count=n;
    s2r_sort(&a); s2r_sort(&b);
    /* a MANTÉM a flag (busca binária); b a perde (varredura vetorizada) */
    b.flags &= (uint8_t)~S2R_FLAG_SORTED;
    CHECK(s2r_is_known_sorted(&a) && !s2r_is_known_sorted(&b), "%s: os dois caminhos armados", nm);
    for(size_t i=0;i<n;i++) ref[i]=s2r_get(&a,i);

    int okg=1,okl=1,oke=1,okr=1; uint64_t bt=0;
    for(uint64_t t=0; t<=mod+1; t++){
        if(s2r_count_gt_fast(&a,t)!=s2r_count_gt_fast(&b,t)){ okg=0; bt=t; break; }
        if(s2r_count_lt_fast(&a,t)!=s2r_count_lt_fast(&b,t)){ okl=0; bt=t; break; }
        if(s2r_count_eq_fast(&a,t)!=s2r_count_eq_fast(&b,t)){ oke=0; bt=t; break; }
    }
    CHECK(okg, "%s: count_gt busca binária == varredura (falhou em %llu)", nm, (unsigned long long)bt);
    CHECK(okl, "%s: count_lt idem (%llu)", nm, (unsigned long long)bt);
    CHECK(oke, "%s: count_eq idem (%llu)", nm, (unsigned long long)bt);
    for(uint64_t l=0; l<=mod+1 && okr; l++)
    for(uint64_t h=l; h<=mod+1 && okr; h++)
        if(s2r_count_range_fast(&a,l,h)!=s2r_count_range_fast(&b,l,h)) okr=0;
    CHECK(okr, "%s: count_range em todo par", nm);

    /* e contra a referência ingênua, não só um contra o outro */
    int okn=1;
    for(uint64_t t=0; t<=mod+1 && okn; t++){
        size_t c=0; for(size_t i=0;i<n;i++) c+=(ref[i]>t);
        if(s2r_count_gt_fast(&a,t)!=c) okn=0;
    }
    CHECK(okn, "%s: count_gt contra a referência ingênua", nm);
    free(ref); s2r_pool_free(&a); s2r_pool_free(&b);
}

static void sweep_sorted_s(const char *nm, size_t n)
{
    S2RPool a,b; s2r_pool_init(&a,S2R_I32,n); s2r_pool_init(&b,S2R_I32,n);
    int64_t *ref=(int64_t*)malloc(n*sizeof(int64_t));
    for(size_t i=0;i<n;i++) ref[i]=(int64_t)(rnd()%400)-200;
    for(size_t i=0;i<n;i++){ s2r_set_signed(&a,i,ref[i]); s2r_set_signed(&b,i,ref[i]); }
    a.count=n; b.count=n;
    s2r_sort(&a); s2r_sort(&b);
    b.flags &= (uint8_t)~S2R_FLAG_SORTED;
    for(size_t i=0;i<n;i++) ref[i]=s2r_get_signed(&a,i);
    int okg=1,okl=1,oke=1,okr=1,okn=1;
    for(int64_t t=-205; t<=205; t++){
        if(s2r_count_gt_signed_fast(&a,t)!=s2r_count_gt_signed_fast(&b,t)) okg=0;
        if(s2r_count_lt_signed_fast(&a,t)!=s2r_count_lt_signed_fast(&b,t)) okl=0;
        if(s2r_count_eq_signed_fast(&a,t)!=s2r_count_eq_signed_fast(&b,t)) oke=0;
        size_t c=0; for(size_t i=0;i<n;i++) c+=(ref[i]>t);
        if(s2r_count_gt_signed_fast(&a,t)!=c) okn=0;
    }
    CHECK(okg, "%s: count_gt com sinal", nm);
    CHECK(okl, "%s: count_lt com sinal", nm);
    CHECK(oke, "%s: count_eq com sinal", nm);
    CHECK(okn, "%s: contra a referência ingênua", nm);
    for(int64_t l=-205; l<=205 && okr; l+=3)
    for(int64_t h=l; h<=205 && okr; h+=3)
        if(s2r_count_range_signed_fast(&a,l,h)!=s2r_count_range_signed_fast(&b,l,h)) okr=0;
    CHECK(okr, "%s: count_range com sinal", nm);
    free(ref); s2r_pool_free(&a); s2r_pool_free(&b);
}

/* ============ 3. cura de sinal ============ */
static void test_sign_healing(void)
{
    printf("-- 2. cura através da fronteira do sinal --\n");
    const size_t N=5000;
    {   /* declarada com sinal, nunca recebeu negativo: i16 -> u8 */
        S2RPool p; s2r_pool_init(&p,S2R_I16,N);
        int64_t *ref=(int64_t*)malloc(N*sizeof(int64_t));
        for(size_t i=0;i<N;i++){ ref[i]=(int64_t)(rnd()%201); s2r_set_signed(&p,i,ref[i]); }
        p.count=N;
        CHECK(s2r_abs_size(p.size)==16 && s2r_is_signed(&p), "antes: i16");
        s2r_fit_class(&p);
        CHECK(s2r_abs_size(p.size)==16, "s2r_fit_class NÃO troca o sinal (comportamento antigo intacto)");
        CHECK(s2r_fit_class_signedness(&p), "cura de sinal");
        CHECK(s2r_abs_size(p.size)==8 && !s2r_is_signed(&p), "depois: u8, 2x menor");
        int same=1; for(size_t i=0;i<N;i++) if((int64_t)s2r_get(&p,i)!=ref[i]){ same=0; break; }
        CHECK(same, "todo valor preservado pela cura");
        /* o preço, cobrado explicitamente */
        CHECK(s2r_push_signed_adaptive(&p,-1)!=S2R_OK, "depois da cura um push negativo é RECUSADO");
        free(ref); s2r_pool_free(&p); }
    {   /* tem negativo: o sinal é merecido, nada muda */
        S2RPool p; s2r_pool_init(&p,S2R_I16,100);
        for(size_t i=0;i<100;i++) s2r_set_signed(&p,i,(int64_t)i-50);
        p.count=100;
        CHECK(s2r_fit_class_signedness(&p), "com negativo: cura roda");
        CHECK(s2r_is_signed(&p), "com negativo: continua com sinal");
        CHECK(s2r_abs_size(p.size)==8, "com negativo: mas estreita para i8");
        int same=1; for(size_t i=0;i<100;i++) if(s2r_get_signed(&p,i)!=(int64_t)i-50){ same=0; break; }
        CHECK(same, "com negativo: valores preservados");
        s2r_pool_free(&p); }
    {   /* já sem sinal: cai no fit_class normal */
        S2RPool p; s2r_pool_init(&p,S2R_32,100);
        for(size_t i=0;i<100;i++) s2r_set(&p,i,i);
        p.count=100;
        CHECK(s2r_fit_class_signedness(&p) && s2r_abs_size(p.size)==8, "sem sinal: estreita normal");
        s2r_pool_free(&p); }
    {   /* i64 com valores grandes mas não negativos -> u32 */
        S2RPool p; s2r_pool_init(&p,S2R_I64,100);
        for(size_t i=0;i<100;i++) s2r_set_signed(&p,i,(int64_t)3000000000ull+(int64_t)i);
        p.count=100;
        CHECK(s2r_fit_class_signedness(&p), "i64 sem negativos: cura");
        CHECK(s2r_abs_size(p.size)==32 && !s2r_is_signed(&p), "i64 -> u32 (3e9 não cabe em i32)");
        int same=1; for(size_t i=0;i<100;i++) if(s2r_get(&p,i)!=3000000000ull+i){ same=0; break; }
        CHECK(same, "i64 -> u32: valores preservados");
        s2r_pool_free(&p); }
}

/* ============ 4. os quatro predicados da camada em blocos ============ */
static void verify_blk_preds(const char *nm, const uint64_t *a, size_t n, size_t blk)
{
    S2RBlocked b;
    CHECK(s2r_blocked_build(&b,a,n,blk), "%s/blk%zu: build", nm, blk);
    uint64_t mn=a[0], mx=a[0];
    for(size_t i=0;i<n;i++){ if(a[i]<mn)mn=a[i]; if(a[i]>mx)mx=a[i]; }
    /* limiares candidatos: cada valor distinto e seus dois vizinhos */
    uint64_t cd[200]; size_t nc=0;
    { uint64_t d[64]; size_t nd=0;
      for(size_t i=0;i<n && nd<64;i++){ int seen=0;
        for(size_t k=0;k<nd;k++){ if(d[k]==a[i]){seen=1;break;} }
        if(!seen) d[nd++]=a[i]; }
      for(size_t k=0;k<nd && nc<198;k++){ if(d[k]) cd[nc++]=d[k]-1; cd[nc++]=d[k]; cd[nc++]=d[k]+1; }
      for(size_t i=0;i<nc;i++) for(size_t j=i+1;j<nc;j++)
        if(cd[j]<cd[i]){ uint64_t t=cd[i];cd[i]=cd[j];cd[j]=t; }
      size_t w=0; for(size_t i=0;i<nc;i++) if(!w||cd[w-1]!=cd[i]) cd[w++]=cd[i]; nc=w; }

    int okl=1,oke=1,okr=1,oks=1; uint64_t bl=0,bh=0;
    for(size_t k=0;k<nc;k++){
        uint64_t t=cd[k]; size_t cl=0,ce=0;
        for(size_t i=0;i<n;i++){ cl+=(a[i]<t); ce+=(a[i]==t); }
        if(s2r_blocked_count_lt(&b,t)!=cl){ okl=0; bl=t; break; }
        if(s2r_blocked_count_eq(&b,t)!=ce){ oke=0; bl=t; break; }
    }
    CHECK(okl, "%s/blk%zu: count_lt (falhou em %llu)", nm, blk, (unsigned long long)bl);
    CHECK(oke, "%s/blk%zu: count_eq (falhou em %llu)", nm, blk, (unsigned long long)bl);
    for(size_t x=0;x<nc && okr && oks;x++)
    for(size_t y=x;y<nc && okr && oks;y++){
        uint64_t l=cd[x], h=cd[y]; size_t cr=0; uint64_t sr=0;
        for(size_t i=0;i<n;i++) if(a[i]>=l && a[i]<=h){ cr++; sr+=a[i]; }
        if(s2r_blocked_count_range(&b,l,h)!=cr){ okr=0; bl=l; bh=h; }
        else if(s2r_blocked_sum_if(&b,l,h)!=sr){ oks=0; bl=l; bh=h; }
    }
    CHECK(okr, "%s/blk%zu: count_range em %zu pares (falhou em [%llu,%llu])", nm, blk,
          nc*(nc+1)/2, (unsigned long long)bl, (unsigned long long)bh);
    CHECK(oks, "%s/blk%zu: sum_if em %zu pares (falhou em [%llu,%llu])", nm, blk,
          nc*(nc+1)/2, (unsigned long long)bl, (unsigned long long)bh);
    /* a soma de toda a faixa tem de bater com a soma de zona */
    CHECK(s2r_blocked_sum_if(&b,mn,mx)==s2r_blocked_sum(&b), "%s/blk%zu: sum_if(min,max) == sum", nm, blk);
    CHECK(s2r_blocked_count_range(&b,mn,mx)==n, "%s/blk%zu: count_range(min,max) == n", nm, blk);
    CHECK(s2r_blocked_count_range(&b,mx+1,mx+2)==0, "%s/blk%zu: janela fora da faixa == 0", nm, blk);
    s2r_blocked_free(&b);
}

int main(void)
{
    printf("=== test_gaps: ordem, sinal, coluna constante, predicados em bloco ===\n");

    test_flag_lifecycle();

    printf("-- 1b. busca binária == varredura, mesmos dados --\n");
    sweep_sorted_u("ordenado, 5000 x 0..200", 5000, 200);
    sweep_sorted_u("ordenado, 300 x 0..60 (abaixo do limiar)", 300, 60);
    sweep_sorted_u("ordenado, 20000 x 0..30 (muitos repetidos)", 20000, 30);
    sweep_sorted_s("ordenado com sinal, 5000 x -200..200", 5000);

    test_sign_healing();

    printf("-- 3. coluna constante --\n");
    {   uint64_t a[4000]; for(size_t i=0;i<4000;i++) a[i]=777777;
        S2RAffine af; CHECK(s2r_affine_build(&af,a,4000), "constante: build");
        CHECK(af.is_const, "constante: detectada");
        CHECK(s2r_affine_bytes(&af)==0, "constante: ZERO bytes de payload");
        int rt=1; for(size_t i=0;i<4000;i++) if(s2r_affine_get(&af,i)!=777777){ rt=0; break; }
        CHECK(rt, "constante: round trip");
        CHECK(s2r_affine_sum(&af)==777777ull*4000, "constante: sum");
        CHECK(s2r_affine_count_gt(&af,777776)==4000, "constante: count_gt abaixo");
        CHECK(s2r_affine_count_gt(&af,777777)==0,    "constante: count_gt igual");
        CHECK(s2r_affine_count_lt(&af,777777)==0,    "constante: count_lt igual");
        CHECK(s2r_affine_count_lt(&af,777778)==4000, "constante: count_lt acima");
        CHECK(s2r_affine_count_eq(&af,777777)==4000, "constante: count_eq");
        CHECK(s2r_affine_count_eq(&af,777778)==0,    "constante: count_eq vizinho");
        CHECK(s2r_affine_count_range(&af,0,777776)==0,       "constante: faixa abaixo");
        CHECK(s2r_affine_count_range(&af,777778,999999)==0,  "constante: faixa acima");
        CHECK(s2r_affine_count_range(&af,777777,777777)==4000,"constante: faixa exata");
        CHECK(s2r_affine_sum_if(&af,777777,777777)==777777ull*4000, "constante: sum_if exata");
        CHECK(s2r_affine_sum_if(&af,777778,999999)==0, "constante: sum_if fora");
        s2r_affine_free(&af); }
    {   int64_t a[100]; for(size_t i=0;i<100;i++) a[i]=-42;
        S2RAffine af; CHECK(s2r_affine_build_signed(&af,a,100), "constante com sinal: build");
        CHECK(af.is_const && s2r_affine_bytes(&af)==0, "constante com sinal: zero payload");
        int rt=1; for(size_t i=0;i<100;i++) if(s2r_affine_get_signed(&af,i)!=-42){ rt=0; break; }
        CHECK(rt, "constante com sinal: round trip");
        CHECK(s2r_affine_sum_signed(&af)==-4200, "constante com sinal: sum");
        s2r_affine_free(&af); }

    printf("-- 4. count_lt / count_eq / count_range / sum_if em blocos --\n");
    {   uint64_t a[3000]; for(size_t i=0;i<3000;i++) a[i]=100+(rnd()%40);
        for(size_t blk=64; blk<=2048; blk*=4) verify_blk_preds("faixa estreita", a, 3000, blk); }
    {   uint64_t a[3000]; for(size_t i=0;i<3000;i++) a[i]=500+(rnd()%12)*1000;
        for(size_t blk=64; blk<=2048; blk*=4) verify_blk_preds("com passo 1000", a, 3000, blk); }
    {   uint64_t a[2000]; for(size_t i=0;i<2000;i++) a[i]=9000000000ull+(rnd()%50);
        for(size_t blk=128; blk<=1024; blk*=4) verify_blk_preds("banda alta em 9e9", a, 2000, blk); }
    {   uint64_t a[1000]; for(size_t i=0;i<1000;i++) a[i]=31337;
        for(size_t blk=64; blk<=512; blk*=4) verify_blk_preds("blocos constantes", a, 1000, blk); }
    {   uint64_t a[1024]; for(size_t i=0;i<1024;i++) a[i]=(uint64_t)i;
        verify_blk_preds("crescente (blocos ordenados)", a, 1024, 256); }
    {   uint64_t a[5]={7,7,7,7,7};
        verify_blk_preds("cinco iguais", a, 5, 2); }
    {   S2RBlocked b; uint64_t z[1]={0};
        CHECK(s2r_blocked_build(&b,z,0,64), "vazio: build");
        CHECK(s2r_blocked_count_range(&b,0,100)==0 && s2r_blocked_count_lt(&b,5)==0
              && s2r_blocked_count_eq(&b,0)==0 && s2r_blocked_sum_if(&b,0,100)==0,
              "vazio: os quatro devolvem 0");
        s2r_blocked_free(&b); }

    printf("------------------------------------------------------------\n");
    printf("  %ld OK, %ld FAIL\n", pass, fail);
    return fail? 1:0;
}
