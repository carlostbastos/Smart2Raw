# Contributing

Thanks for your interest! Simple rules:

1. **Technical honesty.** Only claim performance you have measured; mark
   estimates as estimates. PRs with numbers must include how to reproduce them.
2. **No external dependencies in the header.** `include/smart2raw.h` is
   header-only and must compile cleanly under `-std=c11 -pedantic -Wall -Wextra`.
3. **Tests required.** `scripts/build_and_test.sh` must pass (15 suites,
   0 failures) in every configuration before merging. New features come with
   tests in `tests/` and, if there is a performance number, a benchmark in
   `benchmarks/`.
4. **Portability.** The ARM/big-endian paths are tested under local emulation and
   under QEMU in CI; changes to them must keep both green.
5. **License.** By contributing, you agree to license your contribution under
   AGPL-3.0-or-later and to allow commercial relicensing by the maintainer
   (required for the dual model). See LICENSING.md.

Style: C11, 4 spaces, no tabs; `static inline` functions in the header.
