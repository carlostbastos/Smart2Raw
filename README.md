# Smart2Raw

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.20477234.svg)](https://doi.org/10.5281/zenodo.20477234)
[![License: AGPL v3+](https://img.shields.io/badge/License-AGPL--3.0--or--later-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-3.3.7-informational.svg)](CHANGELOG.md)
[![Language: C11](https://img.shields.io/badge/C-C11-00599C.svg)](include/smart2raw.h)
[![Header-only](https://img.shields.io/badge/build-header--only-success.svg)](include/smart2raw.h)
[![Tests](https://img.shields.io/badge/tests-17%20suites%20%C2%B7%200%20failures-brightgreen.svg)](scripts/build_and_test.sh)
[![Ports](https://img.shields.io/badge/ports-Go%20%C2%B7%20JS%20%C2%B7%20Python-blueviolet.svg)](ports/)

**Adaptive numeric storage for integer data.**

Smart2Raw is a header-only C library and a portable binary format (`.s2r`) for storing large integer arrays using the smallest native integer width that safely represents the actual data range: 8, 16, 32 or 64 bits, signed or unsigned.

The core idea is simple:

> **Classify once. Operate forever in the most compact native format.**

Instead of storing everything as `int64` or `int32` by default, Smart2Raw scans the real value range and selects the smallest native type that preserves every value. After that, operations run directly on the compact representation: sum, min, max, count, filters, statistics, group-by, sort, persistence and memory-mapped reads, without decompression and without manual bit-packing.

---

## Table of contents

- [What's new in 3.3.7](#whats-new-in-337)
- [What's new in 3.3.6](#whats-new-in-336)
- [Why this matters](#why-this-matters)
- [What Smart2Raw is](#what-smart2raw-is)
- [What Smart2Raw is not](#what-smart2raw-is-not)
- [Mental model](#mental-model)
- [Quickstart in C](#quickstart-in-c)
- [Command-line tools](#command-line-tools)
- [Core principle](#core-principle)
- [Main features](#main-features)
- [API reference](#api-reference)
- [The `.s2r` file format](#the-s2r-file-format)
- [Where Smart2Raw can help](#where-smart2raw-can-help)
- [Ports and ecosystem](#ports-and-ecosystem)
- [Format conformance](#format-conformance)
- [Measured results](#measured-results)
- [When Smart2Raw does not help much](#when-smart2raw-does-not-help-much)
- [Design philosophy](#design-philosophy)
- [Build and test](#build-and-test)
- [Repository layout](#repository-layout)
- [Roadmap](#roadmap)
- [Editions and license](#editions-and-license)
- [Citation](#citation)
- [One-sentence summary](#one-sentence-summary)

---

## What's new in 3.3.7

Release 3.3.7 extends the SIMD layer and hardens the performance story with measured, reproducible benchmarks.

- **AVX-512 `u8` sum** — a dedicated path (`_mm512_sad_epu8`, 64 bytes/iter) selected at runtime ahead of AVX2. Measured on an AVX-512 Xeon at ~1.17x (cache-resident) to ~1.30x (memory-bound) over AVX2, bit-identical to scalar. A `u16` AVX-512 kernel was benchmarked, came out slower than AVX2, and was deliberately not shipped — `u16` stays on AVX2 by measurement.
- **RISC-V Vector (RVV 1.0) path — experimental.** Written to the RVV 1.0 intrinsics, gated by `__riscv_v_intrinsic`; logic validated on x86 via an emulation shim (`tests/rvv_emu`), pending an `rv64gcv` + QEMU/hardware run.
- **ARM SVE2 path — experimental.** Written to the SVE ACLE intrinsics, gated by `__ARM_FEATURE_SVE2`; logic validated via `tests/sve2_emu`, pending real SVE hardware. At 128-bit SVE the width equals NEON, so the expected gain is marginal.
- **New reproducible benchmarks** under `benchmarks/`: `bench_avx512_width.c` (instruction width), `bench_format_endtoend.c` (conventional `int64` vs compact `u8`) and `bench_format_lanes.c` (compact format unlocks SIMD lanes). End-to-end, summing compact `u8` with AVX-512 measured ~13-14x over the conventional `int64`+scalar baseline on this machine.
- **Test suite is now 17 suites, 0 failures** (added the RVV and SVE2 emulated-logic suites).

See [`CHANGELOG.md`](CHANGELOG.md).

## What's new in 3.3.6

Release 3.3.6 adds **Analytics v2**: compact integer primitives that are useful before you reach for a full table/columnar layer, implemented in the C core and mirrored across the Go, JavaScript and Python ports (with a shared `.s2r` contract verified by conformance tests).

- `s2r_sort` — sort a pool in place while preserving its integer class.
- `s2r_is_sorted` — ascending-order check.
- `s2r_unique_sorted` — drop adjacent duplicates from a sorted pool.
- `s2r_nunique` — count distinct values without modifying the pool.
- `s2r_value_counts_u8` — 256-bin value counts for byte classes (for signed `i8`, bins use the raw stored byte: `-1` is bin 255, `-128` is bin 128).

See [`docs/ANALYTICS_V2.md`](docs/ANALYTICS_V2.md) and [`CHANGELOG.md`](CHANGELOG.md).

---

## Why this matters

Many systems store small values in unnecessarily large types.

A sensor temperature like `25` does not need 64 bits. An age fits in `u8`. HTTP status codes fit in `u16`. Many counters, flags, categories, buckets, local IDs, token IDs and discrete measurements use only a fraction of the bits normally reserved for them.

At small scale, this looks harmless. At millions or billions of values, it becomes expensive:

- more RAM usage;
- more memory bandwidth;
- more cache pressure;
- more I/O;
- more energy consumption;
- fewer values per cache line;
- fewer records fitting in the same device, server or embedded system.

Smart2Raw reduces that waste by storing integers in the smallest native width that fits the data while keeping direct indexed access.

---

## What Smart2Raw is

Smart2Raw is:

- a C header-only library (`include/smart2raw.h`, C11, no external dependencies);
- a portable `.s2r` binary format (canonical little-endian + CRC32);
- a compact integer storage layer;
- a lightweight scan and aggregation engine;
- a set of CLI tools and examples;
- a base for useful ports in other languages;
- a practical way to reduce memory, cache misses, bandwidth and I/O when integer ranges are smaller than their declared types.

It is designed to work from servers to edge devices and embedded systems.

---

## What Smart2Raw is not

Smart2Raw is not a magic compression layer for every kind of data.

It is **not**:

- a general-purpose compressor;
- a replacement for audio, video or image codecs;
- a format for encrypted data;
- a replacement for already-compressed data formats;
- a dense linear algebra engine;
- a replacement for GEMM, BLAS, tensor cores or matrix kernels;
- a floating-point quantizer;
- a full columnar database;
- a replacement for Parquet, Arrow, ClickHouse or DuckDB.

Smart2Raw works best when the data is integer-based, the real range is smaller than the default type, and the bottleneck is memory, bandwidth, cache, scanning, I/O or aggregation.

---

## Mental model

Imagine 1 million temperature readings between 0 and 60 C.

Stored as `int64`:

```text
1,000,000 values x 8 bytes = 8 MB
```

Stored with Smart2Raw as `u8`:

```text
1,000,000 values x 1 byte = 1 MB
```

The numerical result is the same. The machine simply moves fewer bytes.

Fewer bytes means more values per cache line, less memory traffic, faster scans, lower memory footprint and better use of the same hardware.

---

## Quickstart in C

Header-only — just include it and compile with any C11 compiler.

```c
#include "smart2raw.h"

int main(void) {
    S2RPool pool;

    s2r_pool_init(&pool, S2R_8, 1024);          /* start at u8 */

    for (uint64_t v = 0; v < 1000; v++) {
        s2r_push_adaptive(&pool, v);            /* promotes u8 -> u16 -> ... as needed */
    }

    uint64_t total = s2r_sum_fast(&pool);       /* SIMD-aware reduction */
    (void)total;

    s2r_save_portable(&pool, "data.s2r");       /* portable LE + CRC32 */
    s2r_pool_free(&pool);
    return 0;
}
```

```sh
cc -O2 -std=c11 -I include quickstart.c -o quickstart && ./quickstart
```

`push_adaptive` automatically promotes the pool when a new value no longer fits the current width.

For example:

```text
u8 -> u16 -> u32 -> u64
```

No silent truncation. No manual type guessing.

---

## Command-line tools

Build with `cd tools && make` (needs a C compiler). Tools read and write integers as decimal text (one per line or space-separated) and exchange `.s2r` files with the library and every port.

```text
s2r pack   <in.txt|-> <out.s2r> [--signed]   text integers -> .s2r (classifies)
s2r unpack <in.s2r> <out.txt>                .s2r -> text integers
s2r info   <file.s2r>                        metadata (class, count, fmt)
s2r verify <file.s2r>                        integrity (magic, class, count, CRC)
s2r agg    <file.s2r> sum|min|max|count-gt N|count-range A B
```

```text
s2r_verify  <file.s2r>                       field-by-field check + CRC32 recompute
                                             exit 0 = INTACT, non-zero = invalid/corrupted
s2r_convert <in|-> <out> [--op none|add|mul] [--by N] [--signed] [--cap 32|64]
            single-core cycle: convert -> process in place -> unconvert,
            refusing instead of overflowing past the --cap ceiling
```

---

## Core principle

Smart2Raw replaces this habit:

```text
Use int64 everywhere just to be safe.
```

with this rule:

```text
Use the smallest native integer type that preserves every value.
```

The core classes are:

```text
Unsigned: u8, u16, u32, u64
Signed:   i8, i16, i32, i64
```

After classification, data access remains native and direct. Smart2Raw does not require per-access bit masks or shifts like manual bit-packing.

---

## Main features

### Adaptive integer width

Smart2Raw selects the smallest class that fits the real data range.

Unsigned classes:

```text
0..255              -> u8
0..65,535           -> u16
0..4,294,967,295    -> u32
larger              -> u64
```

Signed classes:

```text
-128..127           -> i8
-32,768..32,767     -> i16
-2^31..2^31-1       -> i32
larger              -> i64
```

### Adaptive push

When a new value does not fit the current width, the pool promotes itself automatically.

```text
u8 -> u16 -> u32 -> u64
```

This avoids silent overflow and truncation.

### Bidirectional width healing

If an outlier forces a pool to grow and that outlier is later removed, the pool can be reclassified back to a smaller width.

```text
u8 -> outlier inserted -> u64
u64 -> outlier removed -> fit_class() -> u8
```

### Safe scalar arithmetic

Scalar operations can promote the pool when needed instead of wrapping around silently.

### Lazy-carry arithmetic

A chain of scalar operations can track the worst-case magnitude and promote only once at commit time.

This reduces reallocations and avoids intermediate overflows.

### SIMD-aware reductions

The C implementation can use hardware-specific optimized paths when available, selected by runtime dispatch.

On x86 the u8 sum uses `vpsadbw`: AVX2 (256-bit) and, when present, a dedicated AVX-512 path selected at runtime. The AVX-512 u8 path was measured on a Xeon at ~1.17x (cache-resident) to ~1.30x (memory-bound) over AVX2 (a u16 AVX-512 kernel was slower than AVX2, so u16 stays on AVX2). There is a NEON path for ARM, plus experimental RISC-V Vector (RVV) and ARM SVE2 paths, both validated for logic via emulation and pending hardware validation.

When the optimized path is not available, Smart2Raw falls back to scalar code.

### Zero-copy memory mapping

`.s2r` files can be memory-mapped and scanned directly without copying the entire dataset into RAM.

This is useful for:

- edge devices;
- sensor history;
- quantized artifacts;
- startup-sensitive services;
- datasets larger than available RAM;
- read-only analytics.

### Portable `.s2r` format

Smart2Raw defines a compact binary format:

```text
magic
class
flags
format version
count
little-endian payload
CRC32
```

The payload is written in canonical little-endian order. CRC32 allows corruption detection. The same files are read and written by the C library and every port.

### Block-wise width

In addition to one width for the whole array, Smart2Raw supports block-wise width (a PFOR-style layout).

This reduces the impact of outliers: a large value inflates only its own block instead of forcing the entire collection to use a wider class.

### Lightweight analytics

Smart2Raw includes operations useful for scans and lightweight analytics:

- sum;
- min;
- max;
- mean;
- variance;
- standard deviation;
- count by condition;
- range filters;
- sort and sortedness checks;
- unique / distinct counts;
- value counts for small integer classes;
- histograms;
- group-by count;
- group-by sum;
- signed-aware statistics.

---

## API reference

A representative subset of the public, `static inline` API in `include/smart2raw.h`. Unsigned and signed variants exist for most operations; only key signatures are shown.

**Types:** `S2RPool`, `S2RMap`, `S2RBlocked`, `S2RTracked`, `S2RDeferred` · enums `S2RError`, `S2RSize` (`S2R_8/16/32/64`), `S2RFlags`.

```c
/* Lifecycle */
int       s2r_pool_init(S2RPool *p, int8_t size, size_t capacity);
void      s2r_pool_free(S2RPool *p);
void      s2r_clear(S2RPool *p);
int       s2r_reserve(S2RPool *p, size_t new_cap);
int       s2r_shrink_to_fit(S2RPool *p);
int       s2r_copy(S2RPool *dst, const S2RPool *src);

/* Insertion (adaptive = grows class; checked = errors instead) */
S2RError  s2r_push_adaptive(S2RPool *p, uint64_t v);
S2RError  s2r_push_signed_adaptive(S2RPool *p, int64_t v);
size_t    s2r_push_many(S2RPool *p, const uint64_t *values, size_t count);
int       s2r_from_array_auto(S2RPool *p, const uint64_t *arr, size_t count);

/* Access */
uint64_t  s2r_get(const S2RPool *p, size_t i);
int64_t   s2r_get_signed(const S2RPool *p, size_t i);
size_t    s2r_used_bytes(const S2RPool *p);
int       s2r_is_signed(const S2RPool *p);

/* Classification / healing */
S2RSize   s2r_classify(uint64_t v);
S2RSize   s2r_classify_array(const uint64_t *arr, size_t count);
int       s2r_fit_class(S2RPool *p);          /* heal down to smallest fitting class */

/* Reductions (SIMD-aware sum_fast) */
uint64_t  s2r_sum(const S2RPool *p);
uint64_t  s2r_sum_fast(const S2RPool *p);
uint64_t  s2r_min(const S2RPool *p);
uint64_t  s2r_max(const S2RPool *p);
int64_t   s2r_sum_signed(const S2RPool *p);
int64_t   s2r_min_signed_val(const S2RPool *p);
int64_t   s2r_max_signed_val(const S2RPool *p);

/* Statistics */
double    s2r_mean(const S2RPool *p);
double    s2r_variance(const S2RPool *p);     /* sample variance (n-1) */
double    s2r_stddev(const S2RPool *p);       /* + *_signed variants */

/* Filters / counts */
size_t    s2r_count_gt(const S2RPool *p, uint64_t thr);
size_t    s2r_count_range(const S2RPool *p, uint64_t min_v, uint64_t max_v);
uint64_t  s2r_sum_if(const S2RPool *p, uint64_t min_v, uint64_t max_v);
int64_t   s2r_find(const S2RPool *p, uint64_t value);

/* Safe / lazy-carry arithmetic */
int       s2r_add_scalar_safe(S2RPool *p, uint64_t s);
int       s2r_mul_scalar_safe(S2RPool *p, uint64_t s);
void      s2r_defer_begin(S2RDeferred *d, S2RPool *p);
void      s2r_defer_add(S2RDeferred *d, uint64_t s);
void      s2r_defer_mul(S2RDeferred *d, uint64_t s);
int       s2r_defer_commit(S2RDeferred *d);   /* promotes at most once */

/* Analytics v2 (3.3.6) */
S2RError  s2r_sort(S2RPool *p);
int       s2r_is_sorted(const S2RPool *p);
size_t    s2r_unique_sorted(S2RPool *p);
S2RError  s2r_nunique(const S2RPool *p, size_t *out);
S2RError  s2r_value_counts_u8(const S2RPool *p, uint64_t counts[256]);

/* Block-wise width (PFOR) */
int       s2r_blocked_build(S2RBlocked *b, const uint64_t *src, size_t n, size_t block);
uint64_t  s2r_blocked_sum(const S2RBlocked *b);
uint64_t  s2r_blocked_sum_fast(const S2RBlocked *b);
size_t    s2r_blocked_bytes(const S2RBlocked *b);
void      s2r_blocked_free(S2RBlocked *b);

/* Persistence + zero-copy mmap */
S2RError  s2r_save_portable(const S2RPool *p, const char *filename);
S2RError  s2r_load_portable(S2RPool *p, const char *filename);
S2RError  s2r_map_open(S2RMap *m, const char *filename, int verify_crc);
void      s2r_map_close(S2RMap *m);

/* Utilities */
const char* s2r_strerror(S2RError err);
uint32_t    s2r_crc32(const void *data, size_t n, uint32_t crc);
int         s2r_has_avx2(void);
```

---

## The `.s2r` file format

A self-describing, portable file. All multibyte fields are **canonical little-endian**. A fixed 16-byte header lets an mmap reader locate the payload at a constant offset. Full spec: [`docs/S2R_FORMAT.md`](docs/S2R_FORMAT.md).

| Offset | Size           | Field   | Meaning                                                        |
|-------:|---------------:|---------|---------------------------------------------------------------|
| 0      | 4              | magic   | `0x33335253` (uint32 LE; bytes `53 52 33 33`, "SR33")          |
| 4      | 1              | size    | class as signed `int8` (negative = signed: ±8/16/32/64)        |
| 5      | 1              | flags   | bit 0 = signed                                                 |
| 6      | 1              | fmt     | format version (currently `1`)                                 |
| 7      | 1              | rsvd    | reserved (`0`)                                                 |
| 8      | 8              | count   | number of elements (uint64 LE)                                |
| 16     | count·eb       | payload | elements in canonical LE (`eb = abs(size)/8` bytes)           |
| 16+n   | 4              | crc32   | CRC32 (IEEE 802.3) of the payload (uint32 LE)                  |

On little-endian hosts the on-disk payload matches memory byte-for-byte (true zero-copy). On big-endian hosts, reads/writes convert, and the CRC is validated over the canonical LE bytes before any conversion.

---

## Where Smart2Raw can help

Smart2Raw is useful whenever you store many integers and suspect they occupy more space than they need.

### IoT and edge

Sensor and edge data often consists of small integers:

- temperature;
- humidity;
- pressure;
- luminosity;
- device states;
- counters;
- discrete events.

In RAM- and flash-constrained devices, every byte matters.

### Telemetry, logs and metrics

Operational data is often integer-heavy:

- HTTP status codes;
- latency buckets;
- error codes;
- counters;
- flags;
- metric windows;
- local IDs;
- compact event types.

Smart2Raw can reduce memory footprint and accelerate scans, counts, sums and filters.

### Lightweight analytics

For integer columns with small ranges, Smart2Raw can act as a compact scan layer.

It does not try to replace mature columnar engines. Instead, it is useful when you need a small, dependency-light component for agents, tools, embedded analytics, local processing or custom pipelines.

### Feature stores and ML preprocessing

Many tabular ML features are discrete:

- categories;
- buckets;
- IDs;
- flags;
- ranges;
- counters;
- quantized features.

Smart2Raw can reduce footprint and speed up per-column scans and aggregations.

### Artificial intelligence

Smart2Raw does not replace GEMM, tensor cores, VNNI, BLAS or specialized matrix kernels.

Its role in AI is different: storage, movement and auxiliary reductions around integer data.

Potential use cases include:

- quantized artifacts;
- integer weights or activations;
- token IDs;
- integer indices;
- discrete features;
- quantized feature stores;
- KV-cache layouts with localized outliers;
- zero-point correction sums in asymmetric quantization;
- memory-mapped quantized data;
- analysis and preprocessing of integer tensors.

Smart2Raw does not perform the main matrix multiplication. It helps with the integer storage and scan-heavy operations around it.

### Embedded systems

The C core can be built in small configurations by disabling stdio, mmap and SIMD paths.

Possible targets include:

- firmware;
- microcontrollers;
- FreeRTOS;
- Zephyr;
- ESP32;
- STM32;
- Arduino / PlatformIO;
- Raspberry Pi Pico.

### Servers

On servers, Smart2Raw is most useful when a workload is memory-, cache-, bandwidth- or I/O-bound.

Examples:

- metrics services;
- log processing;
- event counters;
- time-window aggregations;
- in-memory integer columns;
- systems storing `int64` or `int32` by default even when values fit in `u8` or `u16`.

### Architectures and platforms

The core is designed to be portable.

Natural targets include:

- Linux x86_64;
- Linux ARM64;
- macOS x86_64;
- macOS Apple Silicon;
- Windows x64;
- WebAssembly;
- Node.js;
- browsers;
- Android through the NDK;
- iOS through C/Swift wrappers;
- embedded systems;
- RISC-V through the scalar C path;
- RISC-V Vector (RVV): experimental SIMD path (logic-validated via emulation; pending hardware validation).

The canonical implementation is C. Ports exist to make the same idea useful in other ecosystems.

---

## Ports and ecosystem

The canonical implementation lives in `include/smart2raw.h`. Ports exist because each targets a real ecosystem — not to create artificial variations. They all read and write the **same** `.s2r` files.

| Port | Path | Focus | Quickstart |
|---|---|---|---|
| Go | `ports/go` | services, telemetry, backends | `cd ports/go && go test ./...` |
| JavaScript | `ports/js` | Node.js, browser demos, `.s2r` reader/writer | `cd ports/js && npm test` |
| Python | `ports/python` | notebooks, tutorials, conformance fixtures | `cd ports/python && python -m unittest discover -s tests` |

C bindings for embedding the core from other languages (e.g. a Python `ctypes` wrapper) live in `bindings/`.

The goal of these ports is not to create artificial variations. Each port exists for a real technical ecosystem.

---

## Format conformance

The `conformance/` directory contains canonical `.s2r` fixtures and cross-language format tests.

The goal is to ensure that `.s2r` is a real portable contract, not just an internal detail of the C implementation. The matrix exercises every writer against every reader:

```text
C writes           -> Go reads
C writes           -> JavaScript reads
C writes           -> Python reads
Go writes          -> other languages read
JavaScript writes  -> other languages read
Python writes      -> other languages read
```

Regenerate fixtures and run the full check:

```sh
python3 conformance/scripts/generate_fixtures.py
bash conformance/run_conformance.sh
```

---

## Measured results

Actual speedups depend on the hardware, compiler, data distribution, working-set size and baseline. Reproduce with [`scripts/reproduce.sh`](scripts/reproduce.sh); the machine and method are recorded in [`benchmarks/RESULTS.md`](benchmarks/RESULTS.md).

| Mechanism | Measured result |
|---|---:|
| Memory vs `int64` for 8-16 bit data | 75-87.5% less |
| Min/max reductions with vectorization | 8-10x |
| SIMD `u8` sum (`vpsadbw`) | 2.8-10x |
| SIMD `u16` sum | 1.4-4x |
| AVX-512 `u8` sum vs AVX2 | ~1.17-1.30x (measured, Xeon) |
| End-to-end `u8`+AVX-512 vs `int64`+scalar | ~13-14x (measured) |
| 4-way histogram on skewed data | 3.6-4x |
| `u8 -> u32` group sum | ~1.78 G rows/s |
| int8 zero-point correction | 2.3x |
| Block-wise width with 0.01% outliers | ~3.7x memory recovery |
| SIMD block sum for zero-point support | ~6.7x |
| Class-based zone-map scan | up to 94.5% bandwidth avoided in measured scenario |
| MCU build without stdio/mmap/SIMD | ~3.4 KB |

These numbers are not universal promises. They show where the mechanism works. Server-capacity figures elsewhere in the project are **model estimates**, clearly labeled as such, not measurements of a real server.

**Run it on your own data.** [`benchmarks/maestro/`](benchmarks/maestro/) is an interactive terminal benchmark — standard-library Python, no numpy — that compares conventional `int64`, **SQLite**, and Smart2Raw on the same dataset: memory, on-disk size, query speed (`SUM`/`COUNT`) and SIMD throughput, driven through the library's real C kernels via `ctypes`. Point it at any integer CSV to see the figures on your machine.

---

## When Smart2Raw does not help much

Smart2Raw is not expected to provide large gains when the data is already dense, random, encrypted, compressed or naturally large.

Examples:

- encrypted data;
- already-compressed files;
- compressed audio;
- compressed video;
- compressed images;
- high-entropy random data;
- raw floating-point data that has not been quantized;
- dense matrix multiplication;
- payloads where every value truly requires 64 bits;
- data already stored as `int8` with no outliers and no need for a container;
- domain-specific queues or formats that are already heavily optimized.

In such cases, Smart2Raw may still provide a format, CRC, mmap or API layer, but the main memory gain may disappear.

---

## Design philosophy

Smart2Raw follows a few rules:

1. Measure before claiming.
2. Separate measured gains from estimates.
3. Do not claim to be a general-purpose compressor.
4. Do not promise speedups for dense computation.
5. Treat mmap mainly as capacity and I/O improvement, not universal acceleration.
6. Acknowledge that mature columnar systems already use related ideas in their own domains.
7. Focus on where a lightweight integer storage layer is actually useful: edge, telemetry, embedded systems, lightweight analytics, services and support operations around larger pipelines.

---

## Build and test

Run the main C test suite:

```sh
bash scripts/build_and_test.sh
```

Expected result:

```text
17 suites OK, 0 failures
```

Build the tools and examples:

```sh
( cd tools && make ) && ( cd examples && make )
```

Run the ports:

```sh
cd ports/go && go test ./...
cd ports/js && npm test
cd ports/python && python -m unittest discover -s tests
```

Run conformance:

```sh
bash conformance/run_conformance.sh
```

The C suite covers functionality (all modules, PFOR, signed PFOR + SIMD, regression, backward compatibility, signed lazy-carry, big-endian COW mmap, analytics, Analytics v2) across multiple build gates: `-O3 -march=native`, `-O2`, no-SIMD (`-DS2R_NO_SIMD`), strict ISO C11 (`-pedantic`), an MCU build with no stdio/mmap/SIMD, plus emulated NEON and big-endian paths and experimental RISC-V RVV and ARM SVE2 paths validated for logic via emulation (CI repeats the ARM/BE paths on real hardware via QEMU; RVV/SVE2 pending real hardware builds).

---

## Repository layout

```text
include/
  smart2raw.h              C header-only library

tools/
  s2r                      main CLI
  s2r_convert              converter
  s2r_verify               verifier
  (+ Makefile)

examples/
  one minimal, runnable example per use case (+ Makefile)

tests/
  C suites: modules, regression, analytics, mmap, SIMD, PFOR,
  plus emulated NEON, RISC-V RVV and ARM SVE2 paths, and big-endian

benchmarks/
  measured experiments + RESULTS.md
  bench_avx512_width.c / bench_format_endtoend.c / bench_format_lanes.c
  maestro/  interactive Python benchmark (int64 / SQLite vs Smart2Raw)

concepts/
  the core idea in a single annotated file

notebooks/
  reproducible experiment notebooks (regenerated from gen_notebooks.py)

bindings/
  language bindings for the C core (e.g. Python ctypes)

ports/
  go/
  js/
  python/

conformance/
  .s2r fixtures
  cross-language format tests

docs/
  .s2r format specification
  Analytics v2 documentation
  technical whitepaper (PDF)

scripts/
  build, test and utility scripts
```

---

## Roadmap

Planned or natural next steps:

- Rust port;
- WebAssembly playground;
- embedded examples for FreeRTOS, Zephyr, ESP32 and STM32;
- Windows memory-mapping backend;
- Android NDK wrapper;
- Swift wrapper for Apple platforms;
- SQLite extension;
- DuckDB adapter;
- Arrow bridge;
- RISC-V scalar validation;
- RISC-V Vector SIMD path (experimental; logic-validated via emulation, pending hardware);
- ARM SVE2 SIMD path (experimental; logic-validated via emulation, pending hardware);
- broader ARM NEON validation on real hardware;
- block-wise `.s2r` serialization;
- sub-byte experimental mode for int4-style use cases.

See [`ROADMAP.md`](ROADMAP.md).

---

## Editions and license

Smart2Raw follows an **open-core** model. The **open edition** — everything in this repository — is licensed under:

```text
AGPL-3.0-or-later
```

A **commercial license** is available for proprietary, closed-source, or AGPL-incompatible use (including closed network services).

See:

```text
LICENSE
LICENSING.md
EDITIONS.md
NOTICE
```

---

## Citation

If you use Smart2Raw in research, benchmarks, papers, reports, products or technical comparisons, please cite the project using [`CITATION.cff`](CITATION.cff).

Releases are archived on Zenodo with versioned DOIs under the concept DOI [10.5281/zenodo.20477234](https://doi.org/10.5281/zenodo.20477234). This release (3.3.7): [10.5281/zenodo.20613701](https://doi.org/10.5281/zenodo.20613701).

---

## One-sentence summary

Smart2Raw is a portable layer for storing and operating on integer data in the smallest native format that preserves the values, reducing memory, cache pressure, bandwidth and I/O whenever the real data range is smaller than the types normally used by default.
