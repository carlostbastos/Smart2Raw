# Contributing

Thanks for your interest! A few simple rules:

1. **Technical honesty.** Only claim performance you measured; label estimates as
   estimates. PRs with numbers must include how to reproduce them.
2. **No external dependencies in the header.** `include/smart2raw.h` is
   header-only and must compile cleanly under `-std=c11 -pedantic -Wall -Wextra`.
3. **Tests are mandatory.** `scripts/build_and_test.sh` must pass (14 suites, 0
   failures) in every configuration before a merge. New features come with tests
   in `tests/`, and with a benchmark in `benchmarks/` if they carry a number.
4. **Portability.** ARM/big-endian paths are tested in local emulation and on real
   architectures via QEMU in CI; changes there must keep both green.
5. **License.** By contributing, you agree to license your contribution under
   AGPL-3.0-or-later and to allow commercial relicensing by the maintainer
   (required for the dual model). See LICENSING.md.

Style: C11, 4 spaces, no tabs; `static inline` functions in the header.
