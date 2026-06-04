# Security Policy

## Supported versions
The 3.3.x line receives security fixes.

## Reporting a vulnerability
Please do **not** open a public issue for vulnerabilities. Use a private channel:

- GitHub **Private Vulnerability Reporting** for this repository
  (the *Security* tab -> *Report a vulnerability*), at
  https://github.com/carlostbastos/Smart2Raw/security ; or
- email **caterencio@gmail.com**.

Include a description, the impact, and reproduction steps. Initial response within
7 days.

## Sensitive areas
- `s2r_load_portable` / `s2r_map_open` (file parsing, CRC and bounds checks) — a
  natural fuzzing target.
- Overflow-guarded arithmetic. (Version 3.3.5 fixed, via AddressSanitizer, an
  overflow in class promotion on an empty pool; a regression test guards it.)

Please test against the latest version before reporting.
