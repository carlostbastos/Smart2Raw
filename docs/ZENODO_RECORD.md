# Smart2Raw

**Adaptive numeric storage for integer data.**

Smart2Raw is a header-only C library and a portable binary format (`.s2r`) for storing large integer arrays in the smallest native integer width that safely represents the actual data range: 8, 16, 32 or 64 bits, signed or unsigned.

The core idea:

> **Classify once. Operate forever in the most compact native format.**

Instead of defaulting everything to `int64`/`int32`, Smart2Raw scans the real value range and picks the smallest native type that preserves every value. Operations then run directly on the compact representation — sum, min, max, count, filters, statistics, group-by, sort, persistence and memory-mapped reads — without decompression and without manual bit-packing.

The consequence is what separates this from a compressor: because the stored bytes are still native machine integers, a 512-bit register processes 64 of them per instruction instead of 8. **The compact format is not a cost paid at read time; it is what unlocks the width.** Nothing is unpacked, so nothing has to be unpacked before it can be computed on.

This is the canonical record for the project. The full documentation, source, tools, ports and tests are in the accompanying archive and at the source repository.

---

## Upgrade notice: 3.5.0 and 3.4.0

**If you load `.s2r` files you did not write, 3.5.0 and earlier are vulnerable.** `s2r_blocked_load()` sized the body it was about to read as `nblocks × metadata + bytes`. Both terms come off disk, and the sum was done in plain `size_t`. A file declaring `nblocks = 2^22` and a `bytes` near 2^64 makes that sum **wrap** to 16: the reader allocates sixteen bytes and copies four megabytes into them.

That file is 64 bytes long and passes **every** validation that already existed — magic, `fmt`, all four classes, `nblocks == ceil(count/block)`, every field inside its limit, **a correct CRC32 over the real body**, and exact EOF. There is nothing malformed in it. Accidental corruption breaks the CRC; deliberate corruption comes with the right one, and that was the difference left unseen. Confirmed under AddressSanitizer as a 4 MB `heap-buffer-overflow` over a 16-byte region, and as a plain segmentation fault at `-O2`.

Writing was never affected — there both terms describe a structure that already exists in memory. The flat pool was never affected either: it has done `count > SIZE_MAX/eb` since 3.3. One place in the family of readers was missing the guard its siblings had. Fixed in 3.5.1; see below.

3.5.0 also contains a fix for **silent data corruption** present in 3.4.0. A block's frame of reference was held in `int64_t` and the largest base was tracked with a *signed* comparison, even for an unsigned column. A base above `INT64_MAX` read as negative, the bookkeeping class came out too narrow, and the base was **truncated on write**:

```c
uint64_t v[2] = { 1, UINT64_MAX };
s2r_blocked_get(&b, 1)   ->  255        /* should be UINT64_MAX */
```

No error, no warning, no broken CRC — the file is internally consistent with the wrong values. It affects any unsigned column whose range crosses 2^63 and that uses the block-wise layer: 64-bit hashes, hash-derived ids, nanosecond timestamps, wide bit masks. Signed columns were never affected, nor unsigned columns below 2^63, nor the flat pool.

Twenty-five suites of *chosen* cases missed it, because a battery of chosen cases inherits the blind spot of whoever chose them. A new differential fuzz suite — 12 column shapes, random sizes and block sizes, everything checked against a naive reference, with **fixed seeds** — finds it in 9 checks and passes 100,950 on the fix.

## What's new in 3.5.1

A security fix, and nothing else. **No new API, no format change**: a file written by 3.5.0 is read identically by 3.5.1, and the reverse holds too. 3.5.1 does not touch a single byte of the format — it only stopped accepting files 3.5.0 should never have accepted.

The fix is two locks, and neither adds a dependency:

1. **The arithmetic has to close.** `s2r__blk_body_len_ck()` does the multiply and the add in checked arithmetic and hands back a length only when it is real. No compiler builtins — the same guard shape the flat loader already used, so it stays portable to any C99.
2. **The body has to actually be in the file.** The bytes on disk are the only honest witness of how big the body is, and they cost nothing to consult: the file is already open and seekable. A 64-byte `.s2r` can no longer ask for a gigabyte.

**Why 31 suites did not catch it.** Same reason as 3.4.1, different target. The suites test files *written* by the library, and then files *corrupted* byte by byte — the two things that happen on their own. None tested a file built on purpose to be internally consistent and lying.

**The regression test proves what it claims.** `tests/test_format_hardening.c` gained a sixth section (46 checks, was 40): the length that wraps, a length that does not wrap but simply is not in the file, the `nblocks × metadata` multiply overflowing on its own, and — as important as the other three — an honest file written by the library that must still load and still sum correctly, because a guard that also rejects real data is not a fix. Against the 3.5.0 header the section aborts under ASan; against 3.5.1 it passes at `-O2`, `-Os`, in C99 and C11 with `-pedantic -Wall -Wextra`, and under ASan+UBSan.

**Four suite counts became one.** `scripts/build_and_test.sh` runs 31 suites over 21 test files, and 31 is what CI verifies. `.zenodo.json` said 25, `README.md` said 17, `CONTRIBUTING.md` said 15 — all frozen at older releases. They now say 31. `SECURITY.md` no longer claims the supported line is 3.3.x, and gains an explicit threat model: reading a file you did not write is the attack surface.

## Previously, in 3.5.0

**The frame of reference gains a scale.** v3.4.0 removed an *offset* the data did not need. It did not remove a *scale*. A column of `{500, 1500, … 11500}` spans 11000 and therefore takes 14 bits, but every value is `base + 1000·i` with `i` in 0..11 — four bits of index wearing a fourteen-bit coat.

```text
v = base + stride · i        stride = gcd, over the block, of (v − base)
```

The common step is the gcd of the offsets, one pass to find, and dividing it out is exact by construction. **This is not a dictionary**: there is no lookup table and no per-value indirection — the map is a closed-form affine function, so every operation rewrites in the index domain and the stored bytes remain the native integers they always were.

```text
v > t          ⟺   i > (t − base) / stride          integer division
v ∈ [lo,hi]    ⟺   i ∈ [ceil((lo−base)/stride), floor((hi−base)/stride)]
SUM(v)         =   n·base + stride·SUM(i)
```

`stride == 1` is exactly v3.4.0, the same way `base == 0` was exactly v3.3. Measured on 12M elements: **22.89 MB / 1.033 ms → 11.44 MB / 0.468 ms**. A new `fmt = 3` carries the stride array, emitted *only* when a stride above 1 exists — so a strideless column is byte-for-byte the file v3.4.0 wrote, and a v3.4.0 reader still opens it.

**Then the same question was asked of everything else.** An audit of where the library still spent bytes or time the information did not require produced nine more changes:

- **Established order on the flat pool.** The block-wise layer had answered by binary search since v3.4.0; the flat pool ignored order entirely. `S2R_FLAG_SORTED` is cleared by every write, set only by `s2r_sort()` and `s2r_mark_sorted()`, and **kept by an in-order append** — the ingest pattern that matters, since timestamps and monotonic ids arrive sorted. `count_gt` on 8M sorted elements went from 0.371 ms to below the clock: O(n) → O(log n).
- **Healing across the sign boundary.** A column declared signed that never receives a negative sat twice as wide as it needed, because 0..200 does not fit `i8`. `s2r_fit_class_signedness()` heals both width and sign: 15.26 MB → 7.63 MB. Separate entry point on purpose — after healing, a negative push is refused.
- **A constant column carries zero bits.** `S2RAffine` now stores no payload at all: 7.63 MB → 0 bytes, every predicate O(1).
- **The four missing block-wise predicates** — `count_lt`, `count_eq`, `count_range` and `sum_if`, with zone skipping, which is worth more to a bounded window than to a one-sided threshold.
- **The flat pool's zone map.** `s2r_summarize()` records min, max and sum; predicates then refuse or accept from the data's own bounds without reading payload. `count_gt(220)` on a column that stops at 200: **0.1435 ms → 0.000034 ms**.
- **The cumulative index.** A `u8` column has 256 possible values, so the cumulative count of each value answers *any* range predicate exactly, in two array reads, from a structure that does not grow with the data — 2 KB. **4231x**, repaid in 11 queries. Staleness is not left to discipline: the pool carries an epoch, and an index built at another epoch **refuses to answer** rather than answering wrongly.
- **Block size is classified, not guessed.** `s2r_blocked_plan()` prices every candidate from *one* pass — a fine-granularity sweep plus an exact tree merge, exact because the merged step of two neighbours is `gcd(min_i − m, g_i)`. `s2r_blocked_build_auto()` picks. The prediction was verified equal to the built size in every shape-and-candidate combination tested.
- **`s2r_recommend()`**, because the obvious entry point is the worst one: 4M unix timestamps are 15.26 MB and 0.73 ms in a flat pool, against 4.11 MB and 0.04 ms block-wise.
- **"Never expands" became an assertion.** Every classical alternative has a regime where it *grows* the input. Measured on 4M elements against a 30.52 MB `int64` baseline: dictionary encoding of a high-cardinality column stores a dictionary the size of the data (**41.01 MB**, 34% *larger* than raw); RLE on unordered data stores one run per value (30.52 MB, no compression at all); a bitmap only exists when there are two distinct values. Smart2Raw cannot expand, and not by luck — it classifies by **range**, so the worst case is "the range needs 64 bits", which *is* the `int64` input. `benchmarks/format_matrix.c` measures the whole grid and asserts the bound before printing each row.

**Scope corrections.** The material claimed things measurement does not support, and they were removed rather than softened: **"flags" left the use-case list** (a boolean column takes 8x a bitmap's space and popcount answers it ~17x faster than `count_gt`); `fit_class()` does not change signedness and now says so; `S2R_BLOCK_DEFAULT` is documented as a default and not an optimum, being dominated on two of three measured shapes.

**Honest scope.** Affine factoring pays on data with real granularity — fixed sampling intervals, fixed-point money, quantization steps. On arbitrary values the gcd is 1 and the classification is byte-for-byte what v3.4.0 produced. It does not reverse the warehouse benchmark's regime B either: with the stride factored out we are still 2x larger and 1.44x slower than 4-bit dictionary codes, because 12 distinct values need log₂(12) = 3.58 bits and the smallest native class is 8. That absence is the design decision that buys 7.9 ms → 0.00 ms of materialisation, not an oversight.

**31 test suites, 0 failures** (was 25).

## Previously, in 3.4.0

Release 3.4.0 added no new capability. Every item was either a defect that was silently producing a wrong answer, undefined behaviour on a path whose correctness proof *requires* defined wraparound, a place where the C core disagreed with its own ports and its own published specification, or an operation leaving a measured multiple on the table.

The `.s2r` reader was made to obey its own specification — header flags had been adopted verbatim from disk, so a crafted `EXTERNAL` bit leaked every loaded pool and a `READONLY` bit froze it; format version, the reserved byte, class/signed-flag agreement and exact file length are all enforced now, and each of those malformed files had been accepted by C and rejected by at least one port. Two classes of undefined behaviour were removed, both found by adding an ASan+UBSan job to CI. The entire predicate family was vectorised through a single range kernel, because a range test on a w-bit lane is one wrapping subtract plus one unsigned compare — `v ∈ [lo,hi] ⟺ (v − lo) mod 2^w ≤ span` — and the same identity serves signed pools by two's complement, measured at 4.5x–13.4x. The block-wise layer gained the frame of reference (3.9x on unix timestamps, 7.7x on sequential IDs at 9e9, 170x on a constant column), zone statistics answering `SUM`/`MAX`/`MIN` in O(blocks), a sorted-block flag behind a measured size gate, and block-wise `.s2r` serialization. The SVE2 path turned out to be unreachable dead code behind NEON *and* eight times too narrow; it was rewritten around `UDOT`, and RVV widened the same way.

## Previously, in 3.3.7

Release 3.3.7 extended the SIMD layer with a dedicated AVX-512 `u8` sum path (`_mm512_sad_epu8`, 64 bytes per iteration), measured at ~1.17x to ~1.30x over AVX2 and bit-identical to the scalar result. An AVX-512 `u16` kernel was implemented, benchmarked, came out *slower*, and was deliberately not shipped — `u16` stays on AVX2 by measurement.

---

## Why this matters

Many systems store small values in unnecessarily large types: a temperature of 25, an age, an HTTP status code, a counter, a local ID. At small scale this is harmless; at millions or billions of values it costs real RAM, memory bandwidth, cache, I/O and energy. Smart2Raw removes that waste by storing integers in the smallest native width that fits, while keeping direct indexed access.

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
    uint64_t hits  = s2r_count_range_fast(&pool, 100, 500);
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
- **Bidirectional healing** — `fit_class()` reclassifies the width back down after an outlier is removed; since 3.5.0 `fit_class_signedness()` also drops an unearned sign, on request.
- **Safe + lazy-carry arithmetic** — chained operations promote at most once, never wrap silently.
- **SIMD-aware reductions *and* predicates** — sum, min, max, and the whole filter family (`count_gt`/`lt`/`eq`/`range`, `sum_if`, signed and unsigned) through one range kernel, with runtime dispatch to AVX2 and AVX-512 (x86) and NEON (ARM), experimental RISC-V RVV and ARM SVE2 paths, and a portable scalar fallback.
- **Zero-copy mmap** — scan `.s2r` files directly, without loading the whole dataset into RAM.
- **Block-wise width (PFOR) with frame of reference *and scale*** — each block is stored relative to its own minimum and divided by its own common step, so a narrow band at a high base costs the width of the band, and a lattice costs the width of the index.
- **Zone statistics, per block and now for the flat pool** — minimum, span and sum let `SUM`/`MAX`/`MIN` and predicate skipping answer from metadata without touching the payload.
- **A cumulative index for narrow classes** — any range predicate answered exactly in two reads, from 2 KB that does not grow with the data.
- **Automatic representation choice** — `s2r_recommend()` measures the flat, affine and block-wise forms and names the one to use; `s2r_blocked_build_auto()` classifies the block size from the data.
- **Portable `.s2r`** — canonical little-endian + CRC32, identical across the C core and all ports, with the same file rejected by all four implementations for the same reason.

## The `.s2r` file format

Self-describing and portable. All multibyte fields are canonical little-endian; a fixed 16-byte header lets an mmap reader find the payload at a constant offset.

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | magic | `0x33335253` (uint32 LE; bytes `53 52 33 33`, "SR33") |
| 4 | 1 | size | class as signed int8 (negative = signed) |
| 5 | 1 | flags | bit 0 = signed; all other bits reserved and rejected |
| 6 | 1 | fmt | 1 = flat pool, 2 = block-wise column, 3 = block-wise with per-block stride |
| 7 | 1 | rsvd | reserved, must be 0 |
| 8 | 8 | count | number of elements (uint64 LE) |
| 16 | count·eb | payload | elements in canonical LE (eb = abs(size)/8 bytes) |
| 16+n | 4 | crc32 | CRC32 (IEEE 802.3) of the payload (uint32 LE) |

On little-endian hosts the on-disk payload matches memory byte-for-byte (true zero-copy). On big-endian hosts, reads and writes convert, and the CRC is validated over the canonical LE bytes before any conversion. A reader must reject an unsupported `fmt`, a nonzero reserved byte, a class that disagrees with the signed flag, a count that does not match the file length, and any trailing bytes — all four implementations do.

`fmt = 3` is emitted only when some block actually has a stride above 1, so a column without one is byte-for-byte the file v3.4.0 wrote and a v3.4.0 reader still opens it. A v3.5.0 reader accepts both, and reads `fmt = 2` as "every stride is 1" — which is what it means.

## What Smart2Raw cannot do to your data

Every classical alternative has a regime where it **expands** the input. Measured on 4M elements against a 30.52 MB `int64` baseline: dictionary encoding of a high-cardinality column stores a dictionary the size of the data (41.01 MB); RLE on unordered data stores one run per value (30.52 MB); a bitmap only exists when there are two distinct values.

Smart2Raw cannot expand. It classifies by range, so the worst case is "the range needs 64 bits" — which *is* the input. The widest class is the baseline. That bound is asserted in the test suite, and `benchmarks/format_matrix.c` reproduces the whole grid.

## Where it helps

- **IoT / edge** — sensor readings, device states, counters, discrete events on RAM/flash-constrained devices.
- **Telemetry / logs / metrics** — status codes, latency buckets, error codes, local IDs; faster scans, counts, sums and filters. *Not* boolean flags: a two-valued column belongs in a bitmap, which is 8x smaller and ~17x faster here.
- **Time-partitioned columns** — timestamps and monotonic IDs, where the frame of reference turns a 64-bit column into an 8-bit one, and a fixed sampling interval turns it into an index.
- **Lightweight analytics** — a small, dependency-light scan layer for integer columns with small ranges.
- **Feature stores / ML preprocessing** — discrete features, buckets, IDs, quantized columns.
- **AI (storage and scan, not the matmul)** — quantized artifacts, token IDs, integer indices, KV-cache layouts with localized outliers, zero-point correction sums, mmapped quantized data. Smart2Raw does not replace GEMM or tensor cores.
- **Embedded** — small builds with stdio, mmap and SIMD disabled (firmware, MCUs, ESP32/STM32, FreeRTOS/Zephyr).
- **Servers** — workloads bound by memory, cache, bandwidth or I/O.

## Ports and conformance

The canonical implementation is `include/smart2raw.h`. Ports for Go (`ports/go`), JavaScript (`ports/js`) and Python (`ports/python`) read and write the same `.s2r` files. The `conformance/` directory holds canonical fixtures and a cross-language test matrix — every writer against every reader — so `.s2r` is a real portable contract rather than a C-only detail, and the matrix runs in CI.

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
| AVX-512 `u8` sum vs AVX2 | ~1.17–1.30x (Xeon) | 3.3.7 |
| End-to-end `u8`+AVX-512 vs `int64`+scalar | ~13–14x | 3.3.7 |
| 4-way histogram on skewed data | 3.6–4x | core |
| int8 zero-point correction | 2.3x | core |
| Class-based zone-map scan | up to 94.5% bandwidth avoided | core |
| SIMD predicates, unsigned | 4.5–6.0x | 3.4.0 |
| SIMD predicates, signed | 8.6–13.4x | 3.4.0 |
| PFOR frame of reference, unix timestamps | 3.9x memory | 3.4.0 |
| PFOR frame of reference, sequential IDs at 9e9 | 7.7x memory | 3.4.0 |
| PFOR frame of reference, constant column | 170x memory | 3.4.0 |
| Zone `SUM` from metadata (8M elements) | 114x | 3.4.0 |
| Sorted-block `count_gt` at block 65536 | 140x | 3.4.0 |
| **Affine factoring on a stride-bearing column** | **2.00x memory, 2.21x predicate** | **3.5.0** |
| **Predicate on a pool of established order** | **O(n) → O(log n)** | **3.5.0** |
| **Sign healing on a signed column with no negatives** | **2.00x memory** | **3.5.0** |
| **Flat-pool summary pruning a predicate** | **0.1435 ms → 0.000034 ms** | **3.5.0** |
| **Cumulative index, narrow class** | **4231x, 2 KB, repaid in 11 queries** | **3.5.0** |
| **Recommended form vs the obvious entry point** | **3.7x memory, 18x time** | **3.5.0** |
| MCU build without stdio/mmap/SIMD | ~3.4 KB | core |

These numbers are not universal promises; they show where the mechanism works. Where measurement contradicted the design, the design lost: an AVX-512 `u16` kernel was written, measured slower, and deleted rather than shipped; sorted-block binary search was measured at **0.67x** on small blocks and is gated off below a size threshold rather than claimed unconditionally.

## When it does not help much

Encrypted data, already-compressed files, compressed audio/video/images, high-entropy random data, raw un-quantized floating point, dense matrix multiplication, payloads where every value truly needs 64 bits, boolean columns (use a bitmap), and data already stored as `int8` with no outliers. In those cases Smart2Raw may still offer a format, CRC, mmap or API layer, but the memory gain largely disappears.

## Build and test

```sh
bash scripts/build_and_test.sh            # C suite: 31 suites, 0 failures
bash tools/test_cli.sh                    # command-line tools: 19 checks
( cd tools && make ) && ( cd examples && make )
cd ports/go     && go test ./...
cd ports/js     && npm test
cd ports/python && python -m unittest discover -s tests
python bindings/python/test_binding.py
bash conformance/run_conformance.sh
```

The C suite covers all modules, block-wise PFOR (signed and unsigned, frame of reference, affine stride, zone statistics, sorted flags, serialization), the `.s2r` contract, the SIMD predicate family against a scalar reference, a differential fuzz with fixed seeds, regression, backward compatibility, lazy-carry, big-endian COW mmap, analytics — across multiple build gates (`-O3`, `-O2`, no-SIMD, strict ISO C11, an MCU build), plus emulated NEON, RISC-V RVV and ARM SVE2 paths swept across vector lengths. The ARM and big-endian paths are repeated on real hardware via QEMU in CI; RVV and SVE2 remain pending real hardware. CI additionally runs the whole suite under AddressSanitizer and UndefinedBehaviorSanitizer.

**One command reproduces the claim of correctness**, and it is the same command the author runs. A deposit that cannot be checked is a press release; this one ships its own falsifier.

## Editions and license

Smart2Raw follows an open-core model. The open edition — everything in this archive — is licensed under **AGPL-3.0-or-later**. A commercial license is available for proprietary, closed-source or AGPL-incompatible use, including closed network services. See `LICENSE`, `LICENSING.md`, `EDITIONS.md` and `NOTICE`.

## Citation

If you use Smart2Raw in research, benchmarks, papers, reports, products or technical comparisons, please cite it using `CITATION.cff`. Releases are archived on Zenodo with versioned DOIs under the concept DOI **10.5281/zenodo.20477234**, which always resolves to the latest version. This release (3.5.1): **10.5281/zenodo.21676456**. The previous 3.5.0 release is **10.5281/zenodo.21623772** and 3.4.0 is **10.5281/zenodo.21614309**.

## One-sentence summary

Smart2Raw is a portable layer for storing and operating on integer data in the smallest native format that preserves the values, reducing memory, cache pressure, bandwidth and I/O whenever the real data range is smaller than the types normally used by default.
