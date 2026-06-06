# Smart2Raw Analytics v2

Analytics v2 adds compact integer primitives that are useful before building the table layer.

## Operations

- `s2r_sort`: sort a pool in place while preserving its current integer class.
- `s2r_is_sorted`: check ascending order.
- `s2r_unique_sorted`: remove adjacent duplicates from an already sorted pool.
- `s2r_nunique`: count distinct values without modifying the original pool.
- `s2r_value_counts_u8`: count byte-class values in 256 bins. For signed i8, bins use the raw stored byte (`-1` is bin 255, `-128` is bin 128).

## Why this matters

These operations make Smart2Raw more useful for telemetry, logs, feature stores, token IDs, categorical features and small integer analytics. They also prepare the next architectural step: multi-column tables where sort, unique, group-by and value counts become table-level operations.

## Scope

The C core keeps the hot path compact. Ports in Go, JavaScript and Python expose equivalent APIs for demos, services, browsers, notebooks and conformance tests.
