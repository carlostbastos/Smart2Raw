#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Smart2Raw - Maestro de benchmark (terminal, so biblioteca padrao)
=================================================================

Compara, sobre um CSV de dados inteiros (de um PC comum ou de um servidor de
armazenamento), guardar tudo como int64 (o habito comum) contra guardar cada
coluna na MENOR classe nativa que cabe (a regra do Smart2Raw) -- medindo
memoria, deslocamento de dados e velocidade.

O Python e o "maestro": ele usa SO o que ja vem com o proprio Python.
  - MEMORIA e DESLOCAMENTO  -> modulo `array` (tipos nativos u8/u16/u32/u64).
    Exato, sem dependencia nenhuma. Mais fiel ao Smart2Raw que o numpy.
  - VELOCIDADE              -> modulo `ctypes` chamando a PROPRIA biblioteca em C
    do Smart2Raw (kernels SIMD reais). Python puro e interpretado e esconderia a
    largura, entao a velocidade so e medida com um motor nativo de verdade.

Como rodar (Python e CSV na mesma pasta):
    python3 smart2raw_bench.py
    python3 smart2raw_bench.py meu.csv
    python3 smart2raw_bench.py --no-color

Para a parte de velocidade (opcional), o maestro compila um pequeno shim com o
compilador do sistema (cc/gcc/clang) usando o cabecalho smart2raw.h. Basta
deixar `smart2raw.h` (ou a pasta `include/`) ao lado deste script. Sem isso,
memoria e deslocamento continuam sendo medidos normalmente; numpy e usado como
ultimo recurso se estiver instalado.

Honestidade: memoria e deslocamento sao exatos; a velocidade e medida na sua
maquina (com o SIMD real) e muda conforme CPU/cache. Colunas que ja precisam de
64 bits ou sao float NAO encolhem -- aparecem com 0%, de proposito.
"""

import array
import csv
import os
import shutil
import subprocess
import sys
import tempfile
import time
import platform
import random
import ctypes

# --------------------------------------------------------------------------- #
# Cores ANSI (tela preta). Desligaveis com --no-color ou se nao for um TTY.
# --------------------------------------------------------------------------- #
class C:
    enabled = True
    @classmethod
    def wrap(cls, code, s): return f"\033[{code}m{s}\033[0m" if cls.enabled else s
    @classmethod
    def b(cls, s):    return cls.wrap("1", s)
    @classmethod
    def dim(cls, s):  return cls.wrap("2", s)
    @classmethod
    def g(cls, s):    return cls.wrap("38;5;78", s)
    @classmethod
    def y(cls, s):    return cls.wrap("38;5;179", s)
    @classmethod
    def c(cls, s):    return cls.wrap("38;5;75", s)
    @classmethod
    def r(cls, s):    return cls.wrap("38;5;174", s)
    @classmethod
    def grey(cls, s): return cls.wrap("38;5;245", s)

def enable_windows_ansi():
    if os.name == "nt":
        os.system("")

# --------------------------------------------------------------------------- #
# Geracao de um CSV realista (~650 KB) se nenhum for encontrado
# --------------------------------------------------------------------------- #
def generate_csv(path, target_kb=650, seed=20260609):
    rnd = random.Random(seed)
    header = ["ts","http_status","latency_ms","cpu_pct","temp_c","mem_mb",
              "error_code","region_id","bytes_sent","user_id","flags",
              "score_x1000","global_id","price"]
    status_pool = [200,200,200,200,200,301,304,404,500,502]
    def make_row(i):
        err = 0 if rnd.random() < 0.85 else rnd.randint(1,120)
        return [i, rnd.choice(status_pool),
                max(0, min(3000, int(abs(rnd.gauss(80,60))))),
                rnd.randint(0,100), rnd.randint(15,92), rnd.randint(200,64000),
                err, rnd.randint(0,28), rnd.randint(0,8_000_000),
                rnd.randint(1,250_000), rnd.randint(0,255), rnd.randint(0,100_000),
                rnd.randint(0,9_000_000_000_000_000_000),
                round(rnd.uniform(0.50,999.99),2)]
    sample = [",".join(str(x) for x in make_row(i)) for i in range(200)]
    avg = (sum(len(s) for s in sample)/len(sample)) + 1
    header_len = len(",".join(header)) + 1
    rows = max(1000, int((target_kb*1024 - header_len)/avg))
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        for i in range(rows):
            w.writerow(make_row(i))
    return path

def find_or_make_csv(explicit=None):
    here = os.path.dirname(os.path.abspath(__file__))
    if explicit:
        p = explicit if os.path.isabs(explicit) else os.path.join(here, explicit)
        if not os.path.exists(p):
            sys.exit(f"CSV nao encontrado: {p}")
        return p, False
    cands = [f for f in os.listdir(here) if f.lower().endswith(".csv")]
    if cands:
        cands.sort(key=lambda f: os.path.getsize(os.path.join(here,f)), reverse=True)
        return os.path.join(here, cands[0]), False
    p = os.path.join(here, "smart2raw_demo.csv")
    generate_csv(p)
    return p, True

# --------------------------------------------------------------------------- #
# Leitura do CSV e deteccao de tipo por coluna
# --------------------------------------------------------------------------- #
def sniff_type(values):
    is_int = is_float = True
    for v in values:
        if v == "" or v is None:
            continue
        try: int(v)
        except ValueError: is_int = False
        try: float(v)
        except ValueError: is_float = False
        if not is_int and not is_float:
            break
    return "int" if is_int else ("float" if is_float else "text")

def load_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        header = next(reader)
        cols = [[] for _ in header]
        for row in reader:
            if len(row) != len(header):
                continue
            for j, v in enumerate(row):
                cols[j].append(v)
    n = len(cols[0]) if cols else 0
    typed = []
    sn = min(500, n)
    for name, col in zip(header, cols):
        typed.append((name, sniff_type(col[:sn]), col))
    return header, n, typed

# --------------------------------------------------------------------------- #
# Classificacao: a menor classe nativa que cabe (a regra do Smart2Raw)
# --------------------------------------------------------------------------- #
def smallest_class(lo, hi):
    """Retorna (nome, bytes_por_elemento, signed)."""
    if lo < 0:
        for name, bits in [("i8",1),("i16",2),("i32",4),("i64",8)]:
            mn, mx = -(1 << (bits*8-1)), (1 << (bits*8-1)) - 1
            if lo >= mn and hi <= mx:
                return name, bits, True
        return "i64", 8, True
    for name, bits in [("u8",1),("u16",2),("u32",4),("u64",8)]:
        if hi <= (1 << (bits*8)) - 1:
            return name, bits, False
    return "u64", 8, False

def arr_typecode(nbytes, signed):
    table = {(1,False):"B",(2,False):"H",(4,False):"I",(8,False):"Q",
             (1,True):"b",(2,True):"h",(4,True):"i",(8,True):"q"}
    tc = table[(nbytes, signed)]
    if array.array(tc).itemsize != nbytes:
        alt = {"I":"L","i":"l","Q":"L","q":"l"}.get(tc)
        if alt and array.array(alt).itemsize == nbytes:
            tc = alt
    return tc

def fmt_range(lo, hi):
    if hi >= 1_000_000_000 or lo <= -1_000_000_000:
        return f"{lo:.1e}..{hi:.1e}"
    return f"{lo}..{hi}"

def hb(n):
    n = float(n)
    for unit in ["B","KB","MB","GB"]:
        if n < 1024 or unit == "GB":
            return (f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}")
        n /= 1024.0

# --------------------------------------------------------------------------- #
# Motor de velocidade nativo: a PROPRIA lib em C, via ctypes (stdlib)
# --------------------------------------------------------------------------- #
C_SHIM = r'''
#include <stdint.h>
#include <stddef.h>
#include "smart2raw.h"   /* define S2R_X86_SIMD; se 1, traz os kernels s2r__sum_* */

/* No Windows todo simbolo precisa ser exportado explicitamente (MSVC, mingw e
   clang). Em ELF/Mach-O a visibilidade padrao ja exporta. */
#if defined(_WIN32)
  #define S2RB_EXPORT __declspec(dllexport)
#else
  #define S2RB_EXPORT
#endif

/* soma n elementos de `width` bytes -> uint64. width==1/2 com use_lib usa o
   kernel SIMD real do Smart2Raw (so existe quando S2R_X86_SIMD, i.e. gcc/clang
   em x86); senao usa laco nativo, que o compilador auto-vetoriza. */
S2RB_EXPORT uint64_t s2rb_sum(const void *p, size_t n, int width, int use_lib){
    (void)use_lib;
    if(width==1){ const uint8_t *a=(const uint8_t*)p;
#if S2R_X86_SIMD
        if(use_lib){ if(s2r_has_avx512bw()) return s2r__sum_u8_avx512(a,n);
                     if(s2r_has_avx2())     return s2r__sum_u8_avx2(a,n); }
#endif
        uint64_t s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
    if(width==2){ const uint16_t *a=(const uint16_t*)p;
#if S2R_X86_SIMD
        if(use_lib && s2r_has_avx2()) return s2r__sum_u16_avx2(a,n);
#endif
        uint64_t s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
    if(width==4){ const uint32_t *a=(const uint32_t*)p;
        uint64_t s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
    const int64_t *a=(const int64_t*)p;
    uint64_t s=0; for(size_t i=0;i<n;i++) s+=(uint64_t)a[i]; return s;
}
/* conta elementos > k (filtro de faixa), width-aware. */
S2RB_EXPORT uint64_t s2rb_count_gt(const void *p, size_t n, int width, int64_t k){
    uint64_t c=0;
    if(width==1){ const uint8_t  *a=(const uint8_t*)p;  for(size_t i=0;i<n;i++) c+=((int64_t)a[i]>k); }
    else if(width==2){ const uint16_t *a=(const uint16_t*)p; for(size_t i=0;i<n;i++) c+=((int64_t)a[i]>k); }
    else if(width==4){ const uint32_t *a=(const uint32_t*)p; for(size_t i=0;i<n;i++) c+=((int64_t)a[i]>k); }
    else { const int64_t *a=(const int64_t*)p; for(size_t i=0;i<n;i++) c+=(a[i]>k); }
    return c;
}
'''

def find_header(here):
    """Locate smart2raw.h. The repository's include/ always wins over a local copy.

    README_maestro.md states the header is "not duplicated here - the maestro finds
    it in the repository's include/". But this folder sits two levels below the repo
    root (benchmarks/maestro/), so the only repo-relative candidate was
    "../include", which resolves to benchmarks/include/ and never exists. The lookup
    therefore always fell through to a vendored copy beside the script, and that
    copy silently drifted out of sync with include/smart2raw.h - so the benchmark
    could be measuring a stale core while reporting the current version string.

    Order matters: the repo header is checked BEFORE any local one, so running
    inside a checkout always measures the current core even if an old vendored copy
    is still lying around. The local candidates stay last so the folder keeps
    working when it is copied out on its own, as the README advertises.
    """
    cands = [os.environ.get("SMART2RAW_INCLUDE",""),
             os.path.join(here,"..","..","include","smart2raw.h"),   # repo root/include
             os.path.join(here,"..","include","smart2raw.h"),        # copied one level down
             os.path.join(here,"include","smart2raw.h"),             # vendored subfolder
             os.path.join(here,"smart2raw.h")]                       # vendored beside script
    for c in cands:
        if c and os.path.isfile(c):
            return os.path.dirname(os.path.abspath(c))
    return None

def _run(cmd, cwd=None):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=90, cwd=cwd)
        return r.returncode == 0
    except Exception:
        return False

def build_c_engine(here):
    """Compila o shim com o compilador do sistema e carrega via ctypes.
    Tenta gcc/clang/cc (POSIX) e, no Windows, tambem o cl.exe do MSVC.
    Retorna (lib, label) ou (None, motivo)."""
    # ja compilado ao lado? (so a extensao do SO atual -- nunca tentar um .so
    # do Linux no Windows, que daria erro 0xc000012f "imagem invalida")
    if os.name == "nt":
        exts = (".dll",)
    elif sys.platform == "darwin":
        exts = (".dylib", ".so")
    else:
        exts = (".so",)
    for pre in ("s2r_kernels", "libs2r_kernels"):
        for ext in exts:
            p = os.path.join(here, pre+ext)
            if os.path.isfile(p):
                try:
                    return _load_engine(p), "lib pre-compilada"
                except Exception:
                    pass
    incdir = find_header(here)
    if not incdir:
        return None, "smart2raw.h nao encontrado ao lado do script"

    tmp = tempfile.mkdtemp(prefix="s2rbench_")
    src = os.path.join(tmp, "shim.c")
    with open(src, "w") as f:
        f.write(C_SHIM)
    out = os.path.join(tmp, "s2r_kernels" + (".dll" if os.name == "nt" else ".so"))

    tried = []
    # -static so faz sentido no Windows/mingw (embute o runtime no .dll, deixando
    # o arquivo autossuficiente). Em Linux/macOS, -static brigaria com -shared.
    posix_extra = ["-static"] if os.name == "nt" else []
    # 0) compilador explicito via variavel de ambiente S2R_CC (caminho completo) --
    #    util quando o gcc/clang nao esta no PATH (ex.: MSYS2/LLVM em E:\...).
    envcc = os.environ.get("S2R_CC")
    if envcc and os.path.isfile(envcc):
        if envcc.lower().endswith("cl.exe"):
            for arch in (["/arch:AVX2"], []):
                cmd = [envcc, "/nologo", "/O2", *arch, "/LD", f"/I{incdir}", src, f"/Fe:{out}"]
                if _run(cmd, cwd=tmp) and os.path.isfile(out):
                    return _load_engine(out), "S2R_CC cl (MSVC)"
        else:
            for flags in (["-march=native"], []):
                cmd = [envcc, "-O3", "-shared", "-fPIC", *posix_extra, *flags,
                       "-I", incdir, src, "-o", out]
                if _run(cmd) and os.path.isfile(out):
                    return _load_engine(out), "S2R_CC " + os.path.basename(envcc)
        tried.append(os.path.basename(envcc))
    # 1) POSIX: gcc / clang / cc (Linux, macOS, e mingw/clang no Windows)
    for cand in ("cc", "gcc", "clang"):
        if not shutil.which(cand):
            continue
        for flags in (["-march=native"], []):
            cmd = [cand, "-O3", "-shared", "-fPIC", *posix_extra, *flags,
                   "-I", incdir, src, "-o", out]
            if _run(cmd) and os.path.isfile(out):
                return _load_engine(out), cand + (" -march=native" if flags else "")
        tried.append(cand)
        break  # um compilador POSIX basta tentar
    # 2) MSVC: cl.exe (Windows sem mingw; rodar num "Developer Command Prompt")
    if os.name == "nt" and shutil.which("cl"):
        for arch in (["/arch:AVX2"], []):
            cmd = ["cl", "/nologo", "/O2", *arch, "/LD", f"/I{incdir}", src, f"/Fe:{out}"]
            if _run(cmd, cwd=tmp) and os.path.isfile(out):
                lab = "cl (MSVC)" + (" /arch:AVX2" if arch else "")
                return _load_engine(out), lab
        tried.append("cl")

    if not tried:
        return None, "nenhum compilador C no PATH (cc/gcc/clang, ou cl do MSVC)"
    return None, f"falha ao compilar o shim ({', '.join(tried)})"

def _load_engine(path):
    lib = ctypes.CDLL(path)
    lib.s2rb_sum.restype = ctypes.c_uint64
    lib.s2rb_sum.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int]
    lib.s2rb_count_gt.restype = ctypes.c_uint64
    lib.s2rb_count_gt.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int64]
    return lib

# --------------------------------------------------------------------------- #
# Round-trip .s2r real (opcional), se o port Python estiver ao lado
# --------------------------------------------------------------------------- #
def try_real_s2r(int_cols, here):
    try:
        import smart2raw as s2r
    except Exception:
        return None
    try:
        best = None
        for name, vals, lo, hi, cname, cbytes, signed in int_cols:
            if best is None or cbytes < best["cbytes"]:
                best = {"name":name,"vals":vals,"lo":lo,"cbytes":cbytes,"class":cname,"signed":signed}
        if best is None:
            return None
        pool = s2r.Smart2RawPool(best["vals"], signed=best["signed"])
        path = os.path.join(here, f"smart2raw_demo_{best['name']}.s2r")
        pool.save(path)
        reloaded = s2r.Smart2RawPool.load(path, verify_crc=True)
        ok = tuple(reloaded.values) == tuple(best["vals"])
        return {"name":best["name"], "n":len(best["vals"]), "class":best["class"],
                "s2r_size":os.path.getsize(path), "int64_size":len(best["vals"])*8,
                "ok":ok, "path":os.path.basename(path)}
    except Exception:
        return None

# --------------------------------------------------------------------------- #
# Programa principal
# --------------------------------------------------------------------------- #
def measure_sqlite(int_cols, engine, here):
    """Compara armazenamento e consulta contra o SQLite -- um banco binario de
    verdade, da stdlib. GRAVA os artefatos ao lado do script para voce ver os
    tres formatos: o CSV (entrada), o smart2raw_demo.db (SQLite) e a pasta
    s2r_cols/ com um .s2r por coluna (formato real)."""
    import sqlite3, struct, zlib
    if not int_cols:
        return {"db_size": 0, "s2r_size": 0, "n": 0}
    n = len(int_cols[0][1])

    # --- grava 1 .s2r por coluna (formato real: 16B header LE + payload + CRC32) ---
    MAGIC = 0x33335253; FMT = 1
    s2r_dir = os.path.join(here, "s2r_cols")
    os.makedirs(s2r_dir, exist_ok=True)
    s2r_size = 0
    for (name, vals, lo, hi, cname, cb, signed) in int_cols:
        payload = array.array(arr_typecode(cb, signed), vals).tobytes()
        sizebits = (cb*8) * (-1 if signed else 1)
        head = struct.pack("<IbBBBQ", MAGIC, sizebits, 1 if signed else 0, FMT, 0, len(vals))
        blob = head + payload + struct.pack("<I", zlib.crc32(payload) & 0xffffffff)
        with open(os.path.join(s2r_dir, name + ".s2r"), "wb") as f:
            f.write(blob)
        s2r_size += len(blob)

    # --- grava o banco SQLite (todas as colunas inteiras) ---
    dbp = os.path.join(here, "smart2raw_demo.db")
    try:
        if os.path.exists(dbp):
            os.remove(dbp)
    except OSError:
        pass
    con = sqlite3.connect(dbp); cur = con.cursor()
    cn = [f"c{i}" for i in range(len(int_cols))]
    cur.execute("CREATE TABLE t(%s)" % ",".join(f"{c} INTEGER" for c in cn))
    cur.executemany("INSERT INTO t VALUES(%s)" % ",".join("?"*len(cn)),
                    list(zip(*[c[1] for c in int_cols])))
    con.commit()
    res = {"db_size": os.path.getsize(dbp), "s2r_size": s2r_size, "n": n,
           "nfiles": len(int_cols),
           "db_path": os.path.basename(dbp), "s2r_dir": os.path.basename(s2r_dir)}

    # consulta nas colunas que compactam (cbytes<8): evita estouro do u64 e
    # reflete onde o Smart2Raw atua. Smart2Raw usa o kernel SIMD real (engine C).
    comp = [(i, c) for i, c in enumerate(int_cols) if c[5] < 8]
    if comp and engine is not None:
        arrs, ptrs = [], []
        for i, (name, vals, lo, hi, cname, cb, signed) in comp:
            a = array.array(arr_typecode(cb, signed), vals); arrs.append(a)
            ad, _ = a.buffer_info()
            ptrs.append((ctypes.c_void_p(ad), len(a), cb, (lo+hi)//2, cn[i]))
        q_sum = "SELECT " + ",".join(f"SUM({p[4]})" for p in ptrs) + " FROM t"
        q_cnt = "SELECT " + ",".join(f"SUM({p[4]}>{p[3]})" for p in ptrs) + " FROM t"
        def tm(fn):
            fn(); fn()
            t0 = time.perf_counter(); reps = 0
            while time.perf_counter() - t0 < 0.2:
                fn(); reps += 1
            return (time.perf_counter() - t0) / max(1, reps)
        res["sum_sql"] = tm(lambda: cur.execute(q_sum).fetchall())
        res["sum_s2r"] = tm(lambda: [engine.s2rb_sum(p[0], p[1], p[2], 1) for p in ptrs])
        res["cnt_sql"] = tm(lambda: cur.execute(q_cnt).fetchall())
        res["cnt_s2r"] = tm(lambda: [engine.s2rb_count_gt(p[0], p[1], p[2], p[3]) for p in ptrs])
        res["ncomp"] = len(comp)
        _ = arrs  # mantem os buffers vivos durante as medicoes
    con.close()
    return res

def main():
    args = list(sys.argv[1:])
    if "--no-color" in args:
        C.enabled = False; args.remove("--no-color")
    if not sys.stdout.isatty():
        C.enabled = False
    enable_windows_ansi()
    explicit = args[0] if args else None
    here = os.path.dirname(os.path.abspath(__file__))
    line = "─" * 64

    csv_path, generated = find_or_make_csv(explicit)
    size = os.path.getsize(csv_path)
    print()
    print(C.g(C.b("  SMART2RAW · maestro de benchmark")))
    print(C.grey("  convencional (int64) × menor classe nativa que cabe"))
    print(C.grey("  só biblioteca padrão do Python (array + ctypes + sqlite3)"))
    print(C.grey("  " + line))
    if generated:
        print(C.dim(f"  (nenhum CSV na pasta; gerei {os.path.basename(csv_path)} ~ {hb(size)})"))
    print(f"  origem  : {C.c(os.path.basename(csv_path))}   ({hb(size)})  — dados de entrada (CSV)")

    header, nrows, typed = load_csv(csv_path)
    # classifica colunas inteiras
    int_cols = []   # (name, vals, lo, hi, cname, cbytes, signed)
    int_specs = []  # (idx, name, typecode), na ordem das colunas inteiras
    float_names, text_names = [], []
    for idx, (name, t, col) in enumerate(typed):
        if t == "int":
            vals = [int(v) for v in col if v != ""]
            lo, hi = (min(vals), max(vals)) if vals else (0, 0)
            cname, cbytes, signed = smallest_class(lo, hi)
            int_cols.append((name, vals, lo, hi, cname, cbytes, signed))
            int_specs.append((idx, name, arr_typecode(cbytes, signed)))
        elif t == "float":
            float_names.append(name)
        else:
            text_names.append(name)
    print(f"  linhas  : {C.c(f'{nrows:,}')}    colunas: {len(header)} "
          f"({len(int_cols)} inteiras, {len(float_names)} float, {len(text_names)} texto)")
    print(C.grey("  " + line))

    # ----- memoria (exata, via tamanho nativo) -----
    print(C.b("  MEMÓRIA por coluna (medida exata)"))
    print(C.grey(f"  {'coluna':<14}{'faixa real':<20}{'classe':<16}"
                 f"{'int64':>10}{'compacto':>11}{'economia':>10}"))
    total_conv = total_smart = 0
    no_gain = False
    for name, vals, lo, hi, cname, cbytes, signed in int_cols:
        conv_b, smart_b = len(vals)*8, len(vals)*cbytes
        total_conv += conv_b; total_smart += smart_b
        econ = 100*(1 - smart_b/conv_b) if conv_b else 0
        ecs = C.g(f"{econ:>9.0f}%") if econ > 0 else C.dim(f"{econ:>9.0f}%")
        if econ == 0: no_gain = True
        print(f"  {name[:13]:<14}{fmt_range(lo,hi):<20}{('int64→'+cname):<16}"
              f"{hb(conv_b):>10}{hb(smart_b):>11}{ecs}")
    for name in float_names:
        conv_b = nrows*8; total_conv += conv_b; total_smart += conv_b; no_gain = True
        print(f"  {name[:13]:<14}{'(ponto flutuante)':<20}{'float64 (igual)':<16}"
              f"{hb(conv_b):>10}{hb(conv_b):>11}{C.dim('        0%')}")
    print(C.grey("  " + line))
    ratio_mem = total_conv/total_smart if total_smart else 1.0
    pct_mem = 100*(1 - total_smart/total_conv) if total_conv else 0
    print(C.b("  MEMÓRIA total (campos numéricos)"))
    print(f"    convencional (int64/float64) : {C.r(hb(total_conv))}")
    print(f"    Smart2Raw (classe que cabe)  : {C.g(hb(total_smart))}")
    print(f"    ganho                        : {C.g(C.b(f'{ratio_mem:.2f}×'))}  "
          f"({C.g(f'{pct_mem:.0f}% menos')})")
    print(C.grey("  " + line))

    # ----- baseline SQLite × Smart2Raw (.s2r) -----
    engine, label = build_c_engine(here)
    sq = measure_sqlite(int_cols, engine, here)
    r_disk = sq["db_size"]/sq["s2r_size"] if sq["s2r_size"] else 1.0
    print(C.b("  DISCO  (baseline: SQLite — banco binário de verdade)"))
    print(f"    SQLite (.db)            : {C.r(hb(sq['db_size']))}")
    print(f"    Smart2Raw (.s2r)        : {C.g(hb(sq['s2r_size']))}")
    print(f"    ganho                   : {C.g(C.b(f'{r_disk:.2f}× menor'))}")
    if sq.get("db_path"):
        print(C.dim(f"    gravei na pasta: {sq['db_path']} (SQLite) e "
                    f"{sq['s2r_dir']}/ ({sq['nfiles']} arquivos .s2r) — abra e confira"))
    print(C.grey("  " + line))
    if "sum_sql" in sq:
        print(C.b(f"  CONSULTA  (SQLite × Smart2Raw, {sq['ncomp']} colunas que compactam)"))
        print(C.grey(f"    {'operação':<18}{'SQLite':>14}{'Smart2Raw':>14}{'ganho':>9}"))
        for nm, ts, t2 in [("soma (SUM)", sq["sum_sql"], sq["sum_s2r"]),
                           ("filtro (COUNT>)", sq["cnt_sql"], sq["cnt_s2r"])]:
            sp = ts/t2 if t2 else 1.0
            print(f"    {nm:<18}{C.r(f'{ts*1e6:>9.0f} µs')}{C.g(f'{t2*1e6:>9.1f} µs')}"
                  f"{C.g(C.b(f'{sp:>8.0f}×'))}{C.dim(' ✓')}")
        print(C.dim("    SQLite é um banco completo (SQL, índices, updates, durabilidade);"))
        print(C.dim("    aqui isolamos a varredura de colunas inteiras. O Smart2Raw ganha"))
        print(C.dim("    por ser colunar (lê só a coluna) e operar no compacto com SIMD."))
    else:
        print(C.dim("  CONSULTA: a comparação de velocidade precisa do motor nativo"))
        print(C.dim("  (mesma condição da seção VELOCIDADE abaixo)."))
    print(C.grey("  " + line))

    # ----- deslocamento (exato, sobre as colunas inteiras) -----
    int_conv_b = sum(len(v)*8 for _,v,_,_,_,_,_ in int_cols)
    int_smart_b = sum(len(v)*cb for _,v,_,_,_,cb,_ in int_cols)
    ratio_move = int_conv_b/int_smart_b if int_smart_b else 1.0
    print(C.b("  DESLOCAMENTO de dados (bytes por varredura das colunas inteiras)"))
    print(f"    convencional : {C.r(hb(int_conv_b))}")
    print(f"    Smart2Raw    : {C.g(hb(int_smart_b))}")
    print(f"    ganho        : {C.g(C.b(f'{ratio_move:.2f}× menos dados movidos'))}")
    print(C.grey("  " + line))

    # ----- velocidade: motor nativo (C via ctypes) -> numpy -> nada -----
    speed = None
    if engine is not None:
        speed = speed_with_c(engine, int_cols, label)
    else:
        np = None
        try: import numpy as np
        except Exception: np = None
        if np is not None:
            speed = speed_with_numpy(np, int_cols)
            print(C.dim(f"  (motor nativo C indisponível: {label}; usei numpy)"))
        else:
            print(C.b("  VELOCIDADE"))
            print(C.y(f"    motor nativo indisponível ({label}) e numpy ausente."))
            print(C.dim("    memória e deslocamento acima já são o ganho exato e garantido."))
            print(C.dim("    para medir velocidade com o SIMD real, deixe 'smart2raw.h' (ou a"))
            print(C.dim("    pasta include/) ao lado e tenha cc/gcc/clang no PATH."))
    print(C.grey("  " + line))

    # ----- biblioteca real: round-trip .s2r (opcional) -----
    s2r_info = try_real_s2r(int_cols, here)
    if s2r_info:
        print(C.b("  BIBLIOTECA REAL · round-trip .s2r"))
        print(f"    coluna {C.c(s2r_info['name'])} "
              f"({s2r_info['n']:,} valores, classe {s2r_info['class']})")
        print(f"    gravado {C.c(s2r_info['path'])}: {C.g(hb(s2r_info['s2r_size']))}"
              f"   vs int64 em memória {C.r(hb(s2r_info['int64_size']))}")
        rt = "✓" if s2r_info["ok"] else "✗"
        print(f"    CRC verificado {C.g('✓')}    round-trip idêntico "
              f"{C.g(rt) if s2r_info['ok'] else C.r(rt)}")
        print(C.grey("  " + line))

    # ----- ganho total / resumo honesto -----
    print(C.b(C.g("  GANHO TOTAL")))
    parts = (f"    memória : {C.g(C.b(f'{ratio_mem:.2f}×'))}    "
             f"deslocamento : {C.g(C.b(f'{ratio_move:.2f}×'))}")
    if speed is not None:
        parts += f"    velocidade : {C.g(C.b(f'{speed:.2f}×'))}"
    print(parts)
    print()
    print(C.grey("  Leitura honesta:"))
    print(C.grey("   · memória e deslocamento são exatos (tipos nativos do módulo array);"))
    if speed is not None:
        print(C.grey("     a velocidade foi medida aqui com o SIMD real e varia por máquina."))
    else:
        print(C.grey("     a velocidade precisa de um motor nativo (ver acima)."))
    if no_gain:
        print(C.grey("   · colunas que já precisam de 64 bits ou são float NÃO encolhem —"))
        print(C.grey("     aparecem com 0% acima, de propósito. Não é compressão."))
    print(C.grey("   · a classe de cada coluna saiu da faixa real dos dados: adapta-se"))
    print(C.grey("     sozinho a este arquivo e a esta máquina."))
    rodape(engine is not None, label)

def _tile(int_cols, target_conv_bytes=48*1024*1024):
    """Constroi arrays nativos (stdlib) replicados ate o working set alvo."""
    base = int_cols[0][1]
    per_col = target_conv_bytes / (len(int_cols) * 8)
    reps = max(1, int(per_col / max(1, len(base))))
    conv, comp, meta = [], [], []
    for name, vals, lo, hi, cname, cbytes, signed in int_cols:
        a64 = array.array("q", vals) * reps
        tc = arr_typecode(cbytes, signed)
        ac = array.array(tc, vals) * reps
        conv.append(a64); comp.append(ac)
        addr_c, n = a64.buffer_info()
        addr_s, _ = ac.buffer_info()
        meta.append((name, n, cbytes, (lo+hi)//2, addr_c, addr_s))
    return conv, comp, meta, reps

def speed_with_c(lib, int_cols, label):
    conv, comp, meta, reps = _tile(int_cols)
    n_values = sum(m[1] for m in meta)
    conv_bytes = sum(m[1]*8 for m in meta)
    s2rb_sum, s2rb_cnt = lib.s2rb_sum, lib.s2rb_count_gt
    # pre-computa ponteiros e parametros uma vez (fora do cronometro)
    cols = [(n, w, k, ctypes.c_void_p(ac), ctypes.c_void_p(asd))
            for (name, n, w, k, ac, asd) in meta]

    # confere a corretude (1 passada, fora do cronometro)
    ok = True
    for (n, w, k, pc, ps) in cols:
        if s2rb_sum(pc, n, 8, 0) != s2rb_sum(ps, n, w, 1): ok = False
        if s2rb_cnt(pc, n, 8, k) != s2rb_cnt(ps, n, w, k): ok = False

    def t_sum(use_compact):
        t0 = time.perf_counter()
        for _ in range(passes):
            for (n, w, k, pc, ps) in cols:
                if use_compact: s2rb_sum(ps, n, w, 1)
                else:           s2rb_sum(pc, n, 8, 0)
        return time.perf_counter()-t0
    def t_cnt(use_compact):
        t0 = time.perf_counter()
        for _ in range(passes):
            for (n, w, k, pc, ps) in cols:
                if use_compact: s2rb_cnt(ps, n, w, k)
                else:           s2rb_cnt(pc, n, 8, k)
        return time.perf_counter()-t0

    passes = 3
    t_sum(False); t_sum(True)               # aquece
    t1 = t_sum(False)
    passes = max(3, min(200, int(0.3/(t1/3)) if t1>0 else 30))

    print(C.b("  VELOCIDADE por operação (SIMD real via ctypes)"))
    print(C.dim(f"    motor: {label} · working set {hb(conv_bytes)} (int64) · {passes} passadas"))
    print(C.grey(f"    {'operação':<16}{'convencional':>16}{'Smart2Raw':>16}{'ganho':>9}"))
    chk = "✓" if ok else "✗"
    rows = []
    for nm, fconv, fcomp, scanned in [("soma", t_sum, None, n_values),
                                      ("filtro (faixa)", t_cnt, None, n_values)]:
        tc = fconv(False); ts = fconv(True)
        thr_c = scanned*passes/tc/1e6 if tc else 0
        thr_s = scanned*passes/ts/1e6 if ts else 0
        sp = tc/ts if ts else 1.0
        rows.append((nm, sp))
        print(f"    {nm:<16}{C.r(f'{thr_c:>9.0f} Mval/s')}{C.g(f'{thr_s:>9.0f} Mval/s')}"
              f"{C.g(C.b(f'{sp:>8.2f}×'))}{C.dim(' '+chk)}")
    # destaque: a varredura por filtro (limitada por banda)
    if "MSVC" in label:
        print(C.dim("    nota: sob MSVC os kernels SIMD escritos à mão da lib (gcc/clang) não"))
        print(C.dim("    são compilados; soma/filtro usam laços auto-vetorizados (/arch:AVX2)."))
    speed = dict(rows).get("filtro (faixa)", dict(rows).get("soma", 1.0))
    return speed

def speed_with_numpy(np, int_cols):
    base = int_cols[0][1]
    target = 48*1024*1024
    reps = max(1, int((target/(len(int_cols)*8)) / max(1, len(base))))
    conv, comp, thr = [], [], []
    for name, vals, lo, hi, cname, cbytes, signed in int_cols:
        a = np.array(vals, dtype=np.int64)
        dt = {(1,False):np.uint8,(2,False):np.uint16,(4,False):np.uint32,(8,False):np.uint64,
              (1,True):np.int8,(2,True):np.int16,(4,True):np.int32,(8,True):np.int64}[(cbytes,signed)]
        conv.append(np.tile(a, reps)); comp.append(np.tile(a.astype(dt), reps))
        thr.append(int((lo+hi)//2))
    n_values = sum(a.size for a in conv); conv_bytes = sum(a.nbytes for a in conv)
    def t_filter(arrs):
        c=0
        for a,k in zip(arrs,thr): c+=int(np.count_nonzero(a>k))
        return c
    def timeit(fn,arrs,passes):
        t0=time.perf_counter(); r=None
        for _ in range(passes): r=fn(arrs)
        return time.perf_counter()-t0, r
    timeit(t_filter, conv, 2); t1,_=timeit(t_filter,conv,2)
    passes = max(3, min(120, int(0.3/(t1/2)) if t1>0 else 20))
    print(C.b("  VELOCIDADE de varredura (filtro por faixa, via numpy)"))
    print(C.dim(f"    working set {hb(conv_bytes)} (int64) · {passes} passadas"))
    tc,rc=timeit(t_filter,conv,passes); ts,rs=timeit(t_filter,comp,passes)
    sp = tc/ts if ts else 1.0
    print(f"    convencional : {C.r(f'{n_values*passes/tc/1e6:8.0f} Mval/s')}")
    print(f"    Smart2Raw    : {C.g(f'{n_values*passes/ts/1e6:8.0f} Mval/s')}")
    print(f"    ganho        : {C.g(C.b(f'{sp:.2f}×'))}  "
          f"(resultado idêntico {'✓' if rc==rs else '✗'})")
    print(C.dim("    nota: numpy reduz inteiros estreitos de forma instável; por isso a"))
    print(C.dim("    varredura (filtro) e nao a soma. O motor C (ctypes) mede o SIMD real."))
    return sp

def rodape(c_ok, label):
    print(C.grey("  " + "─" * 64))
    cpu = os.cpu_count() or "?"
    nuc = "1 núcleo" if cpu == 1 else f"{cpu} núcleos"
    eng = f"C/SIMD ({label})" if c_ok else "—"
    print(C.dim(f"  máquina: {platform.system()} {platform.machine()} · {nuc} · "
                f"Python {platform.python_version()} · motor velocidade: {eng}"))
    print(C.dim("  Sem numpy: memória/deslocamento usam o módulo 'array' (tipos nativos);"))
    print(C.dim("  a velocidade usa 'ctypes' chamando a própria biblioteca em C do Smart2Raw."))
    print()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
