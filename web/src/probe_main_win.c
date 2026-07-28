/*
 * probe_main_win.c - s2r-probe.exe
 *
 * A mesma sonda da página, como um executável de console para Windows que não
 * depende de NADA: sem CRT, sem DLL de runtime, sem instalador. Só kernel32,
 * que todo Windows tem. Aponte para um arquivo com uma coluna de inteiros e ele
 * responde o que o Smart2Raw faria com ela - e confere cada resposta contra um
 * laço ingênuo antes de imprimir.
 *
 * Smart2Raw
 * Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdint.h>
#include <stddef.h>

/* ---- o pouco de Windows que isto usa, declarado à mão --------------------
 * S2R_HOSTED troca as importações do kernel32 por implementações POSIX, para
 * que ESTE MESMO arquivo possa ser compilado e EXECUTADO no Linux durante a
 * verificação. O que fica sem execução automática é só a carga do PE e as
 * chamadas ao kernel32 - a lógica toda é a mesma e é testada. */
#ifdef S2R_HOSTED
#  define WINAPI
#  define WINIMP
#else
#  define WINAPI __stdcall
#  define WINIMP __declspec(dllimport)
#endif
typedef void*          HANDLE;
typedef unsigned long  DWORD;
typedef int            BOOL;

WINIMP char*  WINAPI GetCommandLineA(void);
WINIMP HANDLE WINAPI GetStdHandle(DWORD);
WINIMP BOOL   WINAPI WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
WINIMP BOOL   WINAPI ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
WINIMP HANDLE WINAPI CreateFileA(const char*, DWORD, DWORD, void*,
                                 DWORD, DWORD, HANDLE);
WINIMP BOOL   WINAPI CloseHandle(HANDLE);
WINIMP BOOL   WINAPI GetFileSizeEx(HANDLE, int64_t*);
WINIMP void   WINAPI ExitProcess(unsigned);
WINIMP BOOL   WINAPI QueryPerformanceCounter(int64_t*);
WINIMP BOOL   WINAPI QueryPerformanceFrequency(int64_t*);
WINIMP BOOL   WINAPI SetConsoleOutputCP(unsigned);

#define STD_OUT ((DWORD)-11)
#define GENERIC_READ  0x80000000u
#define GENERIC_WRITE 0x40000000u
#define OPEN_EXISTING 3u
#define CREATE_ALWAYS 2u
#define FILE_SHARE_READ 1u
#define INVALID_HANDLE ((HANDLE)(size_t)-1)

/* ---- a sonda -------------------------------------------------------------- */
#ifndef S2R_HOSTED
void     *malloc(size_t);
void      free(void*);
#else
#include <stdlib.h>
#endif
uint64_t *s2r_probe_input(uint32_t n);
int       s2r_probe_run(uint32_t n, int is_signed);
uint64_t *s2r_probe_report(void);
uint32_t  s2r_probe_count_gt_naive(uint64_t t);
uint32_t  s2r_probe_count_gt_s2r(uint64_t t);
uint32_t  s2r_probe_count_gt_flat(uint64_t t);
uint32_t  s2r_probe_count_gt_blocked(uint64_t t);
uint32_t  s2r_probe_count_range_naive(uint64_t lo, uint64_t hi);
uint32_t  s2r_probe_count_range_index(uint64_t lo, uint64_t hi);
unsigned char *s2r_probe_file(void);
uint32_t  s2r_probe_file_len(void);
const char *s2r_probe_version(void);

enum { R_N=0, R_SIGNED, R_CLS, R_ELEM_BITS, R_MIN, R_MAX, R_SUM, R_DISTINCT, R_RUNS,
       R_RAW, R_FLAT, R_AFFINE, R_AF_BASE, R_AF_STRIDE, R_CONST,
       R_BLOCKED, R_BLOCK, R_NBLOCKS, R_NCONST, R_HAS_STRIDE,
       R_BEST, R_BEST_BYTES, R_IDX_OK, R_IDX_BYTES, R_SORTED, R_SUMMARY_OK,
       R_DICT, R_DICT_K, R_RLE, R_RLE_RUNS, R_BITMAP, R_BITMAP_OK,
       R_FILE_BYTES, R_FILE_FMT, R_ROUNDTRIP, R_CRC_OK,
       R_NEVER_EXPANDS, R_VERIFIED, R_ERR, R_NPLAN, R_PLAN_BLK,
       R_PLAN_BYTES = R_PLAN_BLK + 12 };
#define NA 0xFFFFFFFFu

/* ---- saída ---------------------------------------------------------------- */
static HANDLE hout;
static char   obuf[8192];
static int    olen;

static void flush_(void){
    DWORD w;
    if(olen){ WriteFile(hout, obuf, (DWORD)olen, &w, 0); olen = 0; }
}
static void putc_(char c){ if(olen >= (int)sizeof obuf) flush_(); obuf[olen++] = c; }
static void s_(const char *p){ while(*p) putc_(*p++); }
static void nl(void){ s_("\r\n"); }
static void rep_(char c, int n){ while(n-- > 0) putc_(c); }

/* inteiro com separador de milhar, à moda daqui */
static int fmt_u64(char *dst, uint64_t v, int group){
    char t[24];
    int k = 0, i = 0, g = 0;
    if(!v) t[k++] = '0';
    while(v){ t[k++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    for(int j = k - 1; j >= 0; j--){
        dst[i++] = t[j];
        g++;
        if(group && j && (j % 3) == 0) dst[i++] = '.';
        (void)g;
    }
    dst[i] = 0;
    return i;
}
static void u_(uint64_t v){ char b[32]; fmt_u64(b, v, 1); s_(b); }
static void i_(int64_t v){ if(v < 0){ putc_('-'); v = -v; } u_((uint64_t)v); }
static void upad(uint64_t v, int w){ char b[32]; int n = fmt_u64(b, v, 1); rep_(' ', w - n); s_(b); }
static void spad(const char *p, int w){ int n = 0; const char *q = p; while(*q++) n++; s_(p); rep_(' ', w - n); }
/* x.xx sem ponto flutuante na saída */
static void ratio(uint64_t a, uint64_t b, int w){
    char t[32];
    uint64_t r = b ? (a * 100u + b/2u) / b : 0;
    int n = fmt_u64(t, r / 100u, 1);
    rep_(' ', w - n - 3);
    s_(t);
    putc_(',');
    putc_((char)('0' + (int)((r / 10u) % 10u)));
    putc_((char)('0' + (int)(r % 10u)));
}
/* milissegundos com 4 casas, a partir de ticks */
static void ms4(int64_t ticks, int64_t freq, uint64_t reps, int w){
    char t[32];
    uint64_t us10 = freq && reps ? (uint64_t)((ticks * 10000000.0) / (double)freq / (double)reps) : 0;
    int n = fmt_u64(t, us10 / 10000u, 0);
    rep_(' ', w - n - 5);
    s_(t); putc_(',');
    for(int d = 3; d >= 0; d--){
        uint64_t p = 1; for(int k = 0; k < d; k++) p *= 10u;
        putc_((char)('0' + (int)((us10 / p) % 10u)));
    }
}

/* ---- leitura do arquivo --------------------------------------------------- */
static char *slurp(const char *path, size_t *len){
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    int64_t sz = 0;
    char *buf;
    DWORD got = 0, done = 0;
    if(h == INVALID_HANDLE) return 0;
    if(!GetFileSizeEx(h, &sz) || sz <= 0 || sz > (int64_t)1 << 31){ CloseHandle(h); return 0; }
    buf = (char*)malloc((size_t)sz + 1u);
    if(!buf){ CloseHandle(h); return 0; }
    while(done < (DWORD)sz){
        if(!ReadFile(h, buf + done, (DWORD)sz - done, &got, 0) || !got) break;
        done += got;
    }
    CloseHandle(h);
    buf[done] = 0;
    *len = done;
    return buf;
}
static int write_file(const char *path, const unsigned char *p, uint32_t n){
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    DWORD w = 0;
    int ok;
    if(h == INVALID_HANDLE) return 0;
    ok = WriteFile(h, p, n, &w, 0) && w == n;
    CloseHandle(h);
    return ok;
}

/* ---- linha de comando ----------------------------------------------------- */
static int argc_; static char *argv_[32]; static char cmdbuf[4096];
static void split_cmd(void){
    char *s = GetCommandLineA();
    int i = 0, q = 0, n = 0;
    while(s[n] && n < (int)sizeof cmdbuf - 1){ cmdbuf[n] = s[n]; n++; }
    cmdbuf[n] = 0;
    s = cmdbuf;
    while(*s && argc_ < 32){
        while(*s == ' ' || *s == '\t') s++;
        if(!*s) break;
        if(*s == '"'){ q = 1; s++; }
        argv_[argc_++] = s;
        while(*s && (q ? *s != '"' : (*s != ' ' && *s != '\t'))) s++;
        if(*s){ *s++ = 0; }
        q = 0;
    }
    (void)i;
}
static int streq(const char *a, const char *b){ while(*a && *a == *b){ a++; b++; } return *a == *b; }
static uint64_t parse_u(const char *p){ uint64_t v = 0; while(*p >= '0' && *p <= '9') v = v*10u + (uint64_t)(*p++ - '0'); return v; }

/* ---- o programa ----------------------------------------------------------- */
static void usage(void){
    s_("uso: s2r-probe.exe <arquivo.csv|.txt> [--coluna N] [--limite N] [--salvar saida.s2r]"); nl();
    s_("     o arquivo deve conter inteiros; --coluna escolhe o campo (1 = primeiro)."); nl();
}

int main_(void)
{
    const char *path = 0, *save = 0;
    uint32_t col = 0, limit = 0;
    char *txt; size_t tlen = 0;
    uint64_t *buf; uint32_t n = 0;
    int is_signed = 0;
    int64_t freq = 0;

    hout = GetStdHandle(STD_OUT);
    SetConsoleOutputCP(65001);
    QueryPerformanceFrequency(&freq);
    split_cmd();

    for(int i = 1; i < argc_; i++){
        if(streq(argv_[i], "--coluna") && i+1 < argc_) col = (uint32_t)parse_u(argv_[++i]);
        else if(streq(argv_[i], "--limite") && i+1 < argc_) limit = (uint32_t)parse_u(argv_[++i]);
        else if(streq(argv_[i], "--salvar") && i+1 < argc_) save = argv_[++i];
        else if(argv_[i][0] != '-') path = argv_[i];
    }
    s_("Smart2Raw "); s_(s2r_probe_version()); s_(" - sonda de coluna"); nl();
    s_("Copyright (C) 2026 Carlos Alberto Terencio de Bastos - AGPL-3.0-or-later"); nl(); nl();
    if(!path){ usage(); flush_(); return 2; }

    txt = slurp(path, &tlen);
    if(!txt){ s_("nao consegui abrir: "); s_(path); nl(); flush_(); return 2; }

    /* conta quantos inteiros existem, depois lê de verdade */
    {
        size_t i;
        uint32_t cap = 0, field = 0;
        int innum = 0;
        for(i = 0; i < tlen; i++){
            char c = txt[i];
            int dig = (c >= '0' && c <= '9');
            if(dig && !innum){ if(!col || field + 1u == col) cap++; innum = 1; }
            else if(!dig){
                innum = 0;
                if(c == ',' || c == ';' || c == '\t' || c == '|') field++;
                else if(c == '\n') field = 0;
            }
        }
        if(limit && cap > limit) cap = limit;
        if(!cap){ s_("nenhum inteiro encontrado nessa coluna."); nl(); flush_(); return 2; }
        buf = s2r_probe_input(cap);
        if(!buf){ s_("memoria insuficiente."); nl(); flush_(); return 2; }
        field = 0; innum = 0;
        for(i = 0; i < tlen && n < cap; i++){
            char c = txt[i];
            int dig = (c >= '0' && c <= '9');
            if(dig && !innum){
                innum = 1;
                if(!col || field + 1u == col){
                    int neg = (i && txt[i-1] == '-');
                    uint64_t v = 0;
                    size_t j = i;
                    while(j < tlen && txt[j] >= '0' && txt[j] <= '9') v = v*10u + (uint64_t)(txt[j++] - '0');
                    if(neg){ buf[n++] = (uint64_t)(-(int64_t)v); is_signed = 1; }
                    else      buf[n++] = v;
                }
            } else if(!dig){
                innum = 0;
                if(c == ',' || c == ';' || c == '\t' || c == '|') field++;
                else if(c == '\n') field = 0;
            }
        }
        free(txt);
    }

    s_("arquivo ..... "); s_(path); nl();
    if(col){ s_("coluna ...... "); u_(col); nl(); }
    s_("elementos ... "); u_(n); nl(); nl();

    s2r_probe_run(n, is_signed);
    {
        uint64_t *R = s2r_probe_report();
        int64_t cls = (int64_t)R[R_CLS];
        s_("  amplitude real ....... ");
        if(R[R_SIGNED]) { i_((int64_t)R[R_MIN]); s_(" .. "); i_((int64_t)R[R_MAX]); }
        else            { u_(R[R_MIN]); s_(" .. "); u_(R[R_MAX]); }
        nl();
        s_("  classe escolhida ..... "); s_(cls < 0 ? "int" : "uint"); u_((uint64_t)(cls<0?-cls:cls));
        s_("  ("); u_(R[R_ELEM_BITS]); s_(" bits por elemento)"); nl();
        s_("  valores distintos .... "); u_(R[R_DISTINCT]); nl();
        s_("  ordenada ............. "); s_(R[R_SORTED] ? "sim (busca binaria habilitada)" : "nao"); nl();
        if(R[R_CONST])            s_("  coluna constante: a forma afim nao guarda payload nenhum\r\n");
        else if(R[R_AF_STRIDE]>1){ s_("  passo comum .......... "); u_(R[R_AF_STRIDE]);
                                   s_("   (v = "); i_((int64_t)R[R_AF_BASE]); s_(" + passo*i)"); nl(); }
        s_("  bloco escolhido ...... "); u_(R[R_BLOCK]); s_(" elementos, "); u_(R[R_NBLOCKS]);
        s_(" blocos, "); u_(R[R_NCONST]); s_(" sem payload"); nl();
        nl();

        s_("  representacao                       bytes    x int64"); nl();
        s_("  ------------------------------------------------------"); nl();
        {
            struct { const char *nm; uint64_t by; int mine; } row[7];
            int nr = 0, i;
            row[nr].nm="int64 (linha de base)"; row[nr].by=R[R_RAW];     row[nr].mine=-1; nr++;
            row[nr].nm="Smart2Raw pool plano";  row[nr].by=R[R_FLAT];    row[nr].mine=0; nr++;
            row[nr].nm="Smart2Raw afim";        row[nr].by=R[R_AFFINE];  row[nr].mine=1; nr++;
            row[nr].nm="Smart2Raw em blocos";   row[nr].by=R[R_BLOCKED]; row[nr].mine=2; nr++;
            row[nr].nm="dicionario + bits";     row[nr].by=R[R_DICT];    row[nr].mine=-1; nr++;
            row[nr].nm="RLE";                   row[nr].by=R[R_RLE];     row[nr].mine=-1; nr++;
            if(R[R_BITMAP_OK]){ row[nr].nm="bitmap"; row[nr].by=R[R_BITMAP]; row[nr].mine=-1; nr++; }
            for(i = 0; i < nr; i++){
                const char *mark = "";
                if(row[i].mine >= 0 && (uint64_t)row[i].mine == R[R_BEST]) mark = "  <- recomendado";
                s_("  "); spad(row[i].nm, 26); upad(row[i].by, 14); s_("   ");
                ratio(row[i].by, R[R_RAW], 6);
                if(row[i].by > R[R_RAW]) s_("   EXPANDE");
                s_(mark); nl();
            }
            if(!R[R_BITMAP_OK]){ s_("  "); spad("bitmap", 26); s_("   nao se aplica: "); u_(R[R_DISTINCT]);
                                 s_(" valores distintos"); nl(); }
        }
        nl();

        /* consulta cronometrada, com o resultado conferido */
        {
            uint64_t thr = R[R_SIGNED] ? (uint64_t)(((int64_t)R[R_MIN])/2 + ((int64_t)R[R_MAX])/2)
                                       : (R[R_MIN]/2u + R[R_MAX]/2u);
            uint64_t lo, hi, q; int64_t t0, t1; uint64_t reps = 0, sink = 0;
            uint32_t c_nai, c_s2r;
            if(R[R_SIGNED]){
                int64_t mn = (int64_t)R[R_MIN], mx = (int64_t)R[R_MAX];
                int64_t sp = (mx - mn) / 4;
                lo = (uint64_t)(mn + sp); hi = (uint64_t)(mx - sp);
            } else {
                uint64_t sp = (R[R_MAX] - R[R_MIN]) / 4u;
                lo = R[R_MIN] + sp; hi = R[R_MAX] - sp;
            }

            s_("  consulta: quantos valores > ");
            if(R[R_SIGNED]) i_((int64_t)thr); else u_(thr); nl();
            c_nai = s2r_probe_count_gt_naive(thr);
            c_s2r = s2r_probe_count_gt_s2r(thr);
            reps = n > 2000000u ? 5u : n > 200000u ? 30u : 300u;

            QueryPerformanceCounter(&t0);
            for(q = 0; q < reps; q++) sink += s2r_probe_count_gt_naive(thr + (q & 7u));
            QueryPerformanceCounter(&t1);
            s_("    varredura ingenua .... "); ms4(t1-t0, freq, reps, 10); s_(" ms"); nl();
            {   int64_t naive = t1 - t0;
                QueryPerformanceCounter(&t0);
                for(q = 0; q < reps; q++) sink += s2r_probe_count_gt_s2r(thr + (q & 7u));
                QueryPerformanceCounter(&t1);
                s_("    Smart2Raw ............ "); ms4(t1-t0, freq, reps, 10); s_(" ms   ");
                ratio((uint64_t)naive, (uint64_t)(t1-t0 ? t1-t0 : 1), 6); s_("x"); nl();
            }
            s_("    resultados ........... ");
            if(c_nai == c_s2r){ s_("iguais ("); u_(c_nai); s_(")"); }
            else { s_("DIVERGIRAM: "); u_(c_nai); s_(" != "); u_(c_s2r); }
            nl();

            if(s2r_probe_count_range_index(lo, hi) != NA){
                uint32_t a = s2r_probe_count_range_naive(lo, hi);
                uint32_t b = s2r_probe_count_range_index(lo, hi);
                int64_t naive;
                s_("  consulta: quantos na faixa ");
                if(R[R_SIGNED]){ i_((int64_t)lo); s_(" .. "); i_((int64_t)hi); }
                else           { u_(lo); s_(" .. "); u_(hi); }
                nl();
                QueryPerformanceCounter(&t0);
                for(q = 0; q < reps; q++) sink += s2r_probe_count_range_naive(lo, hi + (q & 7u));
                QueryPerformanceCounter(&t1);
                naive = t1 - t0;
                s_("    varredura ingenua .... "); ms4(naive, freq, reps, 10); s_(" ms"); nl();
                QueryPerformanceCounter(&t0);
                for(q = 0; q < reps*100u; q++) sink += s2r_probe_count_range_index(lo, hi + (q & 7u));
                QueryPerformanceCounter(&t1);
                s_("    indice cumulativo .... "); ms4(t1-t0, freq, reps*100u, 10); s_(" ms   ");
                ratio((uint64_t)(naive*100), (uint64_t)(t1-t0 ? t1-t0 : 1), 6); s_("x");
                s_("   ("); u_(R[R_IDX_BYTES]); s_(" bytes que nao crescem com o dado)"); nl();
                s_("    resultados ........... ");
                if(a == b){ s_("iguais ("); u_(a); s_(")"); } else { s_("DIVERGIRAM"); }
                nl();
            }
            if(sink == 0xFFFFFFFFFFFFFFFFull) s_("");
        }
        nl();

        s_("  verificacoes"); nl();
        s_("    cada valor conferido contra o original .... ");
        s_(R[R_VERIFIED] ? "OK" : "FALHOU"); nl();
        s_("    nunca excedeu a linha de base int64 ....... ");
        s_(R[R_NEVER_EXPANDS] ? "OK" : "FALHOU"); nl();
        s_("    .s2r salvo, relido, CRC e valores ......... ");
        if(R[R_ROUNDTRIP]){ s_("OK  (fmt "); u_(R[R_FILE_FMT]); s_(", "); u_(R[R_FILE_BYTES]); s_(" bytes)"); }
        else s_("nao gerado");
        nl();

        if(save && s2r_probe_file_len()){
            nl();
            if(write_file(save, s2r_probe_file(), s2r_probe_file_len())){
                s_("  gravado: "); s_(save); s_("  ("); u_(s2r_probe_file_len()); s_(" bytes)"); nl();
            } else { s_("  nao consegui gravar "); s_(save); nl(); }
        }
        flush_();
        return R[R_VERIFIED] && R[R_NEVER_EXPANDS] ? 0 : 1;
    }
}

#ifndef S2R_HOSTED
void mainCRTStartup(void){ ExitProcess((unsigned)main_()); }
#endif
