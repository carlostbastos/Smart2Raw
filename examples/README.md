# examples/ — one example per use case

Each file is minimal and runnable. Build: `make` (needs `gcc`).

| File | What it shows |
|---|---|
| `telemetry.c`      | u8/u16 latencies: % memory vs int64 + filters |
| `iot_edge.c`       | signed sensors (i16): tiny footprint |
| `analytics.c`      | class = free zone-map; `sum_fast` + range |
| `feature_store.c`  | each column in its own width; total vs uniform int64 |
| `ai_activations.c` | activations with outlier channels (per-channel PFOR): ~2x |
| `ai_kv_cache.c`    | KV-cache with outlier tokens (per-token PFOR) |
| `ai_zeropoint.c`   | zero-point correction (row-sum) per block, exact |
| `indices.c`        | token IDs: 2x if vocab <=16b, ~1x above (honest) |

Honest framing: the AI examples gain over the *safe uniform* width (u16 forced by
outliers), not over a flat int8; and none of this is the GEMM itself.
