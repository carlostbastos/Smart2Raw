# Smart2Raw

**Adaptive numeric storage for integer data.**

Smart2Raw is a header-only C library and a portable binary format (`.s2r`) for storing large integer arrays using the smallest native integer width that safely represents the actual data range: 8, 16, 32 or 64 bits, signed or unsigned.

The core idea:

> Classify once. Operate forever in the most compact native format.

Instead of defaulting everything to `int64`/`int32`, Smart2Raw scans the real value range and picks the smallest native type that preserves every value. Operations then run directly on the compact representation — sum, min, max, count, filters, statistics, group-by, sort, persistence and memory-mapped reads — without decompression and without manual bit-packing.

This is the canonical record for the project. The full documentation, source, tools, ports and tests are in the accompanying archive and at the source repository.

## What's new in 3.3.7

Release 3.3.7 extends the SIMD layer and backs the performance story with measured, reproducible benchmarks. Each gain is labeled as **measured** or **experimental**, never inflated.

- **AVX-512 `u8` sum (measured).** A dedicated path (`_mm512_sad_epu8`, 64 bytes per iteration) is selected at runtime ahead of AVX2. Measured on an AVX-512 Xeon at **~1.17x (cache-resident) to ~1.30x (memory-bound) over AVX2**, and bit-identical to the scalar result. A `u16` AVX-512 kernel was implemented and benchmarked, came out slower than the AVX2 `u16` path (8 vs 16 elements per iteration), and was deliberately not shipped — `u16` stays on AVX2 by measurement.
- **RISC-V Vector (RVV 1.0) path — experimental.** Written to the RVV 1.0 intrinsics, gated by `__riscv_v_intrinsic`. Its vector-length-agnostic logic is validated on x86 via an emulation shim (`tests/rvv_emu`); it is pending a real `rv64gcv` build plus a QEMU/hardware run before being promoted from experimental.
- **ARM SVE2 path — experimental.** Written to the SVE ACLE intrinsics, gated by `__ARM_FEATURE_SVE2`. Its logic is validated via `tests/sve2_emu`, pending real SVE hardware. At 128-bit SVE the width equals NEON, so the expected gain is marginal.
- **New reproducible benchmarks** under `benchmarks/`: `bench_avx512_width.c` (instruction width, format held constant), `bench_format_endtoend.c` (conventional `int64` vs compact `u8`) and `bench_format_lanes.c` (the compact format unlocking SIMD lanes). End-to-end, summing compact `u8` with AVX-512 measured **~13–14x** over the conventional `int64`+scalar baseline on this machine — most of which comes from the compact format letting a 512-bit register process 64 values per instruction instead of 8.
- **Test suite is now 17 suites, 0 failures** (added the RVV and SVE2 emulated-logic suites).

Honest scope: the AVX-512 numbers are measured on one machine and are not universal promises; the RVV and SVE2 paths are experimental, validated by emulation rather than on RISC-V/SVE hardware, and are gated off by default so they never affect other targets.

## Previously, in 3.3.6

Release 3.3.6 added **Analytics v2** — compact integer primitives useful before reaching for a full columnar layer (`s2r_sort`, `s2r_is_sorted`, `s2r_unique_sorted`, `s2r_nunique`, `s2r_value_counts_u8`), implemented in the C core and mirrored across the Go, JavaScript and Python ports, with a shared `.s2r` contract verified by conformance tests.

## Why this matters

Many systems store small values in unnecessarily large types: a temperature of 25, an age, an HTTP status code, a counter, a flag, a local ID. At small scale this is harmless; at millions or billions of values it costs real RAM, memory bandwidth, cache, I/O and energy. Smart2Raw removes that waste by storing integers in the smallest native width that fits, while keeping direct indexed access.

## What it is, and what it is not

It **is**: a C header-only library (`include/smart2raw.h`, C11, no dependencies); a portable `.s2r` format (canonical little-endian + CRC32); a compact integer storage layer; a lightweight scan/aggregation engine; a base for ports in other languages. It runs from servers to edge and embedded systems.

It **is not**: a general-purpose compressor; a codec for audio/video/images; a format for encrypted or already-compressed data; a dense linear-algebra engine or a replacement for GEMM/BLAS/tensor cores; a floating-point quantizer; or a full columnar database. It works best when the data is integer-based, the real range is smaller than the declared type, and the bottleneck is memory, bandwidth, cache, scanning, I/O or aggregation.

## Quickstart (C)

Header-only — include it and compile with any C11 compiler.

```c
#include "smart2raw.h"

int main(void) {
    S2RPool pool;
    s2r_pool_init(&pool, S2R_8, 1024);          /* start at u8 */

    for (uint64_t v = 0; v < 1000; v++)
        s2r_push_adaptive(&pool, v);            /* promotes u8 -> u16 -> ... as needed */

    uint64_t total = s2r_sum_fast(&pool);       /* SIMD-aware reduction */
    (void)total;

    s2r_save_portable(&pool, "data.s2r");       /* portable LE + CRC32 */
    s2r_pool_free(&pool);
    return 0;
}
```

```
cc -O2 -std=c11 -I include quickstart.c -o quickstart && ./quickstart
```

`push_adaptive` promotes the pool automatically (`u8 -> u16 -> u32 -> u64`) when a value no longer fits. No silent truncation.

## Key features

- **Adaptive width** — the smallest class (u8/u16/u32/u64, i8/i16/i32/i64) that fits the real range.
- **Adaptive push** — the pool grows its class automatically instead of overflowing.
- **Bidirectional healing** — `fit_class()` reclassifies back down after an outlier is removed.
- **Safe + lazy-carry arithmetic** — chained operations promote at most once, never wrap silently.
- **SIMD-aware reductions** — runtime dispatch to AVX2 and AVX-512 (x86) and NEON (ARM), with experimental RISC-V RVV and ARM SVE2 paths and a portable scalar fallback.
- **Zero-copy mmap** — scan `.s2r` files directly, without loading the whole dataset into RAM.
- **Block-wise width (PFOR)** — an outlier inflates only its own block, not the whole array.
- **Lightweight analytics** — sum, min, max, mean, variance, stddev, filters, range queries, histograms, group-by, sort, distinct/value counts; signed-aware throughout.
- **Portable `.s2r`** — canonical little-endian + CRC32, identical across the C core and all ports.

## The .s2r file format

Self-describing and portable. All multibyte fields are canonical little-endian; a fixed 16-byte header lets an mmap reader find the payload at a constant offset.

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | magic | `0x33335253` (uint32 LE; bytes 53 52 33 33, "SR33") |
| 4 | 1 | size | class as signed int8 (negative = signed) |
| 5 | 1 | flags | bit 0 = signed |
| 6 | 1 | fmt | format version (1) |
| 7 | 1 | rsvd | reserved (0) |
| 8 | 8 | count | number of elements (uint64 LE) |
| 16 | count·eb | payload | elements in canonical LE (eb = abs(size)/8 bytes) |
| 16+n | 4 | crc32 | CRC32 (IEEE 802.3) of the payload (uint32 LE) |

On little-endian hosts the on-disk payload matches memory byte-for-byte (true zero-copy). On big-endian hosts, reads/writes convert and the CRC is validated over the canonical LE bytes before any conversion.

## Where it helps

Smart2Raw is useful whenever you store many integers that occupy more space than they need:

- **IoT / edge** — sensor readings, device states, counters, discrete events on RAM/flash-constrained devices.
- **Telemetry / logs / metrics** — status codes, latency buckets, error codes, flags, local IDs; faster scans, counts, sums and filters.
- **Lightweight analytics** — a small, dependency-light scan layer for integer columns with small ranges.
- **Feature stores / ML preprocessing** — discrete features, buckets, IDs, quantized columns.
- **AI (storage/scan, not the matmul)** — quantized artifacts, token IDs, integer indices, KV-cache layouts with localized outliers, zero-point correction sums, mmapped quantized data. Smart2Raw does not replace GEMM/tensor cores.
- **Embedded** — small builds with stdio/mmap/SIMD disabled (firmware, MCUs, ESP32/STM32, FreeRTOS/Zephyr).
- **Servers** — workloads bound by memory, cache, bandwidth or I/O.

The canonical implementation is C; ports exist to carry the same idea into other ecosystems.

## Ports and conformance

The canonical implementation is `include/smart2raw.h`. Ports for Go (`ports/go`), JavaScript (`ports/js`) and Python (`ports/python`) read and write the same `.s2r` files. The `conformance/` directory holds canonical fixtures and a cross-language test matrix (every writer against every reader), so `.s2r` is a real portable contract rather than a C-only detail.

```
python3 conformance/scripts/generate_fixtures.py
bash conformance/run_conformance.sh
```

## Measured results

Speedups depend on hardware, compiler, data distribution, working-set size and baseline. Reproduce with `scripts/reproduce.sh` and the three programs in `benchmarks/`; the machine and method are recorded in `benchmarks/RESULTS.md`. Server-capacity figures elsewhere in the project are clearly labeled model estimates, not server measurements.

| Mechanism | Measured result |
|---|---|
| Memory vs `int64` for 8–16 bit data | 75–87.5% less |
| Min/max reductions with vectorization | 8–10x |
| SIMD `u8` sum (`vpsadbw`) | 2.8–10x |
| SIMD `u16` sum | 1.4–4x |
| AVX-512 `u8` sum vs AVX2 | ~1.17–1.30x (measured, Xeon) |
| End-to-end `u8`+AVX-512 vs `int64`+scalar | ~13–14x (measured) |
| 4-way histogram on skewed data | 3.6–4x |
| int8 zero-point correction | 2.3x |
| Block-wise width with 0.01% outliers | ~3.7x memory recovery |
| SIMD block sum (zero-point support) | ~6.7x |
| Class-based zone-map scan | up to 94.5% bandwidth avoided (measured scenario) |
| MCU build without stdio/mmap/SIMD | ~3.4 KB |

## When it does not help much

Encrypted data, already-compressed files, compressed audio/video/images, high-entropy random data, raw (un-quantized) floating point, dense matrix multiplication, payloads where every value truly needs 64 bits, and data already stored as int8 with no outliers. In those cases Smart2Raw may still offer a format, CRC, mmap or API layer, but the memory gain largely disappears.

## Build and test

```
bash scripts/build_and_test.sh            # C suite: 17 suites, 0 failures
( cd tools && make ) && ( cd examples && make )
cd ports/go && go test ./...
cd ports/js && npm test
cd ports/python && python -m unittest discover -s tests
bash conformance/run_conformance.sh
```

The C suite covers all modules, PFOR (signed/unsigned), regression, backward compatibility, lazy-carry, big-endian COW mmap, analytics and Analytics v2, across multiple build gates (-O3, -O2, no-SIMD, strict ISO C11, an MCU build), plus emulated NEON, RISC-V RVV and ARM SVE2 paths and a big-endian path (the ARM/BE paths are repeated on real hardware via QEMU in CI; RVV and SVE2 are pending real hardware). On an AVX-512 host the AVX-512 path runs for real and is checked against the scalar result.

## Editions and license

Smart2Raw follows an open-core model. The open edition — everything in this archive — is licensed under AGPL-3.0-or-later. A commercial license is available for proprietary, closed-source, or AGPL-incompatible use (including closed network services). See LICENSE, LICENSING.md, EDITIONS.md and NOTICE.

## Citation

If you use Smart2Raw in research, benchmarks, papers, reports, products or technical comparisons, please cite it using `CITATION.cff`. Releases are archived on Zenodo with versioned DOIs under the concept DOI **10.5281/zenodo.20477234**. This release (3.3.7): **10.5281/zenodo.20613701**.

## One-sentence summary

Smart2Raw is a portable layer for storing and operating on integer data in the smallest native format that preserves the values, reducing memory, cache pressure, bandwidth and I/O whenever the real data range is smaller than the types normally used by default.
