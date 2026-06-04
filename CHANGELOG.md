# Changelog

Versioning follows SemVer. Dates in YYYY-MM-DD.

## [3.3.5] - 2026-05-31
- Fix: class promotion on an empty pool (`count==0`) did not re-fit the capacity
  to the already-allocated buffer, which could overflow when the first value
  required a wider class than the initial one (e.g. init I8, first value -5e9).
  Affected push_adaptive / push_signed_adaptive / ensure_fits / reclass. Found
  with AddressSanitizer; regression test added (test_regressao, item 6).

## [3.3.4] - 2026-05-31
- Signed PFOR: `s2r_blocked_build_signed` / `get_signed` / `sum_signed`.
- SIMD-accelerated block sum: `s2r_blocked_sum_fast` (each block reuses the
  vpsadbw/NEON dispatch in its own native type). Measured ~6.5-6.7x over scalar.

## [3.3.3] - 2026-05-31
- Block-wise width (PFOR): `S2RBlocked` (`build`/`get`/`sum`/`max`/`bytes`/`free`),
  unsigned. Each block picks its own class; an outlier inflates only its own block
  (~3.7x memory recovery under localized outliers, measured).

## [3.3.2] - 2026-05-31
- Analytics module merged into the single header: bidirectional width / self-
  healing (`s2r_remove_swap`, `s2r_fit_class`), `S2RTracked` (O(1) min/max on
  push) and group-by on the compact form (`s2r_histogram_u8`, `s2r_group_sum_u8u32`).

## [3.3.1] - 2026-05-31
- Signed lazy-carry arithmetic; NEON (ARM) path; big-endian mmap via copy-on-write
  (on-disk file untouched).

## [3.3.0] - 2026-05-31
- Adaptive push (`s2r_push_adaptive`); runtime SIMD dispatch (AVX2 vpsadbw, scalar
  fallback; `s2r_sum_fast`); zero-copy mmap; portable I/O (canonical LE + CRC32).

## [3.2.1] - 2026-05-31
- Robust `s2r_stddev` (no math.h); aligned allocation (C11); signed-aware
  aggregations/filters/statistics.

## [3.2.0]
- Signed integers (S2R_I8..I64); promote/demote; statistics; range queries;
  `push_many`/`transform`; `S2R_FOREACH`; `s2r_info`.
