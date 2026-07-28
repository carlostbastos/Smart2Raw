# Smart2Raw v3.5.1 - A hostile `.s2r` could write outside the heap

A security fix, and nothing else. No new API, no format change: a file written by
3.5.0 is read identically, and a file written by 3.5.1 is read by 3.5.0.
**Anyone who loads `.s2r` files they did not write should update.**

## The defect

The block-wise loader sizes the body it is about to read as

    nblocks * (2 + meta(bcls) + meta(pcls) + meta(ocls) + meta(scls)
                 + (has_stride ? meta(tcls) : 0))
    + bytes

Both terms come off disk, and the sum was plain `size_t`. A file that declares
`nblocks = 2^22` and `bytes = 2^64 - 2^22*6 + 16` makes it **wrap** to 16. The
loader then does `malloc(16)` and copies 4 MB into it.

The file is 64 bytes long and passes every validation that already existed:
magic, `fmt`, all four classes, `nblocks == ceil(count/block)`, every field
`<= SIZE_MAX`, **a correct CRC32 over the real body**, and exact EOF. There is
nothing malformed in it. What it exploits is arithmetic that closes wrong.

Confirmed with AddressSanitizer as a 4 MB `heap-buffer-overflow` over a 16-byte
region, and as a plain segmentation fault at `-O2` with no sanitizer.

**Who is affected:** only callers of `s2r_blocked_load()` on a file from an
untrusted source. Writing was never affected — there both terms describe a
structure that already exists in memory. The flat pool was never affected: it has
done `count > SIZE_MAX/eb` since 3.3. This one place in the family of loaders was
missing the guard the others had.

## The fix: two locks, no new dependency

1. **The arithmetic has to close.** `s2r__blk_body_len_ck()` does the multiply
   and the add in checked arithmetic and only hands back a length that is real.
   No compiler builtins — the same guard shape the flat loader already used, so
   it stays portable to any C99.
2. **The body has to actually be in the file.** The bytes on disk are the only
   honest witness of how big the body is, and they are free: the file is already
   open and seekable. A 64-byte `.s2r` can no longer ask for a gigabyte.

## Why 31 suites did not catch it

Same reason as 3.4.1, different target. The suites test files **written** by the
library, and then files **corrupted** byte by byte — the two things that happen
on their own. None tested a file **built on purpose to be internally consistent
and lying**. Accidental corruption is a broken CRC; deliberate corruption comes
with the right one.

## Added: `tests/test_format_hardening.c` section 6 (46 checks, was 40)

Four new checks that write the headers by hand: the length that wraps (the exact
file above), a length that does not wrap but simply is not in the file, the
`nblocks * metadata` multiply overflowing on its own, and — the one that matters
as much as the other three — an honest file written by the library that **must
still load and still sum correctly**. A guard that also rejects real data is not
a fix.

Against the 3.5.0 header this section aborts under ASan. Against 3.5.1 it passes
at `-O2`, `-Os`, in C99 and C11 with `-pedantic -Wall -Wextra`, and under
ASan+UBSan.

## Also corrected: four different suite counts in the docs

`scripts/build_and_test.sh` runs **31 suites** over 21 test files, and 31 is what
CI has been verifying. `.zenodo.json` said 25, `README.md` said 17,
`CONTRIBUTING.md` said 15 — all frozen at older releases. They now say 31.
`SECURITY.md` no longer claims the supported line is 3.3.x, and gains an explicit
threat model: reading a file you did not write is the attack surface.
