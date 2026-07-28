/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* test_fuzz_diff.c - fuzz diferencial: formas de coluna e tamanhos de bloco
 * sorteados, tudo conferido contra a referência ingênua.
 *
 * Esta suíte existe por causa de um defeito que as outras 25 não pegaram, e vale
 * dizer por quê. Todas elas testam formas ESCOLHIDAS - faixas plausíveis,
 * outliers plausíveis, tamanhos de bloco plausíveis. O defeito morava numa forma
 * que ninguém escolheria de propósito:
 *
 *     uma coluna SEM SINAL com um valor acima de 2^63
 *
 * A base de cada bloco era guardada em `int64_t` e a MAIOR base era rastreada
 * com comparação com sinal, mesmo numa coluna sem sinal. Uma base acima de
 * INT64_MAX lia como negativa, o máximo corrente ficava pequeno demais, a classe
 * das bases saía estreita demais, e a base era TRUNCADA na escrita. A coluna
 * `{ 1, UINT64_MAX }` classificava as bases em 8 bits e devolvia 255 no lugar de
 * UINT64_MAX - sem erro, sem aviso, sem CRC quebrado. O mesmo valia para a soma
 * de bloco.
 *
 * A lição que fica na suíte: uma bateria de casos escolhidos herda o ponto cego
 * de quem escolheu. Sorteio não tem ponto cego, só precisa de referência.
 *
 * As sementes são fixas: um fuzz que muda a cada execução acha o defeito uma vez
 * e depois some com ele.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smart2raw.h"

static long pass=0, fail=0;
#define CHECK(c, ...) do{ if(c){pass++;} else { if(fail<20){printf("  [FAIL] "); printf(__VA_ARGS__); printf("\n");} fail++; } }while(0)

static uint64_t rs;
static uint64_t rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return rs; }

/* Cada forma estressa um caminho diferente. A 7 e a 8 são as que importam:
 * elas ultrapassam 2^63, que é onde uma comparação com sinal mente. */
static void gen(uint64_t *v, size_t n, int shape){
    switch(shape){
    case 0:  for(size_t i=0;i<n;i++) v[i]=rnd()%2; break;                  /* booleano   */
    case 1:  for(size_t i=0;i<n;i++) v[i]=rnd()%256; break;                /* u8 cheio   */
    case 2:  for(size_t i=0;i<n;i++) v[i]=rnd(); break;                    /* u64 pleno  */
    case 3:  for(size_t i=0;i<n;i++) v[i]=777; break;                      /* constante  */
    case 4:  for(size_t i=0;i<n;i++) v[i]=9000000000ull+(rnd()%3); break;  /* banda alta */
    case 5:  for(size_t i=0;i<n;i++) v[i]=(uint64_t)i; break;              /* crescente  */
    case 6:  for(size_t i=0;i<n;i++) v[i]=n-i-1; break;                    /* decrescente*/
    case 7:  for(size_t i=0;i<n;i++) v[i]=(rnd()%50)?1:UINT64_MAX; break;  /* > 2^63     */
    case 8:  for(size_t i=0;i<n;i++) v[i]=UINT64_MAX-(rnd()%4); break;     /* topo de u64*/
    case 9:  for(size_t i=0;i<n;i++) v[i]=(i%7)?0:255; break;              /* esparso    */
    case 10: for(size_t i=0;i<n;i++) v[i]=(uint64_t)(i/13); break;         /* platôs     */
    default: for(size_t i=0;i<n;i++) v[i]=rnd()%65536; break;              /* u16 cheio  */
    }
}

static void round_(unsigned long iters, uint64_t seed)
{
    rs = seed;
    uint64_t *v=(uint64_t*)malloc(2048*sizeof(uint64_t));
    if(!v) return;
    for(unsigned long it=0; it<iters; it++){
        size_t n = 1 + (size_t)(rnd()%2048);
        int shape = (int)(rnd()%12);
        gen(v,n,shape);
        size_t blk = 1 + (size_t)(rnd()%600);

        S2RBlocked b;
        if(!s2r_blocked_build(&b,v,n,blk)){ CHECK(0,"build n=%zu blk=%zu",n,blk); continue; }
        uint64_t sum=0, mx=v[0], mn=v[0];
        for(size_t i=0;i<n;i++){ sum+=v[i];
            if(v[i]>mx) mx=v[i];
            if(v[i]<mn) mn=v[i]; }
        int rt=1; for(size_t i=0;i<n;i++) if(s2r_blocked_get(&b,i)!=v[i]){ rt=0; break; }
        CHECK(rt,"round trip n=%zu blk=%zu forma=%d",n,blk,shape);
        CHECK(s2r_blocked_sum(&b)==sum,          "sum n=%zu blk=%zu forma=%d",n,blk,shape);
        CHECK(s2r_blocked_sum_fast(&b)==sum,     "sum_fast n=%zu blk=%zu forma=%d",n,blk,shape);
        CHECK(s2r_blocked_max(&b)==mx,           "max n=%zu blk=%zu forma=%d",n,blk,shape);
        CHECK(s2r_blocked_min(&b)==mn,           "min n=%zu blk=%zu forma=%d",n,blk,shape);
        uint64_t thrs[8]={0, mn, mn?mn-1:0, mx, mx==UINT64_MAX?mx:mx+1,
                          v[rnd()%n], UINT64_MAX, (mn>>1)+(mx>>1)};
        for(int k=0;k<8;k++){
            size_t c=0; for(size_t i=0;i<n;i++) c+=(v[i]>thrs[k]);
            CHECK(s2r_blocked_count_gt(&b,thrs[k])==c,
                  "count_gt(%llu) n=%zu blk=%zu forma=%d",
                  (unsigned long long)thrs[k],n,blk,shape);
        }
        s2r_blocked_free(&b);

        S2RPool p;
        if(s2r_pool_init(&p,s2r_classify_array(v,n),n)){
            for(size_t i=0;i<n;i++) s2r_set(&p,i,v[i]);
            p.count=n;
            for(int k=0;k<4;k++){
                uint64_t t=thrs[k];
                size_t cg=0,cl=0,ce=0;
                for(size_t i=0;i<n;i++){ cg+=(v[i]>t); cl+=(v[i]<t); ce+=(v[i]==t); }
                CHECK(s2r_count_gt_fast(&p,t)==cg,"pool count_gt forma=%d",shape);
                CHECK(s2r_count_lt_fast(&p,t)==cl,"pool count_lt forma=%d",shape);
                CHECK(s2r_count_eq_fast(&p,t)==ce,"pool count_eq forma=%d",shape);
            }
            uint64_t lo=thrs[rnd()%8], hi=thrs[rnd()%8];
            if(lo>hi){ uint64_t t=lo; lo=hi; hi=t; }
            size_t cr=0; uint64_t sr=0;
            for(size_t i=0;i<n;i++) if(v[i]>=lo&&v[i]<=hi){ cr++; sr+=v[i]; }
            CHECK(s2r_count_range_fast(&p,lo,hi)==cr,"pool count_range forma=%d",shape);
            CHECK(s2r_sum_if_fast(&p,lo,hi)==sr,     "pool sum_if forma=%d",shape);
            CHECK(s2r_sum_fast(&p)==sum,             "pool sum forma=%d",shape);
            s2r_pool_free(&p);
        }

#if S2R_HAS_STDIO
        if(it%53==0){
            S2RBlocked a,c;
            if(s2r_blocked_build(&a,v,n,blk)){
                if(s2r_blocked_save(&a,"fzd.s2r")==S2R_OK
                   && s2r_blocked_load(&c,"fzd.s2r")==S2R_OK){
                    int same=1;
                    for(size_t i=0;i<n;i++)
                        if(s2r_blocked_get(&a,i)!=s2r_blocked_get(&c,i)){ same=0; break; }
                    CHECK(same,"round trip de arquivo n=%zu blk=%zu forma=%d",n,blk,shape);
                    CHECK(s2r_blocked_sum(&c)==sum,"sum apos load forma=%d",shape);
                    s2r_blocked_free(&c);
                } else CHECK(0,"save/load forma=%d",shape);
                s2r_blocked_free(&a);
            }
            remove("fzd.s2r");
        }
#endif
    }
    free(v);
}

int main(void)
{
    printf("=== test_fuzz_diff: sorteio contra referência ingênua ===\n");

    /* o caso mínimo, fixo, porque um número no relatório vale mais que um sorteio */
    {   uint64_t v[2]={1,UINT64_MAX};
        S2RBlocked b;
        CHECK(s2r_blocked_build(&b,v,2,1), "caso mínimo: build");
        CHECK(s2r_blocked_get(&b,1)==UINT64_MAX,
              "coluna sem sinal com valor acima de 2^63: get devolve %llu",
              (unsigned long long)s2r_blocked_get(&b,1));
        CHECK(s2r_blocked_max(&b)==UINT64_MAX, "caso mínimo: max");
        CHECK(s2r_blocked_sum(&b)==(uint64_t)(1+UINT64_MAX), "caso mínimo: sum");
        CHECK(s2r_blocked_count_gt(&b,1000)==1, "caso mínimo: count_gt");
        CHECK(s2r_abs_size(b.bcls)==64, "caso mínimo: bases classificadas em 64 bits");
        s2r_blocked_free(&b); }

    /* sementes fixas: um fuzz que muda a cada execução esquece o que já achou */
    static const uint64_t seeds[6] = {
        0x123456789ABCDEFull, 99ull, 987654321ull, 55555ull, 777777ull, 0x2545F4914F6CDD1Dull };
    for(int i=0;i<6;i++) round_(600, seeds[i]);

    printf("------------------------------------------------------------\n");
    printf("  %ld OK, %ld FAIL\n", pass, fail);
    return fail? 1:0;
}
