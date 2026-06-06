# Security Policy

## Supported versions
The 3.3.x line receives security fixes.

## How to report
Please do **not** open a public issue for vulnerabilities. Use the private channel:

- **GitHub Private Vulnerability Reporting** for the repository
  (*Security* tab -> *Report a vulnerability*), at
  https://github.com/carlostbastos/smart2raw/security ; or
- contact via the profile https://github.com/carlostbastos
  (configuring a dedicated security e-mail and listing it here is recommended).

Include a description, the impact and reproduction steps. Initial response within 7 days.

## Sensitive areas
- `s2r_load_portable` / `s2r_map_open` (file parsing, CRC and bounds validation)
  — a natural fuzzing target (planned).
- Arithmetic with overflow guards. (3.3.5 fixed, via AddressSanitizer, an
  overflow in class promotion with an empty pool; there is a regression test.)

Test against the latest version before reporting.
