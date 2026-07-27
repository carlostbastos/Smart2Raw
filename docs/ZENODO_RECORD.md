# Smart2Raw

**Adaptive numeric storage for integer data.**

Smart2Raw is a header-only C library and a portable binary format (`.s2r`) for storing large integer arrays in the smallest native integer width that safely represents the actual data range: 8, 16, 32 or 64 bits, signed or unsigned.

The core idea:

> **Classify once. Operate forever in the most compact native format.**

Instead of defaulting everything to `int64`/`int32`, Smart2Raw scans the real value range and picks the smallest native type that preserves every value. Operations then run directly on the compact representation — sum, min, max, count, filters, statistics, group-by, sort, persistence and memory-mapped reads — without decompression and without manual bit-packing.

The consequence is what separates this from a compressor: because the stored bytes are still native machine integers, a 512-bit register processes 64 of them per instruction instead of 8. **The compact format is not a cost paid at read time; it is what unlocks the width.** Nothing is unpacked, so nothing has to be unpacked before it can be computed on.

This is the canonical record for the project. The full documentation, source, tools, ports and tests are in the accompanying archive and at the source repository.

---

## What's new in 3.4.0

Release 3.4.0 adds no new capability. Every item is either a defect that was silently producing a wrong answer, undefined behaviour on a path whose correctness proof *requires* defined wraparound, a place where the C core disagreed with its own ports and its own published specification, or an operation leaving a measured multiple on the table. It is the release where the project audited itself against its own claims.

**The `.s2r` reader now obeys the specification the project wrote.** Header flags were adopted verbatim from disk: a crafted `EXTERNAL` bit made the deallocator skip `free`, leaking every loaded pool (confirmed under AddressSanitizer), and a `READONLY` bit produced a heap pool that silently refused every mutation. Format version, the reserved byte, class/signed-flag agreement, a declared count that overflows `size_t`, and exact file length are all enforced now. Each of those malformed files was **accepted by C and rejected by at least one port**, so `.s2r` was not the portable contract it claimed to be. The Go reader additionally accepted a declared count that wrapped when converted to a signed `int`; the count is now derived from the file length by division and the declared value must agree exactly.

**Two classes of undefined behaviour, removed.** `s2r_mul_scalar` at class `u16` let both operands promote to `int`, so `65436 * 65533` overflowed `INT_MAX` — on precisely the path whose correctness argument depends on wraparound being *defined* in Z/2^w. The signed sum accumulators used `int64_t` where their unsigned twins had always used `uint64_t`. Both were found by adding an ASan+UBSan job to CI. Two's-complement addition is bit-identical, so no caller's answer changes; it is merely defined by the standard now.

**SIMD dispatch for the entire predicate family.** `sum_fast` had been the only vectorised operation: every filter ran scalar, and several went through the generic accessor, putting the width switch *inside* the loop. On the same bytes of one pool, `sum_fast` reached 24,218 Mval/s while every filter sat near 4,000. One kernel now serves the whole family, because a range test on a w-bit lane is a single wrapping subtract plus a single unsigned compare:

> for `lo ≤ hi` with `span = hi − lo < 2^w`:  `v ∈ [lo, hi]` ⟺ `(v − lo) mod 2^w ≤ span`

and `count_gt`, `count_lt` and `count_eq` *are* ranges. The same identity serves **signed** pools with no second kernel — two's-complement subtraction is bit-identical to unsigned subtraction, so reducing the endpoints modulo 2^w and running the unsigned kernel over the raw stored bytes gives the signed answer. `sum_if` reuses the same change of frame, `SUM(v) = SUM(v−lo) + lo·count`, which keeps the summed quantity narrow and unsigned and therefore lets `vpsadbw` sum a *signed* column directly. Measured on 48M elements under AVX2: **4.5x–6.0x** for the unsigned filters, **8.6x–13.4x** for the signed ones.

**Frame of reference in the block-wise (PFOR) layer.** A block's width came from its **maximum alone**, so a block of `{9000000000, 9000000001, …}` was stored as `u64` despite spanning 1. Blocks now store values relative to their own minimum — and the previous behaviour is exactly the special case `base = 0`, so data that was already optimal stays optimal and unstructured data is untouched. Measured on 4M-element columns: **3.9x** on unix timestamps, **7.7x** on sequential IDs based at 9e9, **170x** on a constant column (a block of equal values now has delta width 0 and stores no payload at all), and 0.99–1.00x where the baseline was already zero. Each block additionally keeps its true span and its sum, so `SUM`, `MAX` and `MIN` became O(blocks) walks over metadata that never touch the payload — **114x faster** on 8M elements — and the predicate skip tightened from a *width* bound to the *real* range. Blocks that are individually non-decreasing can answer `count_gt` by binary search, gated on a measured size threshold; shipping that unconditionally would have been a regression (0.67x at block size 64, against 140x at 65536). Block-wise `.s2r` serialization (`fmt = 2`, CRC32 over metadata *and* payload) completes the roadmap item: 8M sequential IDs based at 9e9 occupy 8.31 MB against 61.04 MB of raw `int64`.

**The two experimental vector paths were both unreachable and eight times too narrow.** SVE implies NEON on AArch64 and the NEON block was tested first, so on real hardware the SVE2 kernels were never called — only the x86 emulation test, which forces SVE2 without NEON, ever ran them. They were also written as an extending load of one byte per 64-bit lane, consuming VL/64 bytes per iteration: 2 bytes at VL=128 against NEON's fixed 16, reaching parity only at VL=1024. Both are rewritten — SVE2 around `UDOT` against a vector of ones, which is SVE's answer to `vpsadbw` and consumes VL/8 bytes per iteration, and RVV widened the same way (`u8m1`/`u16m2` into a `u64m8` accumulator). Both remain **experimental**: logic validated by emulation across a swept vector length of 128–1024 bits, real hardware still pending.

**Verification became the deliverable.** The suite went from 17 to **25 suites, 0 failures**, adding 142,952 checks that sweep *every* threshold and *every* ordered pair of range endpoints for the 8-bit classes against a scalar reference, 5,472 checks over the block-wise layer (accessors, aggregates, zone statistics, per-block sorted flags, serialization round trips, byte-flip corruption, truncation, trailing bytes), and 40 checks on the `.s2r` contract including the mmap reader. The block-wise suite is run three times under different binary-search gate settings, because two code paths that must agree are only proven to agree if both are executed. The command-line tools and the Python binding gained their first tests (19 and 16 checks); both were carrying real defects. CI gained an ASan+UBSan job, jobs for the three ports and a conformance job — **and can now fail**, which it could not before: the cross-architecture job piped test output through `tail` without `pipefail`, so a crashing test produced a green check, and that is the only job exercising big-endian.

A new comparison, `benchmarks/warehouse/`, measures Smart2Raw against the encoding family data warehouses are built on — dictionary encoding, bit-packing and RLE — with the peer implemented at its best (sorted-dictionary predicate pushdown, SIMD nibble unpacker) rather than as a strawman. It documents where Smart2Raw wins, where it ties, and where it loses, and includes an explicit honesty ledger.

## Previously, in 3.3.7

Release 3.3.7 extended the SIMD layer and backed the performance story with measured, reproducible benchmarks. A dedicated AVX-512 `u8` sum path (`_mm512_sad_epu8`, 64 bytes per iteration) is selected at runtime ahead of AVX2, measured at ~1.17x (cache-resident) to ~1.30x (memory-bound) over AVX2 and bit-identical to the scalar result. An AVX-512 `u16` kernel was implemented, benchmarked, came out *slower* than the AVX2 `u16` path, and was deliberately not shipped — `u16` stays on AVX2 by measurement. End-to-end, summing compact `u8` with AVX-512 measured ~13–14x over a conventional `int64`+scalar baseline, most of which comes from the compact format letting a 512-bit register process 64 values per instruction instead of 8.

## Previously, in 3.3.6

Release 3.3.6 added Analytics v2 — compact integer primitives useful before reaching for a full columnar layer (`s2r_sort`, `s2r_is_sorted`, `s2r_unique_sorted`, `s2r_nunique`, `s2r_value_counts_u8`), implemented in the C core and mirrored across the Go, JavaScript and Python ports, with a shared `.s2r` contract verified by conformance tests.

---

## Why this matters

Many systems store small values in unnecessarily large types: a temperature of 25, an age, an HTTP status code, a counter, a flag, a local ID. At small scale this is harmless; at millions or billions of values it costs real RAM, memory bandwidth, cache, I/O and energy. Smart2Raw removes that waste by storing integers in the smallest native width that fits, while keeping direct indexed access.

## What it is, and what it is not

**It is:** a header-only C library (`include/smart2raw.h`, C11, no dependencies); a portable `.s2r` format (canonical little-endian + CRC32); a compact integer storage layer; a lightweight scan/aggregation engine; a base for ports in other languages. It runs from servers to edge and embedded systems.

**It is not:** a general-purpose compressor; a codec for audio, video or images; a format for encrypted or already-compressed data; a dense linear-algebra engine or a replacement for GEMM/BLAS/tensor cores; a floating-point quantizer; or a full columnar database. It works best when the data is integer-based, the real range is smaller than the declared type, and the bottleneck is memory, bandwidth, cache, scanning, I/O or aggregation.

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
    uint64_t hits  = s2r_count_range_fast(&pool, 100, 500);   /* new in 3.4.0 */
    (void)total; (void)hits;
    s2r_save_portable(&pool, "data.s2r");       /* portable LE + CRC32 */
    s2r_pool_free(&pool);
    return 0;
}
```

```sh
cc -O2 -std=c11 -I include quickstart.c -o quickstart && ./quickstart
```

`push_adaptive` promotes the pool automatically (`u8 -> u16 -> u32 -> u64`) when a value no longer fits. No silent truncation.

## Key features

- **Adaptive width** — the smallest class (`u8`/`u16`/`u32`/`u64`, `i8`/`i16`/`i32`/`i64`) that fits the real range.
- **Adaptive push** — the pool grows its class automatically instead of overflowing.
- **Bidirectional healing** — `fit_class()` reclassifies back down after an outlier is removed.
- **Safe + lazy-carry arithmetic** — chained operations promote at most once, never wrap silently.
- **SIMD-aware reductions *and* predicates** — sum, min, max, and since 3.4.0 the whole filter family (`count_gt`/`lt`/`eq`/`range`, `sum_if`, signed and unsigned) through one range kernel, with runtime dispatch to AVX2 and AVX-512 (x86) and NEON (ARM), experimental RISC-V RVV and ARM SVE2 paths, and a portable scalar fallback.
- **Zero-copy mmap** — scan `.s2r` files directly, without loading the whole dataset into RAM.
- **Block-wise width (PFOR) with frame of reference** — each block is stored relative to its own minimum, so an outlier inflates only its own block and a narrow band at a high base costs the width of the band, not of the base.
- **Zone statistics** — per-block minimum, span and sum let `SUM`/`MAX`/`MIN` and predicate skipping answer from metadata without touching the payload.
- **Lightweight analytics** — sum, min, max, mean, variance, stddev, filters, range queries, histograms, group-by, sort, distinct and value counts; signed-aware throughout.
- **Portable `.s2r`** — canonical little-endian + CRC32, identical across the C core and all ports, with the same file rejected by all four implementations for the same reason.

## The `.s2r` file format

Self-describing and portable. All multibyte fields are canonical little-endian; a fixed 16-byte header lets an mmap reader find the payload at a constant offset.

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | magic | `0x33335253` (uint32 LE; bytes `53 52 33 33`, "SR33") |
| 4 | 1 | size | class as signed int8 (negative = signed) |
| 5 | 1 | flags | bit 0 = signed; all other bits reserved and rejected |
| 6 | 1 | fmt | format version (1 = flat pool, 2 = block-wise column) |
| 7 | 1 | rsvd | reserved, must be 0 |
| 8 | 8 | count | number of elements (uint64 LE) |
| 16 | count·eb | payload | elements in canonical LE (eb = abs(size)/8 bytes) |
| 16+n | 4 | crc32 | CRC32 (IEEE 802.3) of the payload (uint32 LE) |

On little-endian hosts the on-disk payload matches memory byte-for-byte (true zero-copy). On big-endian hosts, reads and writes convert, and the CRC is validated over the canonical LE bytes before any conversion. A reader must reject an unsupported `fmt`, a nonzero reserved byte, a class that disagrees with the signed flag, a count that does not match the file length, and any trailing bytes — as of 3.4.0 all four implementations do.

## Where it helps

- **IoT / edge** — sensor readings, device states, counters, discrete events on RAM/flash-constrained devices.
- **Telemetry / logs / metrics** — status codes, latency buckets, error codes, flags, local IDs; faster scans, counts, sums and filters.
- **Time-partitioned columns** — timestamps and monotonic IDs, where the frame of reference turns a 64-bit column into an 8-bit one.
- **Lightweight analytics** — a small, dependency-light scan layer for integer columns with small ranges.
- **Feature stores / ML preprocessing** — discrete features, buckets, IDs, quantized columns.
- **AI (storage and scan, not the matmul)** — quantized artifacts, token IDs, integer indices, KV-cache layouts with localized outliers, zero-point correction sums, mmapped quantized data. Smart2Raw does not replace GEMM or tensor cores.
- **Embedded** — small builds with stdio, mmap and SIMD disabled (firmware, MCUs, ESP32/STM32, FreeRTOS/Zephyr).
- **Servers** — workloads bound by memory, cache, bandwidth or I/O.

## Ports and conformance

The canonical implementation is `include/smart2raw.h`. Ports for Go (`ports/go`), JavaScript (`ports/js`) and Python (`ports/python`) read and write the same `.s2r` files. The `conformance/` directory holds canonical fixtures and a cross-language test matrix — every writer against every reader — so `.s2r` is a real portable contract rather than a C-only detail. As of 3.4.0 the conformance matrix runs in CI.

```sh
python3 conformance/scripts/generate_fixtures.py
bash conformance/run_conformance.sh
```

## Measured results

Speedups depend on hardware, compiler, data distribution, working-set size and baseline. Reproduce with `scripts/reproduce.sh` and the programs in `benchmarks/`; the machine and method are recorded in `benchmarks/RESULTS.md`. Server-capacity figures elsewhere in the project are clearly labeled **model estimates**, not server measurements.

| Mechanism | Measured result | Since |
|---|---:|---|
| Memory vs `int64` for 8–16 bit data | 75–87.5% less | core |
| Min/max reductions with vectorization | 8–10x | core |
| SIMD `u8` sum (`vpsadbw`) | 2.8–10x | core |
| SIMD `u16` sum | 1.4–4x | core |
| AVX-512 `u8` sum vs AVX2 | ~1.17–1.30x (Xeon) | 3.3.7 |
| End-to-end `u8`+AVX-512 vs `int64`+scalar | ~13–14x | 3.3.7 |
| 4-way histogram on skewed data | 3.6–4x | core |
| `u8 -> u32` group sum | ~1.78 G rows/s | core |
| int8 zero-point correction | 2.3x | core |
| SIMD block sum (zero-point support) | ~6.7x | core |
| Class-based zone-map scan | up to 94.5% bandwidth avoided | core |
| **SIMD predicates, unsigned** (`count_gt`/`lt`/`eq`/`range`, `sum_if`) | **4.5–6.0x** | **3.4.0** |
| **SIMD predicates, signed** (same family) | **8.6–13.4x** | **3.4.0** |
| **PFOR frame of reference**, unix timestamps | **3.9x** memory | **3.4.0** |
| **PFOR frame of reference**, sequential IDs at 9e9 | **7.7x** memory | **3.4.0** |
| **PFOR frame of reference**, constant column | **170x** memory | **3.4.0** |
| **Zone `SUM` from metadata** (8M elements) | **114x** | **3.4.0** |
| **Sorted-block `count_gt`** at block 65536 | **140x** | **3.4.0** |
| Block-wise width with 0.01% outliers | ~3.7x memory recovery | core |
| MCU build without stdio/mmap/SIMD | ~3.4 KB | core |

These numbers are not universal promises; they show where the mechanism works. Where measurement contradicted the design, the design lost: an AVX-512 `u16` kernel was written, measured slower, and deleted rather than shipped; sorted-block binary search was measured at **0.67x** on small blocks and is gated off below a size threshold rather than claimed unconditionally.

## When it does not help much

Encrypted data, already-compressed files, compressed audio/video/images, high-entropy random data, raw un-quantized floating point, dense matrix multiplication, payloads where every value truly needs 64 bits, and data already stored as `int8` with no outliers. In those cases Smart2Raw may still offer a format, CRC, mmap or API layer, but the memory gain largely disappears.

## Build and test

```sh
bash scripts/build_and_test.sh            # C suite: 25 suites, 0 failures
bash tools/test_cli.sh                    # command-line tools: 19 checks
( cd tools && make ) && ( cd examples && make )
cd ports/go     && go test ./...
cd ports/js     && npm test
cd ports/python && python -m unittest discover -s tests
python bindings/python/test_binding.py
bash conformance/run_conformance.sh
```

The C suite covers all modules, block-wise PFOR (signed and unsigned, frame of reference, zone statistics, sorted flags, serialization), the `.s2r` contract, the SIMD predicate family against a scalar reference, regression, backward compatibility, lazy-carry, big-endian COW mmap, analytics and Analytics v2 — across multiple build gates (`-O3`, `-O2`, no-SIMD, strict ISO C11, an MCU build), plus emulated NEON, RISC-V RVV and ARM SVE2 paths swept across vector lengths, and a big-endian path. The ARM and big-endian paths are repeated on real hardware via QEMU in CI; RVV and SVE2 remain pending real hardware. On an AVX-512 host the AVX-512 path runs for real and is checked against the scalar result. CI additionally runs the whole suite under AddressSanitizer and UndefinedBehaviorSanitizer.

**One command reproduces the claim of correctness**, and it is the same command the author runs. A deposit that cannot be checked is a press release; this one ships its own falsifier.

## Editions and license

Smart2Raw follows an open-core model. The open edition — everything in this archive — is licensed under **AGPL-3.0-or-later**. A commercial license is available for proprietary, closed-source or AGPL-incompatible use, including closed network services. See `LICENSE`, `LICENSING.md`, `EDITIONS.md` and `NOTICE`.

## Citation

If you use Smart2Raw in research, benchmarks, papers, reports, products or technical comparisons, please cite it using `CITATION.cff`. Releases are archived on Zenodo with versioned DOIs under the concept DOI **10.5281/zenodo.20477234**, which always resolves to the latest version. This release (3.4.0): **10.5281/zenodo.21614309**.

## One-sentence summary

Smart2Raw is a portable layer for storing and operating on integer data in the smallest native format that preserves the values, reducing memory, cache pressure, bandwidth and I/O whenever the real data range is smaller than the types normally used by default.
