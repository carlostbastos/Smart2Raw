# Smart2Raw — Benchmark Maestro

A self-contained, terminal benchmark that compares three ways of storing and
processing the **same integer data**:

- **Conventional** — everything as `int64`
- **SQLite** — a real, ubiquitous embedded database (binary, B‑tree, varint‑packed)
- **Smart2Raw** — stores each column in the **smallest native class that fits**
  (±8/16/32/64‑bit) and computes directly on the compact form

Python is only the *maestro* (the conductor): it drives the whole comparison
using **the standard library alone** — `array` for memory, `sqlite3` for the
database baseline, and `ctypes` to call Smart2Raw's real **C/SIMD** kernels for
speed. **No numpy required.**

On the bundled sample data, Smart2Raw uses **2.73× less memory**, is **1.49×
smaller on disk than SQLite**, moves **3.15× less data per scan**, and runs
integer **SUM/COUNT 50–160× faster** than SQLite — every figure produced by the
script itself, on your machine.

---

## What it measures

The full lifecycle of a dataset, not just file size:

- **Memory** footprint in RAM
- **Disk** size versus a real database (SQLite)
- **Query** speed (SUM / COUNT) versus that database
- **Data movement** (bytes read per scan)
- **Raw SIMD throughput** on the compact form

Each figure is labeled **exact** (counted bytes) or **measured** (timed on your
hardware), so you always know what you're reading.

---

## The core idea

A column of integers rarely needs 64 bits. A range of `0..100` fits in one byte
(`u8`); `0..65535` fits in `u16`, and so on. Smart2Raw picks that smallest native
class per column from the **actual data**, stores the values packed at that width,
and runs arithmetic straight on the packed bytes — column by column, with SIMD
kernels (AVX2 / AVX‑512 on x86, NEON on ARM) selected at runtime. Less to hold,
less to move, a tighter loop for the CPU.

Columns that truly need 64 bits, or that are floating point, stay as‑is and show
`0%`. This is choosing the right native type, not compression.

---

## Files in this folder

| File | Role |
|---|---|
| `smart2raw_bench.py` | The maestro — run this |
| `s2r_shim.c`         | Source of the speed engine (compiled on first run) |
| `smart2raw_demo.csv` | Sample input (also generated automatically if absent) |
| `README.md`          | This file |

The header `smart2raw.h` is **not** duplicated here — the maestro finds it in the
repository's `include/`. It searches in this order, and the repository copy always
wins, so a checkout can never be benchmarked against a stale vendored header:

1. `$SMART2RAW_INCLUDE` — explicit override
2. `../../include/smart2raw.h` — the repository's canonical header
3. `../include/`, `./include/`, `./smart2raw.h` — only for when this folder has
   been copied out on its own

The native engine (`s2r_kernels.dll` / `.so`) is a build artifact and is **not**
committed to git; the maestro builds it on first run, or you can drop a prebuilt
one beside the script (see below).

---

## Requirements

- **Python 3.8 or newer.** Standard library only (`array`, `ctypes`, `sqlite3`).
- For the **speed** sections: a C compiler on `PATH` (`gcc` / `clang` / `cc`, or
  MSVC `cl`) so the engine can be built — or a prebuilt `s2r_kernels.*` beside the
  script. Without either, the speed sections are skipped.

---

## Quick start

```sh
python smart2raw_bench.py
```

On a typical developer machine that's all you need: the maestro builds its native
engine from `s2r_shim.c` on first run and measures everything with the real SIMD
kernels. See *The speed engine* if you don't have a compiler.

Options:

```sh
python smart2raw_bench.py my_data.csv    # use your own integer CSV
python smart2raw_bench.py --no-color     # disable ANSI colors
```

If no `.csv` is present, the maestro generates a deterministic
`smart2raw_demo.csv` (~670 KB, seed `20260609`) on first run, so results are
reproducible.

---

## How it works (data flow)

```
smart2raw_demo.csv            (input: text, ~670 KB)
        |   the maestro reads it and finds the smallest class per column
        +------------------>  smart2raw_demo.db     (SQLite, binary)   <- baseline
        +------------------>  s2r_cols/*.s2r         (Smart2Raw, binary) <- evaluated
```

The CSV is the **source** of reproducible data (your "raw export"). From it, the
maestro derives a SQLite database, the Smart2Raw `.s2r` files, and an in‑memory
`int64` form, then measures all three. The two derived forms are written to disk
so you can open and inspect them.

---

## The speed engine

Smart2Raw's speed sections run through its native C/SIMD kernels. The maestro
resolves the engine in this order:

1. **Compile on first run** — if a C compiler (`gcc` / `clang` / `cc`, or MSVC
   `cl`) is on `PATH`, the maestro compiles `s2r_shim.c` automatically via
   `ctypes`, using `smart2raw.h` from the repo's `include/`. No setup on a typical
   dev box.
2. **Prebuilt binary** — drop `s2r_kernels.dll` (Windows) or `s2r_kernels.so`
   (Linux) beside the script and it is loaded directly, no compiler needed. Handy
   for machines without a toolchain; grab one from the project Releases or build
   it once (below).
3. **Explicit compiler path** — when the compiler is not on `PATH`:
   ```bat
   set "S2R_CC=C:\path\to\gcc.exe"
   python smart2raw_bench.py
   ```

With none of these, the maestro still reports memory, disk and data movement
(exact) and skips the speed sections.

### Building a reusable engine

To produce a portable `s2r_kernels.*` once (then reuse it, or share it via a
Release):

```sh
# Linux / macOS
cc -O3 -shared -fPIC s2r_shim.c -I ../../include -o s2r_kernels.so   # .dylib on macOS
```
```bat
REM Windows (MSYS2 gcc; LLVM clang needs MSVC to link a DLL)
gcc -O3 -shared -static s2r_shim.c -I ..\..\include -o s2r_kernels.dll
```

A 64‑bit engine loads only in a 64‑bit Python.

---

## Reading the report

| Section | Compares | Kind |
|---|---|---|
| **MEMORY** | `int64` vs smallest class, per column and total | exact |
| **DISK** | `SQLite (.db)` vs `Smart2Raw (.s2r)` | exact (real files) |
| **QUERY** | `SQLite` vs `Smart2Raw` — SUM and COUNT > k | measured |
| **DATA MOVEMENT** | bytes read per full scan of the integer columns | exact |
| **SPEED** | `int64` vs compact, same scan | measured (real SIMD) |

DISK writes `smart2raw_demo.db` and `s2r_cols/`, so the sizes you see are real
files. QUERY runs SQLite's own engine against Smart2Raw's SIMD kernels; SQLite is
a full database (SQL, indexes, transactions), so this section measures one job —
scanning and aggregating integer columns — where a columnar, packed format with
SIMD is fastest.

---

## Example run

Real output on a Windows machine (AMD64, 8 cores, Python 3.11). Speed figures
scale with the hardware.

```
  MEMORY total (numeric fields)
    conventional (int64/float64) : 934.8 KB
    Smart2Raw (class that fits)  : 342.2 KB
    gain                         : 2.73x  (63% less)

  DISK  (baseline: SQLite)
    SQLite (.db)             : 412.0 KB
    Smart2Raw (.s2r)         : 275.7 KB
    gain                     : 1.49x smaller

  QUERY  (SQLite vs Smart2Raw, 12 columns that compact)
    operation                 SQLite     Smart2Raw    gain
    sum (SUM)               2635 us       16.3 us     161x
    filter (COUNT>)         3900 us       69.5 us      56x

  DATA MOVEMENT
    conventional : 868.1 KB
    Smart2Raw    : 275.4 KB
    gain         : 3.15x less data moved

  SPEED (int64 vs compact, real SIMD)
    sum            2131 Mval/s -> 7005 Mval/s   3.29x
    filter (range) 1416 Mval/s -> 1668 Mval/s   1.18x
```

---

## Generated artifacts

Each run writes (and overwrites) in this folder:

- `smart2raw_demo.db` — the **SQLite** database (open it with any SQLite viewer).
- `s2r_cols/` — one **`.s2r`** file per integer column.

These, plus any `s2r_kernels.*` and the generated `smart2raw_demo.csv`, are build
artifacts — add them to `.gitignore`:

```gitignore
s2r_kernels.dll
s2r_kernels.so
s2r_kernels.dylib
smart2raw_demo.db
s2r_cols/
```

### The `.s2r` format (per column)

A single packed pool of integers:

```
offset  size  field
0       4     magic   = 0x33335253 (little‑endian)
4       1     size    = class width in bits (8/16/32/64), negative if signed
5       1     flags   = bit 0 set when signed
6       1     format  = 1
7       1     reserved= 0
8       8     count   = number of values (little‑endian)
16      N*w   payload = values, packed at width w, little‑endian
16+N*w  4     crc32   = CRC‑32 of the payload (little‑endian)
```

One file per column — the columnar layout that makes single‑column scans cheap.

---

## Platform notes

- A prebuilt engine is OS/arch‑specific: a Windows‑x64 `.dll` won't run on Linux,
  a Linux‑x64 `.so` won't run on Windows, and a 32‑bit build won't load in a
  64‑bit Python. When in doubt, let the maestro compile from source.
- The maestro loads only the extension that matches the current OS, so a binary
  for another OS left in the folder is ignored rather than mis‑loaded.

---

## Troubleshooting

- **No SPEED / QUERY sections.** No native engine was found. Install a C compiler
  (or set `S2R_CC`), or drop a matching `s2r_kernels.*` beside the script.
- **Windows error `0xc000012f`.** A Linux `.so` was placed on Windows. Use a
  `.dll`. (Current versions ignore the wrong‑OS binary automatically.)
- **DLL won't load (bitness).** A 64‑bit Python needs a 64‑bit engine — build with
  an x86_64 compiler (MSYS2 `mingw64`), not a 32‑bit one.

---

## Notes

Memory, disk and data movement are exact byte counts; query and speed are timed
on your machine and scale with CPU, cache and data. The `.s2r` form covers the
integer columns — floating‑point and text are left unchanged.

*Part of the Smart2Raw project. See the repository root for the library, license,
and full documentation.*
