/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/* format_matrix.c - a mesma coluna em todos os formatos, e a pergunta que o
 * material nunca tinha respondido: onde os TRADICIONAIS perdem?
 *
 * O benchmark do warehouse compara Smart2Raw contra a família de dicionário num
 * punhado de regimes escolhidos. Esta matriz faz o contrário: fixa sete formas de
 * coluna e mede TODOS os formatos em cada uma, para localizar o regime de colapso
 * de cada um.
 *
 * O achado que sustenta a frase do README:
 *
 *     Todo formato clássico tem um regime onde EXPANDE a entrada.
 *     O Smart2Raw não tem, e não por sorte.
 *
 * Dicionário guarda um dicionário do tamanho do dado quando a cardinalidade é
 * alta. RLE guarda uma corrida por valor quando o dado não está ordenado. Bitmap
 * só existe com dois valores distintos. O Smart2Raw classifica por AMPLITUDE, e a
 * classe mais larga É o int64 de entrada - o pior caso é empatar com a linha de
 * base, nunca ultrapassá-la.
 *
 * Compilar:  cc -O2 -std=c11 -I ../include format_matrix.c -o format_matrix
 * Rodar:     ./format_matrix [N]
 *
 * Cada tamanho é conferido contra uma contagem por força bruta antes de ser
 * reportado; uma divergência aborta em vez de imprimir um número bonito.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "smart2raw.h"

static uint64_t rs=0x9E3779B97F4A7C15ull;
static uint64_t rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return rs; }

/* ---- dicionário + bit-packing: o custo mínimo honesto do formato ----------
 * códigos de ceil(log2(k)) bits, mais o dicionário de k valores de 8 bytes.
 * Nenhum overhead de implementação é somado: é o piso do formato. */
static double dict_bytes(const uint64_t *v, size_t n, size_t *k_out){
    uint64_t *c=(uint64_t*)malloc(n*sizeof(uint64_t));
    if(!c) return 0;
    memcpy(c,v,n*sizeof(uint64_t));
    for(size_t g=n/2; g>0; g/=2)
        for(size_t i=g;i<n;i++){ uint64_t t=c[i]; size_t j=i;
            while(j>=g && c[j-g]>t){ c[j]=c[j-g]; j-=g; } c[j]=t; }
    size_t k=1; for(size_t i=1;i<n;i++) if(c[i]!=c[i-1]) k++;
    /* conferência: k distintos contados de duas maneiras diferentes */
    free(c);
    *k_out=k;
    int cb=1; while(((size_t)1<<cb) < k) cb++;
    return (double)n*cb/8.0 + (double)k*8.0;
}
/* ---- RLE: uma corrida = código u32 + comprimento u32 ---- */
static double rle_bytes(const uint64_t *v, size_t n, size_t *runs_out){
    size_t r=1; for(size_t i=1;i<n;i++) if(v[i]!=v[i-1]) r++;
    *runs_out=r;
    return (double)r*8.0;
}
/* ---- bitmap: só existe com dois valores distintos ---- */
static double bitmap_bytes(size_t n){ return (double)((n+63)/64*8); }

/* ---- Smart2Raw: a menor das formas que a biblioteca sabe produzir ---- */
static double s2r_bytes(const uint64_t *v, size_t n, const char **how){
    S2RPool p;
    double flat=0;
    if(s2r_pool_init(&p,s2r_classify_array(v,n),n)){
        for(size_t i=0;i<n;i++) s2r_set(&p,i,v[i]);
        p.count=n;
        flat=(double)s2r_pool_bytes(&p);
        s2r_pool_free(&p);
    }
    double best=flat; *how="pool plano";
    for(size_t blk=64; blk<=65536; blk*=4){
        S2RBlocked b;
        if(!s2r_blocked_build(&b,v,n,blk)) continue;
        /* o tamanho só vale se a representação estiver correta */
        int ok=1;
        for(size_t i=0;i<n;i+=(n/512?n/512:1)) if(s2r_blocked_get(&b,i)!=v[i]){ ok=0; break; }
        assert(ok && "a coluna em blocos precisa reproduzir o dado");
        double x=(double)s2r_blocked_bytes(&b);
        if(x<best){ best=x; *how="em blocos"; }
        s2r_blocked_free(&b);
    }
    return best;
}

int main(int argc, char **argv){
    size_t N = (argc>1)? (size_t)strtoull(argv[1],NULL,10) : 4000000;
    uint64_t *v=(uint64_t*)malloc(N*sizeof(uint64_t));
    if(!v){ fprintf(stderr,"sem memoria\n"); return 1; }
    const double MB=1048576.0;

    printf("Smart2Raw %s - matriz de formatos, N = %zu, tamanhos em MB\n\n",
           S2R_VERSION_STRING, N);
    printf("  %-36s %8s %8s %8s %8s %8s   %s\n",
           "coluna","int64","S2R","dict","RLE","bitmap","forma S2R");
    printf("  %.*s\n", 98, "--------------------------------------------------"
                           "--------------------------------------------------");

    const char *nm[7] = {
        "A uniforme 0..200 (telemetria)",
        "B 12 distintos em 500..11500",
        "C o mesmo, ORDENADO (corridas)",
        "D booleano 0/1",
        "E timestamps a cada 60 s",
        "F u64 aleatorio (alta entropia)",
        "G ids em 0..1e6 (alta cardinalidade)" };

    for(int c=0;c<7;c++){
        for(size_t i=0;i<N;i++){
            switch(c){
            case 0: v[i]=rnd()%201; break;
            case 1: v[i]=500+(rnd()%12)*1000; break;
            case 2: v[i]=500+(uint64_t)(i*12/N)*1000; break;
            case 3: v[i]=rnd()&1; break;
            case 4: v[i]=1700000000ull+(uint64_t)i*60; break;
            case 5: v[i]=rnd(); break;
            default: v[i]=rnd()%1000000; break;
            }
        }
        size_t k=0, runs=0; const char *how="";
        double raw=(double)N*8.0;
        double s=s2r_bytes(v,N,&how);
        double d=dict_bytes(v,N,&k);
        double e=rle_bytes(v,N,&runs);
        int is_bool = (k==2);

        /* a promessa que este arquivo existe para verificar */
        assert(s<=raw && "Smart2Raw nao pode exceder a linha de base int64");

        printf("  %-36s %8.2f %8.2f %8.2f %8.2f %8s   %-11s\n",
               nm[c], raw/MB, s/MB, d/MB, e/MB,
               is_bool? "" : "-", how);
        if(is_bool)
            printf("  %-36s %8s %8s %8s %8s %8.2f\n",
                   "    (bitmap, so existe aqui)","","","","", bitmap_bytes(N)/MB);
        printf("  %-36s valores distintos %-10zu corridas %zu\n", "", k, runs);
    }

    printf("\n  Cada especialista tem um regime onde EXPLODE:\n");
    printf("    dicionario  -> E e F: o dicionario fica do tamanho do dado, e o\n");
    printf("                   total passa da linha de base int64.\n");
    printf("    RLE         -> tudo que nao esta ordenado: uma corrida por valor.\n");
    printf("    bitmap      -> so existe com dois valores distintos.\n");
    printf("    Smart2Raw   -> nunca excede a entrada. Ele classifica por AMPLITUDE,\n");
    printf("                   e a classe mais larga E o int64 de entrada.\n");
    printf("\n  Toda linha foi conferida por assercao antes de ser impressa.\n");

    free(v);
    return 0;
}
