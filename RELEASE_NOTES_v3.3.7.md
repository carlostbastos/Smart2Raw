# Smart2Raw v3.3.7 - Wider SIMD and a measured performance story

This release extends the SIMD layer and backs the performance claims with measured,
reproducible benchmarks. DOI for this version: 10.5281/zenodo.20613701
(concept DOI, all versions: 10.5281/zenodo.20477234).

## Highlights

- **AVX-512 `u8` sum** — dedicated path (`_mm512_sad_epu8`, 64 bytes/iter), chosen at
  runtime ahead of AVX2. Measured on an AVX-512 Xeon at ~1.17x (cache-resident) to
  ~1.30x (memory-bound) over AVX2, bit-identical to scalar. A `u16` AVX-512 kernel was
  benchmarked, came out slower than AVX2, and was deliberately not shipped: `u16` stays
  on AVX2 by measurement.
- **RISC-V Vector (RVV 1.0) path - experimental.** Written to the RVV 1.0 intrinsics,
  gated by `__riscv_v_intrinsic`. Logic validated on x86 via `tests/rvv_emu`; pending an
  `rv64gcv` build + QEMU/hardware run.
- **ARM SVE2 path - experimental.** Written to the SVE ACLE intrinsics, gated by
  `__ARM_FEATURE_SVE2`. Logic validated via `tests/sve2_emu`; pending real SVE hardware.
  At 128-bit SVE the width equals NEON, so the expected gain is marginal.
- **New reproducible benchmarks** under `benchmarks/`: `bench_avx512_width.c`,
  `bench_format_endtoend.c`, `bench_format_lanes.c`. End-to-end, summing compact `u8`
  with AVX-512 measured ~13-14x over the conventional `int64`+scalar baseline.

## Validation summary

- C test suite: 17 suites, 0 failures (added the RVV and SVE2 emulated-logic suites).
- AVX-512 path compiled and run on x86 (result identical to scalar/AVX2).
- RVV and SVE2 paths are gated off by default and do not affect other targets.

## Honest scope

The AVX-512 numbers are measured on one machine and are not universal promises. The RVV
and SVE2 paths are experimental: their logic is validated by emulation, not yet on
RISC-V/SVE hardware.
