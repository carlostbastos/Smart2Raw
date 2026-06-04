/*
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
/*
 * s2r_convert.c - single-cycle, single-core converter.
 * Build: gcc -O2 -I ../include s2r_convert.c -o s2r_convert
 * Usage: s2r_convert <in.txt|-> <out.txt> [--op none|add|mul] [--by N]
 *                    [--signed] [--cap 32|64]
 *
 * Cycle (1 core): CONVERT (text -> compact, classify) -> PROCESS in place on the
 * compact form (overflow-safe arithmetic, no truncation) -> DECONVERT (compact ->
 * text). The overflow ceiling is --cap (default 32): if the operation would take
 * the result above 32 bits, the converter REFUSES instead of promoting beyond it
 * ("32-bit overflow only"). Use --cap 64 to allow promotion up to 64 bits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "smart2raw.h"

static int64_t *read_ints(const char *path, size_t *n) {
    FILE *f = strcmp(path,"-")? fopen(path,"r") : stdin;
    if (!f) { perror("input"); return NULL; }
    size_t cap=1024,k=0; int64_t *v=malloc(cap*sizeof(int64_t));
    while (fscanf(f, "%" SCNd64, &v[k])==1) if(++k==cap){cap*=2;v=realloc(v,cap*sizeof(int64_t));}
    if (f!=stdin) fclose(f);
    *n=k;
    return v;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr,
        "usage: s2r_convert <in|-> <out> [--op none|add|mul] [--by N] [--signed] [--cap 32|64]\n"); return 2; }
    const char *in=argv[1], *out=argv[2];
    const char *op="none"; long by=0, cap=32; int is_signed=0;
    for (int i=3;i<argc;i++){
        if(!strcmp(argv[i],"--op")&&i+1<argc) op=argv[++i];
        else if(!strcmp(argv[i],"--by")&&i+1<argc) by=strtol(argv[++i],NULL,10);
        else if(!strcmp(argv[i],"--cap")&&i+1<argc) cap=strtol(argv[++i],NULL,10);
        else if(!strcmp(argv[i],"--signed")) is_signed=1;
    }
    size_t n; int64_t *v=read_ints(in,&n); if(!v) return 1;

    /* (1) CONVERT: text -> compact */
    S2RPool p;
    if (is_signed){ s2r_pool_init(&p,S2R_I8,n?n:1); for(size_t i=0;i<n;i++) s2r_push_signed_adaptive(&p,v[i]); }
    else { for(size_t i=0;i<n;i++) if(v[i]<0){fprintf(stderr,"negative; use --signed\n");free(v);return 1;}
           s2r_pool_init(&p,S2R_8,n?n:1); for(size_t i=0;i<n;i++) s2r_push_adaptive(&p,(uint64_t)v[i]); }
    free(v);
    int cls_in = p.size<0?-p.size:p.size;

    /* (2) PROCESS in place on the compact form, with overflow ceiling --cap */
    int refused=0;
    if (strcmp(op,"none")!=0 && n>0) {
        /* project the largest |value| post-op to check the ceiling before applying */
        uint64_t mx = p.size<0 ? (uint64_t)( s2r_max_signed_val(&p)>0? s2r_max_signed_val(&p):0 ) : s2r_max(&p);
        long double proj = (!strcmp(op,"add")) ? (long double)mx + by : (long double)mx * by;
        long double ceiling = (cap>=64)? 1.8446744073709552e19L : 4294967295.0L;
        if (proj < 0) proj = -proj;
        if (proj > ceiling) { refused=1; }
        else {
            if(!strcmp(op,"add")){ if(is_signed) s2r_add_scalar_signed_safe(&p,by); else s2r_add_scalar_safe(&p,(uint64_t)by); }
            else if(!strcmp(op,"mul")){ if(is_signed) s2r_mul_scalar_signed_safe(&p,by); else s2r_mul_scalar_safe(&p,(uint64_t)by); }
        }
    }
    int cls_out = p.size<0?-p.size:p.size;

    if (refused) {
        fprintf(stderr,"REFUSED: the operation would exceed the %ld-bit ceiling (no truncation).\n", cap);
        s2r_pool_free(&p); return 7;
    }

    /* (3) DECONVERT: compact -> text */
    FILE *fo = fopen(out,"w"); if(!fo){ perror("output"); s2r_pool_free(&p); return 1; }
    for(size_t i=0;i<p.count;i++){
        if(p.size<0) fprintf(fo,"%" PRId64 "\n", s2r_get_signed(&p,i));
        else fprintf(fo,"%" PRIu64 "\n", s2r_get(&p,i));
    }
    fclose(fo);
    fprintf(stderr,"1-core cycle: converted (%d bits) -> processed [op=%s by=%ld cap=%ld] -> deconverted (%d bits) -> %s\n",
            cls_in, op, by, cap, cls_out, out);
    s2r_pool_free(&p);
    return 0;
}
