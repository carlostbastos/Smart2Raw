# Smart2Raw GitHub Package Manifest

Package: Smart2Raw v3.4.0
DOI: https://doi.org/10.5281/zenodo.20477234 (concept DOI, resolves to latest; v3.4.0: 10.5281/zenodo.21614309; v3.3.7: 10.5281/zenodo.20613701)
License: AGPL-3.0-or-later, with commercial licensing option

## Included

- C header-only core: `include/smart2raw.h`
- C tools source: `tools/*.c`
- Tests and benchmarks
- Go port: `ports/go`
- JavaScript port: `ports/js`
- Python port: `ports/python`
- `.s2r` conformance fixtures and scripts
- Documentation, roadmap, release notes and citation metadata
- Full AGPLv3 license text, NOTICE and licensing files

## Not included

Prebuilt platform-specific binaries are intentionally not included. Build tools from source with `make` in the relevant directories.

## Validation

Run `bash scripts/build_and_test.sh` for the C suite, `bash conformance/run_conformance.sh` for the cross-language `.s2r` checks, and the per-port test commands listed in `README.md`.
