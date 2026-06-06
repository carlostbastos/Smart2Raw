# Measured results

**Machine:** Intel(R) Xeon(R) Processor @ 2.10GHz ·
caches L1d 48 KB, L2 2 MB, L3 260 MB · gcc 13 · x86-64 little-endian.
**How to reproduce:** `scripts/reproduce.sh`.

| Mechanism | Measured |
|---|---|
| Memory vs int64 (8–16 bits) | 75–87% less |
| Cache cliff (max, u8 vs int64) | 7.6–22.8× (peak in the L1/L2 window) |
| SIMD u8 sum (vpsadbw) | 2.8–10× |
| 4-table histogram (skewed) | 3.6–4× |
| PFOR under 0.01% outliers | 3.71× (3.70× signed) |
| SIMD block sum (zero-point) | ~6.5–6.7× |
| AI activation/KV (per-channel/per-token layout) | 1.96–1.98× vs uniform u16 |
| MCU (no stdio/mmap/simd) | ~3.4 KB of code |

> The numbers depend on hardware, data size vs. cache and distribution.
> Server estimates are outputs of the capacity model (in the whitepaper),
> not measurements on a real server.
