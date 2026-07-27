# Warehouse-format benchmark

Smart2Raw against the encoding family warehouses are built on: dictionary
encoding, bit-packing and RLE (the Capacitor / Parquet stack), implemented in C
with best-practice operators.

| file | role |
|---|---|
| `bench_warehouse.c` | the harness — every figure in the document comes from here |
| `s2r_seg2.c` | the segment layer (research): frame of reference, zone maps, constant segments, sorted flag, `.s2r` fmt=2 serialization |
| `WAREHOUSE_FORMAT_BENCH.md` | the write-up, including the honesty ledger |

```sh
gcc -O3 -march=native -std=c11 -I ../../include \
    bench_warehouse.c s2r_seg2.c -o bench_warehouse
./bench_warehouse            # or: ./bench_warehouse 48000000
```

Every count and sum is checked against a brute-force reference **before** it is
timed; a mismatch aborts rather than printing a fast wrong answer. Keep `N*8`
well above last-level cache or the bandwidth axis disappears. Ratios are the
finding; absolute times move on shared machines.

### Relationship to the shipped PFOR layer

`s2r_seg2.c` is not a parallel universe - it is where block-wise ideas get tried
before they earn a place in `include/smart2raw.h`. That path is live: the **frame
of reference** was prototyped here and shipped in the core in v3.4.0, where it is
worth 3.9x on timestamps, 7.7x on sequential IDs and 170x on a constant column,
and where writing the block-skip test carelessly cost a real bug that 337 new
checks caught.

What still lives only here, and is the next thing to move:

| feature | here | `S2RBlocked` (shipped) |
|---|:--:|:--:|
| frame of reference per block | yes | **yes, since v3.4.0** |
| constant block stores no payload | yes | **yes, since v3.4.0** |
| bases in the smallest fitting class | yes | **yes, since v3.4.0** |
| predicate with block skipping | yes | **yes, since v3.4.0** |
| zone sums (`SUM` from metadata, O(nblocks)) | yes | **yes, since v3.4.0** (114x) |
| true per-block span (tighter predicate skip) | yes | **yes, since v3.4.0** |
| sorted flag (`count_gt` by binary search) | yes | **yes, since v3.4.0**, behind a measured size gate |
| `.s2r` serialization | yes (`fmt = 2`) | **yes, since v3.4.0** (`fmt = 2`) |

Everything this file prototyped now lives in `include/smart2raw.h`. What remains
here is the **benchmark harness's own view** of a segmented column, kept so the
warehouse comparison can vary segment size freely without touching the shipped
`S2RBlocked` defaults. New block-wise ideas should still be tried here first -
that path has now shipped four features and caught three bugs on the way,
including one (`base + span` wrapping at width 8) that returned 0 where the answer
was 30048, and one (binary search losing 0.67x on small blocks) that would have
been a silent regression.

The core it calls (`s2r_count_gt_fast`, `s2r_sum_fast`) is shipped.
