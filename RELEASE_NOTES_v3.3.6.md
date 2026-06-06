# Smart2Raw v3.3.6 - Analytics v2

This release adds the first Analytics v2 layer on top of the compact adaptive integer core.

## Highlights

- C core: `s2r_sort`, `s2r_is_sorted`, `s2r_unique_sorted`, `s2r_nunique`, `s2r_value_counts_u8`.
- Go port: `Sort()`, `IsSorted()`, `UniqueSorted()`, `NUnique()`, `ValueCounts()`.
- JavaScript port: `sort()`, `isSorted()`, `uniqueSorted()`, `nUnique()`, `valueCounts()`.
- Python port: `sort()`, `is_sorted()`, `unique_sorted()`, `nunique()`, `value_counts()`.
- New documentation: `docs/ANALYTICS_V2.md`.
- New conformance-friendly tests across C, Go, JavaScript and Python.

## Validation summary

Functional suites passed for:

- C core;
- Analytics v2 C tests;
- Go port;
- JavaScript port;
- Python port;
- `.s2r` conformance fixtures.

Project DOI: https://doi.org/10.5281/zenodo.20477235

License: AGPL-3.0-or-later, with commercial licensing option.
