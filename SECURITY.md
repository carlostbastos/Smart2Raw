# Security Policy

## Supported versions
The current minor line receives security fixes — today, **3.5.x**. The library is
a single header, so upgrading is replacing one file.

## Threat model
Reading a `.s2r` file you did not write is the attack surface. Everything else —
building, querying, saving — operates on structures this library created, and a
caller who never loads a foreign file is not exposed. A `.s2r` reader must
therefore treat every field on disk as hostile, including fields that are
internally consistent and carry a correct CRC: accidental corruption breaks the
CRC, deliberate corruption comes with the right one.

## How to report
Please do **not** open a public issue for vulnerabilities. Use the private channel:

- **GitHub Private Vulnerability Reporting** for the repository
  (*Security* tab -> *Report a vulnerability*), at
  https://github.com/carlostbastos/smart2raw/security ; or
- contact via the profile https://github.com/carlostbastos
  (configuring a dedicated security e-mail and listing it here is recommended).

Include a description, the impact and reproduction steps. Initial response within 7 days.

## Sensitive areas
- `s2r_load_portable` / `s2r_map_open` / `s2r_blocked_load` (file parsing, CRC
  and bounds validation) — a natural fuzzing target (planned).
- Arithmetic with overflow guards. (3.3.5 fixed, via AddressSanitizer, an
  overflow in class promotion with an empty pool; there is a regression test.)

## Fixed
- **3.5.1** — `s2r_blocked_load` computed the body length as
  `nblocks*metadata + bytes` in unchecked `size_t`. A 64-byte file that passes
  every existing validation, CRC included, could make that sum wrap and get a
  4 MB copy into a 16-byte allocation (heap-buffer-overflow under ASan, segfault
  at `-O2`). Fixed with checked arithmetic plus a requirement that the declared
  body actually fit in the file. Regression test:
  `tests/test_format_hardening.c`, section 6.

Test against the latest version before reporting.
