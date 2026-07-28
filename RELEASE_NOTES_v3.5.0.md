# Smart2Raw v3.5.0 - The frame of reference gains a scale

v3.4.0 removed an OFFSET the data did not need: `v = base + delta`. It did not
remove a SCALE. This release does, and then applies the same question to nine
other places where the library was leaving something on the table.

It also contains everything from 3.4.1, including a silent-corruption fix that
matters to anyone on 3.4.0. DOI for this version: 10.5281/zenodo.21623772
(concept DOI, all versions: 10.5281/zenodo.20477234).

## Fix first: unsigned columns above 2^63

A block's base was held in `int64_t` and the largest base was tracked with a
SIGNED comparison, even for an unsigned column. A base above `INT64_MAX` read as
negative, the running maximum stayed too small, the bookkeeping class came out too
narrow, and the base was **truncated on write**. The block sum had the same flaw.

    uint64_t v[2] = { 1, UINT64_MAX };
    s2r_blocked_get(&b, 1)   ->  255        /* should be UINT64_MAX */

No error, no warning, no broken CRC - the file is internally consistent with the
wrong values. Affects any unsigned column whose range crosses 2^63 and that uses
the block-wise layer: 64-bit hashes, hash-derived ids, nanosecond timestamps, wide
bit masks. Signed columns were never affected, nor unsigned ones below 2^63, nor
the flat pool.

25 suites of chosen cases missed it. A new differential fuzz suite - 12 column
shapes, random sizes and block sizes, everything checked against the naive
reference, **fixed seeds** - finds it in 9 checks and passes 100,950 on the fix.

## Affine factoring

    v = base + stride * i        stride = gcd, over the block, of (v - base)

A column of `{500, 1500, ... 11500}` spans 11000 and takes 14 bits, but every
value is `base + 1000*i` with `i` in 0..11. The common step is the gcd of the
offsets, one pass to find, and dividing it out is exact by construction.

**Not a dictionary**: no lookup table, no per-value indirection. The map is a
closed-form affine function, so every operation rewrites in the index domain and
the stored bytes stay the native integers they were.

    v > t          <=>   i > (t - base) / stride
    SUM(v)         =     n*base + stride*SUM(i)

`stride == 1` is exactly v3.4.0, the same way `base == 0` was exactly v3.3.
Measured on 12M elements: **22.89 MB / 1.033 ms -> 11.44 MB / 0.468 ms**.

New: per-block stride in `S2RBlocked`, the flat `S2RAffine` pool, and `fmt = 3`,
emitted only when a stride above 1 exists so a strideless column is byte-for-byte
the file v3.4.0 wrote.

## Nine more, from an audit that asked what else was escaping

- **Established order.** The block-wise layer has answered by binary search since
  v3.4.0; the flat pool ignored order entirely. `S2R_FLAG_SORTED` is cleared by
  every write, set by `s2r_sort()` and `s2r_mark_sorted()`, and **kept by an
  in-order append** - the ingest pattern that matters. `count_gt` on 8M sorted
  elements: 0.371 ms -> below the clock.
- **Healing across the sign boundary.** `s2r_fit_class_signedness()`. A column
  declared signed that never receives a negative was twice as wide as it needed:
  15.26 MB -> 7.63 MB. Separate entry point on purpose - after healing, a negative
  push is refused.
- **Constant columns carry zero bits.** `S2RAffine` stores no payload at all:
  7.63 MB -> 0, every predicate O(1).
- **The four missing block-wise predicates**: `count_lt`, `count_eq`,
  `count_range`, `sum_if`, with zone skipping - worth more to a bounded window
  than to a one-sided threshold.
- **The flat pool's zone map.** `s2r_summarize()` records min, max and sum;
  predicates refuse or accept from them without reading payload. `count_gt(220)`
  on a column that stops at 200: 0.1435 ms -> 0.000034 ms.
- **The cumulative index.** A `u8` column has 256 possible values, so the
  cumulative count answers ANY range exactly in two reads from 2 KB that does not
  grow with the data: **4231x**, repaid in 11 queries. The pool carries an epoch;
  a stale index REFUSES rather than answers.
- **Block size is classified, not guessed.** `s2r_blocked_plan()` prices every
  candidate from one pass - a fine-granularity sweep plus an exact tree merge of
  the gcds - and `s2r_blocked_build_auto()` picks. Verified: the prediction equals
  the built size in every shape-candidate combination tested.
- **`s2r_recommend()`**, because the obvious entry point is the worst: 4M
  timestamps are 15.26 MB and 0.73 ms in a flat pool against 4.11 MB and 0.04 ms
  block-wise.
- **"Never expands" is now asserted.** Every classical alternative has a regime
  where it grows the input - dictionary encoding of a high-cardinality column
  measures 41.01 MB against a 30.52 MB `int64` baseline; RLE on unordered data
  stores one run per value. Smart2Raw classifies by RANGE, so its worst case is
  "the range needs 64 bits", which IS the input. `benchmarks/format_matrix.c`
  measures the whole grid and asserts the bound before printing each row.

## Scope corrections

- **"flags" left the use-case list.** A boolean column takes 8x a bitmap's space
  and popcount answers it ~17x faster. Advertising the shape where we lose most is
  the opposite of measured honesty.
- `fit_class()` does not change signedness, and now says so.
- `S2R_BLOCK_DEFAULT` is a default, not an optimum: dominated on two of three
  measured shapes.

## Validation

- **31 suites, 0 failures.** New: `test_fuzz_diff.c` (100,950), `test_affine.c`
  (283), `test_gaps.c` (196), `test_summary.c` (125).
- ASan+UBSan clean; ports, CLI, conformance, binding and examples all pass.
- File compatibility measured in both directions: a strideless column written by
  3.5.0 is byte-identical to what 3.4.0 wrote and opens in 3.4.0; a column with a
  stride is `fmt = 3` and 3.4.0 correctly refuses it.

## Honest scope

Affine factoring pays on data with real granularity - fixed sampling intervals,
fixed-point money, quantization steps. On arbitrary values the gcd is 1 and the
classification is byte-for-byte what v3.4.0 produced. It does not reverse the
warehouse benchmark's regime B either: with the stride factored out we are still
2x larger and 1.44x slower than 4-bit dictionary codes, because 12 distinct values
need log2(12) = 3.58 bits and the smallest native class is 8. That absence is the
design decision that buys 7.9 ms -> 0.00 ms of materialisation, not an oversight.
