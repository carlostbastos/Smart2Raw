# Changelog

Versioning follows SemVer. Dates use the YYYY-MM-DD format.

## [3.4.0] - 2026-07-27

A correctness, contract and performance release. Nothing here is a feature in the
sense of "something the library could not express". Every item is either a bug that
was silently producing a wrong answer, undefined behaviour on a path whose
correctness proof requires *defined* wraparound, a place where the C core disagreed
with its own ports and its own specification, or an operation leaving a measured
multiple on the table.

---

### 1. The `.s2r` contract - the reader did not obey the spec the project wrote

- **Header `flags` were adopted verbatim from disk.** A crafted file with bit 2
  (`S2R_FLAG_EXTERNAL`) made `s2r_pool_free` skip the `free` and null the pointer,
  leaking every loaded pool - confirmed with AddressSanitizer. Bit 1
  (`S2R_FLAG_READONLY`) produced a heap pool that silently refused every mutation.
  Load and mmap now mask to the SIGNED bit, and `s2r_save_portable` masks on the
  way out, so in-memory ownership state can never round-trip through a file.
- `s2r_load_portable` / `s2r_map_open` now reject, per `SPEC_s2r_format.md`: an
  unsupported `fmt`, a nonzero reserved byte, a class/signed-flag disagreement, a
  declared count that overflows `size_t`, and a file length different from
  `16 + payload + 4`. **All of these were accepted by C and rejected by at least
  one port**, so `.s2r` was not the portable contract it claimed to be.
- **Go port:** `Load` converted the declared count to a signed `int` and multiplied
  by the element size with no range check. A header claiming
  `count = 0x8000000000000002` at class 16 wrapped to a 4-byte payload, passed the
  length and CRC checks, and decoded as a 2-element pool. The count is now derived
  from the file length by division; the declared value must agree exactly.
- **Go / JS:** the reserved byte is now checked. **JS / Python:** a signed class
  with the flag clear was accepted because `signed` was derived as the OR of class
  and flag, which made the very next agreement check unreachable.

### 2. Undefined behaviour

- `s2r_mul_scalar` at class u16: `a[i] *= v` promotes both operands to `int`, so
  `65436 * 65533` overflowed `INT_MAX`. That is the exact path lazy-carry depends
  on, and its correctness proof requires wraparound to be *defined* in `Z/2^w`.
  Now multiplies through `unsigned`. Found by adding an ASan+UBSan job to CI.
- `s2r_sum_signed`, `s2r_sum_if_signed` and `s2r_blocked_sum_signed` accumulated in
  `int64_t`; summing a large `i64` pool reaches signed overflow easily. Their
  unsigned twins have always accumulated in `uint64_t` for exactly this reason.
  All three now do the same and cast on return - two's-complement addition is
  bit-identical, so the value callers already got is unchanged, it is just defined
  by the standard now.

### 3. Predicate dispatch - `sum_fast` was the only SIMD-dispatched operation

Every filter - `count_gt`, `count_lt`, `count_eq`, `count_range`, `sum_if`, signed
and unsigned - ran scalar, and the signed variants plus unsigned `count_range` /
`sum_if` went through the `s2r_get` accessor, so the width switch sat INSIDE the
loop. On the same `u8` bytes of one pool, `sum_fast` reached 24218 Mval/s while
every filter sat near 4000.

**One kernel serves the whole family.** It reduces to a range test:

    count_gt(t) = count_range(t+1, MAX)    count_lt(t) = count_range(MIN, t-1)
    count_eq(v) = count_range(v, v)

and a range test on a w-bit lane is one WRAPPING subtract plus one unsigned
compare, because for `lo <= hi` with `span = hi - lo < 2^w`:

    v in [lo,hi]   <=>   (v - lo) mod 2^w  <=  span

The same identity serves SIGNED pools with no second kernel: two's-complement
subtraction is bit-identical to unsigned subtraction, so reducing the endpoints
modulo `2^w` and running the unsigned kernel on the raw stored bytes gives the
signed answer. `sum_if` reuses the frame of reference: `SUM(v) = SUM(v-lo) +
lo*count`, and `v-lo` for a match is narrow and unsigned, so `vpsadbw` sums it
directly even for a signed pool.

`count_gt` then earned a dedicated kernel back: serving it through the range form
needs a wrapping subtract *before* the saturating one, and that instruction cost
~28% on a bandwidth-bound 23 MB `u16` column, because `v > t` needs no rebasing -
`subs_epu(v,t) != 0` already IS the predicate. Specialised by measurement.

New: `s2r_count_gt_fast`, `s2r_count_lt_fast`, `s2r_count_eq_fast`,
`s2r_count_range_fast`, `s2r_sum_if_fast` and the five `_signed_fast` twins.
Following the `s2r_sum` / `s2r_sum_fast` precedent the existing names are
unchanged, and every `_fast` result is identical to its scalar counterpart.

Measured, 48M elements, `-O3 -march=native`, AVX2:

| operation | before | after | gain |
|---|---:|---:|---:|
| `count_gt` u8 | 2812 Mval/s | 16848 Mval/s | 6.0x |
| `count_lt` u8 | 3935 | 17658 | 4.5x |
| `count_eq` u8 | 3970 | 18088 | 4.6x |
| `count_range` u8 | 2943 | 15645 | 5.3x |
| `sum_if` u8 | 2881 | 14361 | 5.0x |
| `count_gt` u16 | 2024 | 7453 | 3.7x |
| `count_gt_signed` i8 | 1402 | 18833 | 13.4x |
| `count_range_signed` i8 | 1988 | 17033 | 8.6x |
| `sum_if_signed` i8 | 1532 | 14710 | 9.6x |

AVX2 kernels for the 8- and 16-bit classes, where the compact classes live and
where `sum_fast` also puts its effort; 32/64-bit and non-x86 fall back to typed
hoisted loops. An explicit NEON path is deliberately NOT claimed - it could not be
measured here.

### 4. The block-wise (PFOR) layer

**Frame of reference.** A block's class came from its **maximum alone**:

    block = { 9000000000, 9000000001, ... }  ->  classify(max) = u64

even though the block spans 1. Blocks now store values relative to their own
minimum:

| column (4M elements, block 256) | single width | PFOR | gain |
|---|---:|---:|---:|
| unix timestamps, +-50 s per block | 15.26 MB | 3.89 MB | **3.92x** |
| sequential IDs from 9e9 | 30.52 MB | 3.95 MB | **7.73x** |
| measurement 100000 +- 30 | 15.26 MB | 3.89 MB | **3.92x** |
| constant column | 7.63 MB | 0.04 MB | **170.67x** |
| sensor 20.00..25.00 C | 7.63 MB | 7.67 MB | 0.99x |
| uniform 0..200 (baseline already 0) | 3.81 MB | 3.84 MB | 0.99x |
| random u64 (no structure) | 15.26 MB | 15.33 MB | 1.00x |

The last three rows matter as much as the first three: **the old behaviour is the
special case `base = 0`**, so data that was already optimal stays optimal and
unstructured data is untouched. The existing PFOR suites pass unchanged with the
same ratios they asserted before. A block whose values are all equal now has delta
width 0 and stores **no payload at all**, which is where the 170x comes from, and
`s2r_blocked_sum_fast` now works on signed blocks, which it previously refused.

**Zone statistics.** Each block keeps its true delta span and its sum, so
`s2r_blocked_sum`, `_max` and `_min` are O(nblocks) walks over metadata that never
touch the payload - SUM measured **114x faster** (377.6 us to 3.3 us on 8M
elements). The predicate skip tightens from a *width* bound to the *real* range: a
block of 100..150 was previously treated as reaching 355 because one byte can hold
that. Cost is ~7% of the payload, and every bookkeeping array is stored in the
smallest class that fits its own range.

**Sorted blocks, and the gate they needed.** A non-decreasing block can answer
`count_gt` by binary search. Shipping that unconditionally would have been a
**regression**, which only measurement revealed - on a sawtooth column where every
block straddles the threshold and both sides hold identical payload:

| block size | binary search vs vectorised scan |
|---:|---:|
| 64 | **0.67x** - a loss |
| 256 | 1.21x |
| 1024 | 3.26x |
| 4096 | 10.78x |
| 65536 | **140.30x** |

`log2(n)` dependent probes lose to a sequential, prefetchable, vectorised scan
until the block is big enough, so the search is gated on
`S2R_BLK_BSEARCH_MIN_BYTES` (default 512 = 8 cache lines, overridable). That
removes the small-block loss (0.67x to 1.15x) and keeps the large-block win.

What the sorted flag does *not* buy is worth stating: on a **globally** sorted
column the zone map already resolves nearly every block. It pays when blocks are
individually ordered but the column is not - the shape of time-partitioned or
per-group data.

**Serialization** (ROADMAP: "block-wise `.s2r` serialization"). `s2r_blocked_save`
/ `s2r_blocked_load`, `fmt = 2`, canonical little-endian, CRC32 over **metadata and
payload** so a corrupted zone map is caught, exact file length required. The class
byte is 0, so a v3.3 reader rejects the file on both the class and fmt checks -
correct, because a blocked column is not a flat pool; the reverse is refused too.
On 8M sequential IDs from 9e9 the file is 8.31 MB against 61.04 MB of raw `int64`.

**A bug this found.** The first `s2r_blocked_count_gt` wrote the skip test as
`base + span <= thr`. At delta width 8, `span` is `UINT64_MAX` and the addition
wraps, so blocks that *did* contain matches were silently skipped - `count_gt`
returned 0 where the answer was 30048. Testing `base > thr` first makes
`thr - base` safe and removes the addition entirely.

### 5. SIMD reach

- **ARM SVE2 was unreachable dead code.** SVE implies NEON on AArch64 and the NEON
  block came first in `s2r_sum_fast`, so on real hardware the SVE kernels were
  never called - only the x86 emulation test, which forces SVE2 without NEON, ran
  them. SVE2 is now tested first, behind a `svcntb() > 16` guard so NEON keeps a
  128-bit vector, where the two are at parity.
- **The SVE kernels were also 8x too narrow.** An extending load of one byte per
  64-bit lane consumes `svcntd()` = VL/64 bytes per iteration - 2 bytes at VL=128
  against NEON's fixed 16 - so they lost to NEON on every shipping SVE machine and
  only reached parity at VL=1024. The header comment claiming "at 128-bit SVE the
  width equals NEON" described the intent, not the code. Rewritten around UDOT
  (`svdot_u32` / `svdot_u64`) against a vector of ones, SVE's answer to `vpsadbw`,
  which consumes `svcntb()` = VL/8 bytes per iteration.
- **RVV had the identical problem**: `u8mf8`/`u16mf4` into a `u64m1` accumulator is
  VLEN/64 elements per iteration. Now `u8m1`/`u16m2` into `u64m8` - VLEN/8, 8x
  wider, same overflow-free accumulation and tail-undisturbed policy.
- Both paths remain EXPERIMENTAL: logic validated by emulation, hardware pending.

### Tools, bindings and benchmarks

- `s2r agg sum` on a signed pool used `(int64_t)s2r_sum()`, which only works at
  class 64: `{-1,-128,-5,100}` as `i8` summed to 734 instead of -34. `count-gt` and
  `count-range` parsed the threshold with `strtoull` and called the unsigned
  kernels, so `count-gt 0` over that pool answered 4 instead of 1. Every
  aggregation is now sign-aware, matching `min`/`max`, which already were, and the
  CLI calls the `_fast` variants.
- `s2r info` read `count` from offset 8 for every magic, but pre-3.3 files keep it
  at offset 12, so legacy files reported a nonsense count and expected size; the
  format label was hardcoded to v3.3. Both layouts are handled now.
- `tools/s2r.c`: unchecked `malloc`/`realloc`/`s2r_pool_init` and discarded push
  return codes could produce a silently truncated `.s2r`.
- **ctypes binding**: `signed` was a Python-side attribute that `push()` flipped on
  the first negative value while the C pool stayed unsigned. The signed push was
  rejected, the return code discarded, and the value vanished - after which every
  read used the signed accessors on bytes stored as unsigned
  (`Pool(S2R_8); push(200); push(-1)` left one element reading back as -56).
  Signedness is now read from the pool, a negative push into an unsigned pool
  raises, and push/save/scalar return codes are checked. Added
  `s2r_capi_sum_signed` and `s2r_capi_is_signed` to the C ABI.
- **Python port**: `set()` widened the class before validating the index, so an
  out-of-range `set` changed `byte_length` on its way to raising `IndexError`;
  `get()` accepted negative indices that the JS and Go ports reject; a corrupt
  class byte raised a bare `ValueError` instead of `S2RFormatError`.
- `benchmarks/maestro/` shipped a duplicate of `include/smart2raw.h`, contradicting
  its own README, and the lookup could never reach the repo (`../include` resolves
  to `benchmarks/include/`), so the stale vendored copy was always used. The search
  now tries `../../include/` first; the duplicate is gone.
- New `benchmarks/warehouse/`: Smart2Raw against the dictionary + bit-packing + RLE
  encoding family, with the peer implemented at its best (sorted-dictionary
  predicate pushdown, SIMD nibble unpacker) and our side calling the shipped
  library. It documents where we win, where we tie and where we lose.

### Tests and CI

- **25 suites** (was 17). New: `test_format_hardening.c` (40 checks on the `.s2r`
  contract including the mmap reader), `test_filters_simd.c` (**142,952 checks** -
  for the 8-bit classes it sweeps EVERY threshold and EVERY ordered pair of range
  endpoints against the scalar reference), `test_pfor_frame.c` (**5,472 checks** -
  accessors, aggregates, zone stats, per-block sorted flags, serialization round
  trips, byte-flip corruption, truncation, trailing bytes), plus vector-length
  sweeps running the same SVE/RVV code at VL = 128..1024 bits.
- The PFOR suite runs three times with different binary-search gate settings -
  default, forced off, forced on - because two code paths that must agree are only
  proven to agree if both are executed.
- New `tools/test_cli.sh` (19 checks) and `bindings/python/test_binding.py`
  (16 tests); neither had any coverage before.
- **CI could not fail.** The QEMU job wrote `/tmp/$t | tail -1` without `pipefail`,
  so the pipeline exit status was `tail`'s and a failing or crashing test produced
  a green check - and that is the only job exercising big-endian. Added an
  ASan+UBSan job, jobs for the three ports, and a conformance job; none of the
  ports was built by CI before.

## [3.3.7] - 2026-06-09
- AVX-512: added a dedicated `s2r__sum_u8_avx512` path (uses `_mm512_sad_epu8`, 64 bytes/iter) selected at runtime via `__builtin_cpu_supports("avx512bw")`, ahead of the AVX2 path. Compiled AND run on an AVX-512 Xeon here; result is bit-identical to scalar/AVX2, and measured at ~1.17x (cache-resident) to ~1.30x (memory-bound) over AVX2 and ~12-14x over scalar for u8 sums.
- AVX-512 u16: a u16 AVX-512 kernel was implemented and benchmarked, but it came out SLOWER than the AVX2 u16 path (8 vs 16 elements/iter), so u16 deliberately stays on AVX2. Honest call by measurement; the u16 AVX-512 kernel was removed rather than shipped unused.
- Experimental: RISC-V Vector (RVV 1.0) path for `s2r_sum_fast` (u8/u16), gated by `__riscv_v_intrinsic` (built with `-march=rv64gcv`). Written to the RVV 1.0 C intrinsics; the vector-length-agnostic logic is validated on x86 via an emulation shim (`tests/rvv_emu`, suite "RISC-V RVV emulated"), but it has NOT yet been compiled or run on a RISC-V toolchain/hardware. Scalar fallback unchanged. To be promoted from experimental after an `rv64gcv` build plus a QEMU/hardware run.
- Experimental: ARM SVE2 path for `s2r_sum_fast` (u8/u16), gated by `__ARM_FEATURE_SVE2`. Written to the SVE ACLE intrinsics; logic validated on x86 via an emulation shim (`tests/sve2_emu`, suite "ARM SVE2 emulated"), but NOT yet compiled/run on SVE hardware. Note: at 128-bit SVE the width equals NEON, so the expected gain is marginal. To be promoted after a real SVE build + QEMU/hardware run.
- Test suite is now 17 suites, 0 failures (added the RVV and SVE2 emulated-logic suites).

## [3.3.6] - 2026-06-05
- Analytics v2: added compact integer `sort`, `is_sorted`, `unique_sorted`, `nunique` and `value_counts` primitives to the C core.
- Mirrored the same analytics-v2 API in the Go, JavaScript and Python ports.
- Added C, Go, JavaScript and Python tests for sorted signed/unsigned pools, distinct counts and value-frequency maps.

## [3.3.5] - 2026-05-31
- Fix: class promotion with an empty pool (`count==0`) did not readjust the
  capacity to the already-allocated buffer, which could overflow when the first
  value required a class larger than the initial one (e.g., init I8, first value
  -5e9). Affected push_adaptive / push_signed_adaptive / ensure_fits / reclass.
  Found with AddressSanitizer; regression test added (test_regressao, item 6).

## [3.3.4] - 2026-05-31
- Signed PFOR: `s2r_blocked_build_signed` / `get_signed` / `sum_signed`.
- SIMD-accelerated block-wise sum: `s2r_blocked_sum_fast` (each block reuses the
  vpsadbw/NEON dispatch in its native type). Measured ~6.5-6.7x over scalar.

## [3.3.3] - 2026-05-31
- Block-wise width (PFOR): `S2RBlocked` (`build`/`get`/`sum`/`max`/`bytes`/`free`),
  unsigned. Each block chooses its class; an outlier inflates only its own block
  (~3.7x memory recovered under localized outliers, measured).

## [3.3.2] - 2026-05-31
- Analytics module merged into the single header: bidirectional / self-healing
  width (`s2r_remove_swap`, `s2r_fit_class`), `S2RTracked` (min/max O(1) on push)
  and group-by on the compact data (`s2r_histogram_u8`, `s2r_group_sum_u8u32`).
- Header banner/changelog updated; module index.

## [3.3.1] - 2026-05-31
- Signed lazy-carry arithmetic (`s2r_add/mul_scalar_signed_safe`,
  `S2RDeferredSigned`); NEON path (ARM); big-endian mmap via copy-on-write
  (on-disk file left intact).

## [3.3.0] - 2026-05-31
- Auto-adaptive push (`s2r_push_adaptive`); SIMD with runtime dispatch
  (AVX2 vpsadbw, scalar fallback; `s2r_sum_fast`); zero-copy mmap
  (`s2r_map_open`/`close`); portable I/O (canonical LE + CRC32).

## [3.2.1] - 2026-05-31
- `s2r_stddev` fixed (robust sqrt, no math.h); aligned allocation via
  `aligned_alloc` (C11); signed-aware aggregations/filters/statistics.

## [3.2.0]
- Signed integers (S2R_I8..I64); promote/demote; statistics; range queries;
  `push_many`/`transform`; `S2R_FOREACH`; `s2r_info`.
