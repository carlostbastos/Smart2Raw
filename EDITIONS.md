# Editions

| Capability | Open edition (AGPLv3) | Advanced edition (commercial) |
|---|:--:|:--:|
| Adaptive core (±8/16/32/64), adaptive push | yes | yes |
| Aggregations + SIMD (AVX2 vpsadbw / NEON), `sum_fast` | yes | yes |
| Statistics, filters, range queries, analytics, group-by | yes | yes |
| Block-wise width (PFOR), signed and unsigned, SIMD block sum | yes | yes |
| Zero-copy mmap + portable I/O (`.s2r`, CRC32) | yes | yes |
| CLI, verifier, converter, Python binding | yes | yes |
| Advanced kernels and proprietary tooling | — | yes |

The **open edition** is everything in this repository. The **advanced edition**
adds capabilities that are distributed only under a commercial license.

Commercial contact: **caterencio@gmail.com**
