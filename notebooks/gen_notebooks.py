#!/usr/bin/env python3
# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates the Smart2Raw .ipynb notebooks (nbformat 4) with already-tested code."""
import json, os

OUT = os.path.dirname(os.path.abspath(__file__))

def md(text):
    return {"cell_type": "markdown", "metadata": {}, "source": text}

def code(text):
    return {"cell_type": "code", "metadata": {}, "execution_count": None,
            "outputs": [], "source": text}

def write_nb(name, cells):
    nb = {"cells": cells,
          "metadata": {"kernelspec": {"display_name": "Python 3", "language": "python", "name": "python3"},
                       "language_info": {"name": "python", "version": "3"}},
          "nbformat": 4, "nbformat_minor": 5}
    # nbformat wants 'source' as a list of lines (with \n)
    for c in nb["cells"]:
        s = c["source"]
        c["source"] = [l + "\n" for l in s.split("\n")[:-1]] + [s.split("\n")[-1]] if "\n" in s else [s]
    path = os.path.join(OUT, name)
    with open(path, "w") as f:
        json.dump(nb, f, indent=1)
    print("ok:", name)

PREAMBLE = r'''import os, subprocess, tempfile
import matplotlib.pyplot as plt

def find_include():
    d = os.getcwd()
    for _ in range(6):
        cand = os.path.join(d, "include", "smart2raw.h")
        if os.path.exists(cand):
            return os.path.join(d, "include")
        d = os.path.dirname(d)
    raise FileNotFoundError("include/smart2raw.h not found; run from the repo")

INC = find_include()

def compile_run(src, args_list, flags="-O3 -march=native"):
    """Compiles a C harness (with the header) and runs it for each args; returns outputs."""
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as f:
        f.write(src); cpath = f.name
    exe = cpath[:-2]
    subprocess.check_call(["gcc"] + flags.split() + ["-I", INC, "-o", exe, cpath])
    outs = []
    for a in args_list:
        outs.append(subprocess.check_output([exe] + [str(x) for x in a]).decode().strip())
    os.remove(cpath); os.remove(exe)
    return outs

print("include:", INC)'''

# ---------------- 01 cache cliff ----------------
C_CLIFF = r'''#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
static double ms(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec*1e-6;}
int main(int argc,char**argv){
  size_t N=argc>1?strtoull(argv[1],0,10):100000;
  uint8_t *a=malloc(N); int64_t *b=malloc(N*8);
  for(size_t i=0;i<N;i++){a[i]=(uint8_t)(i*131);b[i]=(int64_t)a[i];}
  double bu=1e18,bi=1e18; volatile uint64_t s=0;
  for(int r=0;r<9;r++){double t=ms();uint8_t m=0;for(size_t i=0;i<N;i++)if(a[i]>m)m=a[i];s+=m;double d=ms()-t;if(d<bu)bu=d;}
  for(int r=0;r<9;r++){double t=ms();int64_t m=0;for(size_t i=0;i<N;i++)if(b[i]>m)m=b[i];s+=(uint64_t)m;double d=ms()-t;if(d<bi)bi=d;}
  printf("%zu %.6f %.6f\n",N,bu,bi);free(a);free(b);(void)s;return 0;}'''

cliff_measure = 'SRC = r"""' + C_CLIFF + '"""\n\n' + r'''sizes = [2000,4000,8000,16000,32000,64000,128000,256000,512000,
         1000000,2000000,4000000,8000000,16000000,32000000]
rows = [list(map(float, o.split())) for o in compile_run(SRC, [[n] for n in sizes])]
N   = [r[0] for r in rows]
spd = [r[2]/r[1] for r in rows]   # int64_time / u8_time
for n,s in zip(N,spd): print(f"N={int(n):>9}  speedup={s:5.1f}x")'''

cliff_plot = r'''fig, ax = plt.subplots(figsize=(9,4))
ax.semilogx(N, spd, "o-", color="#2E5A88", lw=2)
for x, lab in [(48*1024,"L1 48KB"), (2*1024*1024,"L2 2MB")]:
    ax.axvline(x, color="#C2772E", ls=":")
    ax.text(x, max(spd)*0.95, lab, rotation=90, va="top", color="#C2772E", fontsize=8)
ax.set_xlabel("N (elements; array u8 = N bytes, int64 = 8N bytes)")
ax.set_ylabel("speedup (max scan: int64 / u8)")
ax.set_title("Cache cliff (measured on this machine)")
ax.grid(alpha=0.3); plt.show()'''

write_nb("01_cache_cliff.ipynb", [
    md("# 01 - Cache cliff\n\nScan of `max` over a `u8` array vs `int64` for increasing sizes. "
       "The gain spikes when the `u8` array still fits in cache but `int64` (8x larger) has already overflowed it. "
       "**All measured on this machine** (run the cells)."),
    code(PREAMBLE),
    md("## Measurement (compiles a C harness and sweeps N)"),
    code(cliff_measure),
    md("## Chart"),
    code(cliff_plot),
    md("Expected: a peak in the L1/L2 window (where u8 fits and int64 does not) and a smaller plateau in RAM. "
       "Honest note: this holds for scans (max/min/count); the *naive* byte sum only "
       "gains with the SIMD kernel `vpsadbw` (see `sum_fast`)."),
])

# ---------------- 02 PFOR ----------------
C_PFOR = r'''#include <stdio.h>
#include <stdlib.h>
#include "smart2raw.h"
int main(int argc,char**argv){
  long ppm=argc>1?atol(argv[1]):100; size_t N=4000000; srand(1);
  uint64_t *x=malloc(N*sizeof(uint64_t));
  for(size_t i=0;i<N;i++) x[i]=(uint64_t)(i%200);
  size_t k=(size_t)((double)N*ppm/1e6);
  for(size_t j=0;j<k;j++) x[(size_t)rand()%N]=5000000+rand()%1000000;
  S2RBlocked b; s2r_blocked_build(&b,x,N,256);
  uint64_t mx=0; for(size_t i=0;i<N;i++) if(x[i]>mx)mx=x[i];
  size_t single_w=(size_t)(s2r_classify(mx)/8)*N;
  printf("%ld %zu %zu\n",ppm,single_w,s2r_blocked_bytes(&b));
  s2r_blocked_free(&b); free(x); return 0;}'''

pfor_measure = 'SRC = r"""' + C_PFOR + '"""\n\n' + r'''ppms = [0,10,50,100,200,500,1000,2000,5000,10000]   # parts per million of outliers
rows = [list(map(float, o.split())) for o in compile_run(SRC, [[p] for p in ppms])]
ppm = [r[0] for r in rows]
ratio = [ (r[1]/r[2]) if r[2] else 1.0 for r in rows ]  # single width / block-wise
for p,r in zip(ppm,ratio): print(f"{p/1e4:5.2f}% outliers  ->  {r:4.2f}x memory recovered")'''

pfor_plot = r'''fig, ax = plt.subplots(figsize=(9,4))
ax.plot([p/1e4 for p in ppm], ratio, "o-", color="#2E7D5B", lw=2)
ax.axhline(1.0, color="#888", ls=":")
ax.set_xlabel("% outliers"); ax.set_ylabel("memory: single_w width / per block")
ax.set_title("PFOR: memory recovery vs outlier fraction (measured)")
ax.grid(alpha=0.3); plt.show()'''

write_nb("02_pfor.ipynb", [
    md("# 02 - Block-wise width (PFOR)\n\nAn outlier inflates only its own block. Here we measure the memory of the "
       "block-wise width vs the safe single_w width, varying the outlier fraction. Uses the real `S2RBlocked` type."),
    code(PREAMBLE),
    md("## Measurement"),
    code(pfor_measure),
    md("## Chart"),
    code(pfor_plot),
    md("Expected: ~3.7x when outliers are rare (0.01%) and a gradual drop as they multiply "
       "(more blocks need the wide class). On clean data, ~1x (no harm)."),
])

# ---------------- 03 zone-map (puro Python) ----------------
zone_code = r'''import matplotlib.pyplot as plt

# "Free" zone-map model: each column carries 1 class byte (range bound).
# A query for a large value skips columns whose class can't reach that value,
# reading ONLY the class bytes (metadata), without touching the payload.
NCOLS = 2000
ROWS_PER_COL = 100_000      # payload per column (elements)
class_bound = {8: 255, 16: 65535, 32: 4294967295}

def simulate(frac_wide):
    # 'frac_wide' of the columns are u32 (may hold large values); the rest, u8.
    wide = int(NCOLS * frac_wide)
    cols = [32]*wide + [8]*(NCOLS-wide)
    V = 1000          # query: "any values >= 1000?" (only u32 columns qualify)
    must_scan = [c for c in cols if class_bound[c] >= V]
    skipped = NCOLS - len(must_scan)
    meta_bytes = NCOLS                      # 1 class byte per column
    full_bytes = sum((c//8)*ROWS_PER_COL for c in cols)
    scan_bytes = sum((c//8)*ROWS_PER_COL for c in must_scan) + meta_bytes
    return skipped/NCOLS*100, (1 - scan_bytes/full_bytes)*100

fracs = [0.005,0.01,0.02,0.05,0.10,0.20]
pct_skip = []; pct_band = []
for f in fracs:
    s,b = simulate(f); pct_skip.append(s); pct_band.append(b)
    print(f"{f*100:5.1f}% wide columns -> skips {s:4.1f}% of columns, avoids {b:4.1f}% of bandwidth")

fig, ax = plt.subplots(figsize=(9,4))
ax.plot([f*100 for f in fracs], pct_skip, "o-", label="% columns skipped", color="#2E5A88", lw=2)
ax.plot([f*100 for f in fracs], pct_band, "s--", label="% bandwidth avoided", color="#2E7D5B", lw=2)
ax.set_xlabel("% of 'wide' columns (u32)"); ax.set_ylabel("%")
ax.set_title("Free zone-map: the class (1 byte/column) skips the scan")
ax.legend(); ax.grid(alpha=0.3); plt.show()'''

write_nb("03_zonemap.ipynb", [
    md("# 03 - Free zone-map\n\nEach column's class (1 byte) is already a zone-map: it bounds the possible range. "
       "A query for a value that only fits in wide columns skips the narrow ones, reading **only the class "
       "bytes**. Deterministic model (no timing noise)."),
    code(zone_code),
    md("Reproduces the whitepaper order of magnitude: a few metadata bytes skip the vast majority of "
       "columns and avoid almost all the read bandwidth."),
])

# ---------------- 04 AI blocked ----------------
C_AI = r'''#include <stdio.h>
#include <stdlib.h>
#include "smart2raw.h"
int main(int argc,char**argv){
  double pct=argc>1?atof(argv[1]):1.0; srand(7);
  size_t T=2048,C=512,N=T*C; uint64_t*cm=malloc(N*8),*rm=malloc(N*8);
  size_t nout=0;
  for(size_t c=0;c<C;c++){int o=((double)rand()/RAND_MAX*100.0)<pct; if(o)nout++;
    for(size_t t=0;t<T;t++){uint64_t v=o?(uint64_t)(2000+rand()%25000):(uint64_t)(rand()%201);cm[c*T+t]=v;rm[t*C+c]=v;}}
  S2RBlocked bc,br; s2r_blocked_build(&bc,cm,N,T); s2r_blocked_build(&br,rm,N,C);
  size_t unif=2*N;
  printf("%.3f %zu %zu %zu\n",100.0*nout/C,unif,s2r_blocked_bytes(&bc),s2r_blocked_bytes(&br));
  s2r_blocked_free(&bc);s2r_blocked_free(&br);free(cm);free(rm);return 0;}'''

ai_measure = 'SRC = r"""' + C_AI + '"""\n\n' + r'''pcts = [0,0.5,1.0,1.5,2.0,3.0]
rows = [list(map(float, o.split())) for o in compile_run(SRC, [[p] for p in pcts])]
meas   = [r[0] for r in rows]            # measured % of outlier channels
r_chan = [r[1]/r[2] for r in rows]       # uniform u16 / per channel (localized outlier)
r_tok  = [r[1]/r[3] for r in rows]       # uniform u16 / per token (spread outlier)
for m,a,b in zip(meas,r_chan,r_tok): print(f"{m:4.1f}% outliers  per-channel={a:4.2f}x  per-token={b:4.2f}x")'''

ai_plot = r'''fig, ax = plt.subplots(figsize=(9,4))
ax.plot(meas, r_chan, "o-", color="#2E7D5B", lw=2, label="per channel (localized outlier)")
ax.plot(meas, r_tok,  "s--", color="#C2772E", lw=2, label="per token (spread outlier)")
ax.axhline(1.0, color="#888", ls=":")
ax.set_xlabel("% outlier channels"); ax.set_ylabel("memory: uniform u16 / per block")
ax.set_title("AI: block-wise width; layout decides the gain (measured)")
ax.legend(); ax.grid(alpha=0.3); plt.show()'''

write_nb("04_ai_blocked.ipynb", [
    md("# 04 - Block-wise width in AI\n\nQuantized activation with outlier channels. **Layout decides the gain**: "
       "per channel (outlier localized in one block) recovers ~2x vs the safe uniform-u16 width; per token "
       "(outlier spread across all blocks) stays ~1x. Honest yardstick: byte-granular does not beat flat int8."),
    code(PREAMBLE),
    md("## Measurement"),
    code(ai_measure),
    md("## Chart"),
    code(ai_plot),
    md("The separation between the two curves is the key point: to gain in AI, the outliers must be "
       "localized in memory (per-channel/per-token layout)."),
])

# ---------------- 05 capacity model (puro Python) ----------------
cap_code = r'''import numpy as np, matplotlib.pyplot as plt

# Capacity model (ESTIMATE, not a measurement):
#   capacity = 1 / [ (1 - f) + f/r ]      memory freed = f*(1 - 1/r)
# f = fraction of resident memory in the qualifying profile; r = reduction factor.
def multiplier(f, r): return 1.0/((1-f)+f/r)
def freed(f, r):      return f*(1-1/r)

f = np.linspace(0, 0.9, 100)
fig, ax = plt.subplots(figsize=(9,4))
for r, c in [(4,"#2E5A88"),(8,"#2E7D5B"),(16,"#C2772E")]:
    ax.plot(f*100, multiplier(f, r), color=c, lw=2, label=f"reduction {r}x")
ax.set_xlabel("% of resident memory in the qualifying profile (f)")
ax.set_ylabel("capacity multiplier (same hardware)")
ax.set_title("How much more data fits in the SAME server (model estimate)")
ax.legend(); ax.grid(alpha=0.3); plt.show()

print("Table (multiplier | memory freed):")
print(f"{'f':>5} | {'r=4x':>14} | {'r=8x':>14}")
for ff in [0.2,0.4,0.6,0.8]:
    print(f"{int(ff*100):>4}% | {multiplier(ff,4):>5.2f}x ({freed(ff,4)*100:4.1f}%) | "
          f"{multiplier(ff,8):>5.2f}x ({freed(ff,8)*100:4.1f}%)")'''

write_nb("05_capacity_model.ipynb", [
    md("# 05 - Server capacity model\n\n**Estimate, not a measurement.** Translates the per-workload gain into "
       "capacity on the same hardware. Depends on `f` (fraction of RAM in the qualifying profile) and `r` (reduction). "
       "Applies to the CPU/storage/telemetry ring; **not** to the training/inference GPU."),
    code(cap_code),
    md("For a concrete workload, estimate `f` (how much of RAM is small-range integers) and `r` (the reduction "
       "measured in the earlier notebooks) and read the multiplier / memory freed."),
])

print("\nAll notebooks generated.")
