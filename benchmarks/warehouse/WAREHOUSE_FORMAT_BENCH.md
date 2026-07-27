# Smart2Raw vs warehouse-class column formats (Capacitor / Parquet family)

> **Revision note.** This document replaces an earlier version whose regime A and
> regime B conclusions did not survive re-measurement. The cause was a
> methodological asymmetry: the peer was implemented at its most convenient while
> our side got the benefit of hand-written SIMD. The earlier text names this exact
> failure mode - *"measuring a classical layout and attributing the result to our
> theory produces false defeats"* - and the symmetric version is just as real:
> measuring a peer implemented below the state of the art produces **false
> victories**. Both directions are corrected here. Section 7 lists precisely what
> is retracted.
>
> **What is measured.** BigQuery-the-*service* was not benchmarked and cannot be
> from this sandbox. What was built and measured is BigQuery-the-*format-family*:
> dictionary encoding + bit-packing + RLE, in C, with best-practice operators.
> This measures **per-core format efficiency**. It says nothing about distributed
> execution, slot scheduling, or petabyte scale `[gate]`.
>
> Tags: `[measured]` executed and verified · `[gate]` not testable here.
>
> Single core, `gcc -O3 -march=native`, N = 12,000,000, AVX2. Every count and sum
> is checked against a brute-force reference **before** it is timed; a mismatch
> aborts the run. Source: `bench_warehouse.c`, one program, all numbers.

---

## 1. The two rules this round enforces on itself

**The peer is implemented at its best, not at its most convenient.** Two
consequences, and both change results:

- A dictionary is built over **sorted** distinct values. That is not an
  optimisation, it is what the encoding *is* - and it means `dict[d] > k` is
  monotone in `d`, so the predicate collapses to `code > d_k`: a comparison on the
  codes themselves, with nothing to decode. Any peer that evaluates
  `hit[unpack(codes,i)]` per value when it could compare codes directly is being
  measured below its own definition.
- Sub-byte codes get a **SIMD unpacker**. Two nibbles per byte come apart with a
  mask and a shift, amortised over 64 values per vector.

**Our side uses the shipped library, not a research kernel.** Every Smart2Raw
figure below calls `s2r_count_gt_fast` / `s2r_sum_fast` from
`include/smart2raw.h` v3.4.0. The previous round could not do this - the shipped
filters had no SIMD dispatch at all, so the documented numbers came from
research-layer kernels a user could not reach. That gap is closed; the shipped
`count_gt_fast` now matches the hand-written kernel to within 5%.

And where the representation carries more than a flat array does - regime C - the
**segment layer is applied**, for the same reason the peer gets its best form.

---

## 2. The peer, implemented at its best

```
DICTIONARY ENCODING
  build:  dict[]  = SORTED distinct values
          cbits   = ceil(log2(|dict|))
          codes[] = bit-packed, codes[i] = index of v[i] in dict
  size  = n*cbits/8 + |dict|*8

PREDICATE - the dictionary is sorted, so pushdown is total:
  count_gt(col, k):
      d_k = largest d with dict[d] <= k          # once, cardinality-sized
      return count of codes > d_k                # a comparison on the CODES
  -> byte-aligned codes: one AVX2 compare per 32 codes, no decode
  -> 4-bit codes:        mask + shift, then one compare per 64 codes

SUM - no such shortcut exists:
  sum(col):
      for i in 0..n: hist[ unpack(codes,i) ]++   # a code is not an addable operand
      return sum over d of dict[d]*hist[d]

RLE (the run-shaped case):
  count_gt: for each run: if dict[run.code] > k: cnt += run.length
  sum:      for each run: s += dict[run.code] * run.length
```

**Smart2Raw's side:**

```
  build:    class = smallest NATIVE class holding the range
  count_gt: s2r_count_gt_fast  - runtime SIMD dispatch, nothing to unpack
  sum:      s2r_sum_fast       - vpsadbw over the stored bytes
  and where the data has structure: segment layer - frame of reference per
  segment, zone maps, constant segments carry NO payload, sorted segments
  answer by binary search
```

---

## 3. Regime A - uniform integers, high cardinality (telemetry) `[measured]`

`status`, values 0..200, uniform. Cardinality 201 → **8-bit codes**.

Ratios below are the **median of 5 runs**, with the observed range in brackets;
absolute milliseconds move on a shared machine, ratios do not.

| | size | `COUNT(x>100)` | `SUM(x)` |
|---|---:|---:|---:|
| Smart2Raw u8 (shipped) | 11.44 MB | **1.00x** (0.60 ms) | **1.00x** (0.44 ms) |
| peer, `hit[unpack(code)]` | 11.45 MB | 12.1x [9.7-15.1] | 17.1x [15.8-19.1] |
| peer, sorted-dict pushdown + SIMD | 11.45 MB | **1.05x [0.86-1.08]** | — |
| conventional `int64` | 91.6 MB | — | — |

**The sizes are identical, and at 8-bit codes that is not a coincidence - the
peer's payload is byte-for-byte the same array as our `u8` pool.** Once that is
true, there is nothing left for a "decode tax" to tax: the sorted dictionary makes
the predicate a comparison on those very bytes, and the two implementations
converge to **1.05x - parity**, with the range straddling 1.0 in both directions.

The 12.1x row is real but it measures a *reader that does not use its own
dictionary's ordering*, not a property of the format. Reporting it as the latter
was the error.

**`SUM` is different, and the 17x survives.** There is no monotonicity trick for
addition: reconstructing a value from a code is a lookup, so the peer must
histogram over codes and fold the dictionary afterwards. That scatter is
structural, and it is where dictionary encoding genuinely costs.

---

## 4. Regime B - low cardinality, wide range: the result inverts `[measured]`

`region_id`, 12 distinct values spread over 500..11500, shuffled. Cardinality 12 →
**4-bit codes**, a sub-byte width this project deliberately refuses.

| | size | `COUNT(x>5500)` |
|---|---:|---:|
| Smart2Raw u16 (shipped) | 22.89 MB | **1.00x** (~1.4 ms) |
| peer, unpack + LUT | 5.72 MB | 8.7x [8.2-10.2] |
| peer, nibble SIMD + pushdown | **5.72 MB (4.0x smaller)** | **0.27x [0.25-0.28]** |

The previous version listed "add a shuffle-based SIMD unpacker to the peer" as
untested future work (its section 8, item 2). It is tested now, and the answer
inverts the regime: **the peer is ~3.7x faster and 4x smaller**, not 8.5x slower.

Effective bandwidth: Smart2Raw 15.1 GB/s, peer 16.0 GB/s. **Both are memory
bound at the same rate** - the peer simply has four times less to move, and
unpacking costs two vector operations amortised over 64 values. That is not a
toll, it is noise.

So the earlier framing - *"we spend bytes to buy instructions"* - does not hold
here. In this regime we spend bytes **and** instructions. The honest statement is
different and, we think, stronger:

> We decline dictionary and sub-byte encoding so that the stored bytes stay
> executable by any instruction on the machine. Regime B is what that costs when
> the data is low-cardinality: 4x the space and ~3.7x the time.

That is a price, openly stated, for the property section 6 measures - and the data
this library targets is mostly not shaped like regime B. But the price is real and
belongs in the documentation next to the wins.

---

## 5. Regime C - sorted, run-shaped: the segment layer, and its knob `[measured]`

Same column, sorted. The classical flat pool is the wrong thing to measure here:
the representation carries information a flat array discards, and applying it is
the point of the project.

**The knob nobody turned.** Segment size trades payload against metadata: smaller
segments make more of them *constant* (a constant segment stores nothing at all)
but multiply the per-segment bookkeeping. The total has a minimum, and it is at
neither end:

| `seg_size` | segments | constant | payload | metadata | **total** | `COUNT` | bytes touched |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 256 | 46875 | 46866 | 4.50 KB | 549.32 KB | 553.82 KB | 29.93 us | 183.30 KB |
| 1024 | 11719 | 11708 | 22.00 KB | 137.33 KB | 159.33 KB | 7.53 us | 46.09 KB |
| **2048** | **5860** | **5849** | **44.00 KB** | **68.67 KB** | **112.67 KB** | **3.80 us** | **23.27 KB** |
| 4096 | 2930 | 2919 | 88.00 KB | 40.06 KB | 128.06 KB | 1.94 us | 11.89 KB |
| 16384 | 733 | 722 | 352.00 KB | 10.02 KB | 362.02 KB | 0.55 us | 3.43 KB |
| 65536 | 184 | 173 | 1408.00 KB | 2.52 KB | 1410.52 KB | 0.20 us | 1.41 KB |

The previous document reported this regime at `seg_size = 65536` and called the
result 1.38 MB. **The optimum is 112.67 KB at `seg_size = 2048` - 12.5x better
than the figure that was published**, at 3.80 us. And the frontier is genuinely
two-sided: 65536 is 7x faster but 12.5x larger. Reporting one point of a Pareto
curve as *the* result understated our own layer.

Against the peer:

| | size | `COUNT(x>5500)` | `SUM(x)` |
|---|---:|---:|---:|
| flat pool (classical) | 22.89 MB | 1474 us | 1099 us |
| segments, `seg=2048` | **112.67 KB** | 3.80 us | — |
| segments, `seg=65536` | 1410.52 KB | **0.20 us** | — |
| peer RLE | **0.09 KB** (12 runs) | **0.043 us** | **0.041 us** |

**RLE wins this regime, and by a wide margin on size.** 0.09 KB against 112 KB is
1250x, and it is not close. Run-length is simply the correct encoding for data
that *is* runs; a fixed-width representation cannot match it there. On time the
gap is 4.6x at `seg=65536` - the same order, not the same number.

What the segment layer does buy is the comparison against the *classical* layout
it replaces: 208x less memory and 7,400x less predicate time than the flat pool,
with **1.41 KB actually touched** out of 22.89 MB. That number is now counted -
distinct cache lines probed, plus the zone entries the scan really walks - not
modelled. The previous version reported a hardcoded constant (`8 * 64`) as a
measurement; it was the source of the published "0.50 KB", and it did not vary
with segment size even though the true figure ranges from 1.41 KB to 732 KB
across the sweep.

**The metadata now obeys the library's own thesis.** Segment bookkeeping used to
be five `int64` fields per segment - "use int64 everywhere just to be safe", the
exact habit the README's *Core principle* exists to break. Turning
`s2r_classify` on the metadata itself picks `i16` for the zone bounds, `i32` for
the segment sums and `u16`/`u32` for the offsets, and `base` and `count` turn out
to be derivable and are no longer stored at all. Metadata dropped 3.4x-4.0x, and
the bytes the predicate touches dropped 4x. Without that, the 112 KB optimum above
would be 300 KB and the curve's minimum would sit somewhere else entirely.

---

## 6. The cross-domain test: the claim that survives everything `[measured]`

`COUNT` and `SUM` are SQL-shaped, and sections 3-5 show our advantage there is
regime-dependent - real in A's `SUM`, absent in A's `COUNT`, negative in B. The
thesis that does **not** depend on out-implementing anyone is different:

> A dictionary code is meaningful only to the engine that owns the dictionary.
> A native-width integer is meaningful to every instruction on the machine.

A quantized attention dot, a convolution, an int8 matmul, a DSP filter - each
needs a **contiguous native-width buffer**. The warehouse format must produce one
first:

| path | cost to reach a non-SQL kernel's starting line |
|---|---:|
| peer → materialise contiguous `u8` | **7.9 ms** |
| Smart2Raw pool → already contiguous `u8` | **0.00 ms** |

~7.9 ms per pass, paid every time a non-SQL kernel touches the data. This is not a
slow implementation - it is the format's definition, and no better unpacker
removes it, because the output has to exist somewhere. Note that this is the one
measurement in this document that a stronger peer implementation cannot move.

This is the honest form of "more than SQL": **not that our scan beats a
warehouse's - regime B shows it does not - but that our bytes are directly
executable by kernels the warehouse format cannot reach at all.**

---

## 7. Honesty ledger

**Retracted from the previous version:**

- *"8.5x-18.7x on count and sum against the warehouse encoding family."* The
  `SUM` half stands (16.6x, regime A). The `COUNT` half does not: with the peer
  using its own sorted dictionary, regime A is **1.03x** and regime B is **0.24x**
  (a loss). `[measured]`
- *"The decode tax is structural, not incidental."* True for `SUM`. False for
  `COUNT`, where a sorted dictionary means there is no decode. `[measured]`
- *Regime B's 8.5x/9.5x in our favour.* With a SIMD nibble unpacker the peer is
  **~3.7x faster and 4x smaller** (ratio 0.27x, range 0.25-0.28 over 5 runs).
  `[measured]`
- *"0.50 KB touched by the predicate"* in regime C. That was a hardcoded constant,
  not a count. The measured range is 1.41 KB to 732 KB depending on segment size.
- *Regime C at 1.38 MB.* That is one point on a curve whose optimum is
  **112.67 KB**. This one understated us.

**Licensed by measurement:**

- Regime A `SUM`: **17x** (median of 5; range 15.8-19.1) over the dictionary path,
  at identical size. `[measured]`
- Regime A `COUNT`: **parity** (1.05x, range 0.86-1.08) at identical size -
  dictionary encoding buys the peer nothing here and costs it nothing either.
  `[measured]`
- Regime B: the peer wins **4x on space and ~3.7x on time**. This is the measured
  price of refusing sub-byte and dictionary encoding. `[measured]`
- Regime C against the classical flat layout: **208x less memory, 7,400x less
  predicate time**, 1.41 KB touched out of 22.89 MB. `[measured]`
- Regime C against RLE: RLE wins size by ~1250x and time by ~4.6x. `[measured]`
- Cross-domain: **~7.9 ms** of materialisation stands between warehouse-encoded
  data and any non-SQL kernel; our cost is zero. `[measured]`

**Not licensed - do not claim:**

- *"We beat BigQuery."* We measured its format family on one core. `[gate]`
- Anything about DuckDB, ClickHouse, Snowflake, or real Arrow C++. `[gate]`
- Any advantage on run-shaped or low-cardinality columns. We lose both. `[measured]`

---

## 8. Reproduce

```bash
gcc -O3 -march=native -std=c11 -I ../../include \
    bench_warehouse.c s2r_seg2.c -o bench_warehouse
./bench_warehouse            # or ./bench_warehouse 48000000
```

One program, all figures, assertions on every result before timing. Keep `N*8`
well above last-level cache or the bandwidth axis disappears. **Ratios are the
finding; absolutes move on shared machines.**

---

## 9. What this opens

1. **Run-shaped data: delegate, do not compete.** Regime C is a real size loss to
   RLE and no amount of segment tuning closes 1250x. The useful question is
   build-time *detection* - a column whose run count is a tiny fraction of its
   length should be handed to a run encoding, and the segment layer already
   computes what is needed to detect it (`nconst` per sweep). `[design]`
2. **Sub-byte, now with a number attached.** Regime B is the measured cost of
   refusing it: 4x space, 4.2x time. Whether that trade should stay absolute or
   become a documented opt-in is now a decision with evidence behind it, not a
   preference. `[design]`
3. **The segment size should not be a constant.** The sweep in section 5 is
   data-dependent; `s2r2_build` could pick it by measuring one pass instead of
   taking 65536 on faith. `[design]`
4. **Serialization exists now** (`fmt = 2`, CRC over metadata and payload, a v3.3
   reader rejects it cleanly), so the next round can measure cold-start and mmap
   paths rather than in-memory build only. `[design]`

---

*Code: `bench_warehouse.c`, `s2r_seg2.c`. Core: `include/smart2raw.h` v3.4.0.*
