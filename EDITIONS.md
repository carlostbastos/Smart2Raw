# Smart2Raw Editions

| Feature | Open edition (AGPLv3) | Advanced edition (commercial) |
|---|:--:|:--:|
| Auto-adaptive core (±8/16/32/64), adaptive push | yes | yes |
| Aggregations + SIMD (AVX2 vpsadbw / NEON), `sum_fast` | yes | yes |
| Statistics, filters, range queries, analytics, group-by | yes | yes |
| Block-wise width (PFOR), signed and unsigned, SIMD block sum | yes | yes |
| Zero-copy mmap + portable I/O (`.s2r`, CRC32) | yes | yes |
| CLI, verifier, converter, Python bindings | yes | yes |

> The open edition (this repository) includes everything listed above. The
> advanced edition is offered under a commercial license and may add features
> beyond the open edition. See the commercial contact below.

Commercial contact: https://github.com/carlostbastos
