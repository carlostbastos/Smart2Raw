# Changelog

Versioning follows SemVer. Dates use the YYYY-MM-DD format.

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
