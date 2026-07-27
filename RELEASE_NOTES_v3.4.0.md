# Smart2Raw v3.4.0 - The release where the project audited itself

This release adds no new capability. Every item is either a defect that was silently
producing a wrong answer, undefined behaviour on a path whose correctness proof
*requires* defined wraparound, a place where the C core disagreed with its own ports and
its own published specification, or an operation leaving a measured multiple on the
table. DOI for this version: 10.5281/zenodo.21614309
(concept DOI, all versions: 10.5281/zenodo.20477234).

## Highlights

- **The `.s2r` reader now obeys its own specification.** Header flags were adopted
  verbatim from disk, so a crafted `EXTERNAL` bit leaked every loaded pool (confirmed
  under AddressSanitizer) and a `READONLY` bit froze it. Format version, the reserved
  byte, class/signed-flag agreement, a count overflowing `size_t` and exact file length
  are enforced now. Every one of those malformed files was accepted by C and rejected by
  at least one port, so `.s2r` was not the portable contract it claimed to be. The Go
  reader also accepted a declared count that wrapped when converted to `int`.
- **Two classes of undefined behaviour, removed.** `s2r_mul_scalar` at `u16` promoted to
  `int` and overflowed `INT_MAX`, on the very path whose correctness argument depends on
  wraparound being *defined*. The signed sum accumulators used `int64_t` where their
  unsigned twins had always used `uint64_t`. Both found by a new ASan+UBSan CI job; no
  caller's answer changes, the behaviour is simply defined now.
- **SIMD dispatch for the whole predicate family.** `sum_fast` had been the only
  vectorised operation. The family reduces to one range kernel, because a range test on
  a w-bit lane is one wrapping subtract plus one unsigned compare:
  `v in [lo,hi] <=> (v - lo) mod 2^w <= span`, and `count_gt`/`lt`/`eq` *are* ranges. The
  same identity serves signed pools with no second kernel, by two's complement. Measured
  on 48M elements under AVX2: 4.5x-6.0x unsigned, 8.6x-13.4x signed.
- **Frame of reference in the block-wise (PFOR) layer.** A block's width came from its
  maximum alone, so `{9000000000, 9000000001, ...}` was stored as `u64` despite spanning
  1. Measured 3.9x on unix timestamps, 7.7x on sequential IDs at 9e9, 170x on a constant
  column, and exact parity where the baseline was already zero. Plus zone statistics
  (`SUM`/`MAX`/`MIN` in O(nblocks), 114x faster), a sorted-block flag answering
  `count_gt` by binary search above a measured size gate, and block-wise `.s2r`
  serialization (`fmt = 2`, CRC32 over metadata and payload).
- **ARM SVE2 was unreachable dead code** behind NEON, *and* eight times too narrow;
  rewritten around `UDOT`. RISC-V RVV widened the same way (`u8m1`/`u16m2` into `u64m8`).
  Both remain experimental.
- **New `benchmarks/warehouse/`** — Smart2Raw against dictionary encoding, bit-packing
  and RLE, with the peer implemented at its best rather than as a strawman, including an
  explicit honesty ledger of where we win, tie and lose.

## Validation summary

- C test suite: **25 suites, 0 failures** (was 17).
- 142,952 new checks sweeping every threshold and every ordered pair of range endpoints
  for the 8-bit classes against the scalar reference.
- 5,472 new checks over the block-wise layer, run three times under different
  binary-search gate settings, because two paths that must agree are only proven to agree
  if both are executed.
- 40 checks on the `.s2r` contract including the mmap reader; 19 on the CLI and 16 on the
  Python binding, neither of which had any coverage before.
- CI gained ASan+UBSan, jobs for the three ports and a conformance job — **and can now
  fail**: the cross-architecture job piped output through `tail` without `pipefail`, so a
  crashing test produced a green check, and that is the only job exercising big-endian.

## Honest scope

Where measurement contradicted the design, the design lost. Sorted-block binary search
measured **0.67x** on small blocks and is gated off below a size threshold rather than
claimed unconditionally. The `count_gt` kernel was specialised back out of the range form
because the extra wrapping subtract cost ~28% on a bandwidth-bound `u16` column. An
explicit NEON path for the predicate family is deliberately not claimed: it could not be
measured here. RVV and SVE2 stay experimental, validated by emulation across a swept
vector length of 128-1024 bits, not on real hardware.
