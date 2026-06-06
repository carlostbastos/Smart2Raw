# `.s2r` file format specification (fmt = 1)

A self-describing, portable file. All multibyte fields are in **canonical
little-endian**. A fixed 16-byte header lets mmap locate the payload at a
constant offset.

| Offset | Size        | Field   | Contents                                            |
|-------:|------------:|---------|-----------------------------------------------------|
| 0      | 4           | magic   | `0x33335253` (uint32 LE; bytes `53 52 33 33`) marks the portable v3.3 format |
| 4      | 1           | size    | class (`int8`; negative = signed: ±8/16/32/64)      |
| 5      | 1           | flags   | state bits (`S2RFlags`)                             |
| 6      | 1           | fmt     | format version (`1`)                                |
| 7      | 1           | rsvd    | reserved (`0`)                                      |
| 8      | 8           | count   | number of elements (`uint64`, LE)                   |
| 16     | count·eb    | payload | elements in canonical LE (eb = abs(size)/8 bytes)   |
| 16+n   | 4           | crc32   | CRC32 IEEE 802.3 of the payload (`uint32`, LE)      |

- On a little-endian host, the on-disk payload matches memory byte for byte.
- On big-endian, reads/writes convert; the CRC is validated over the canonical
  (LE) bytes before any conversion.
- The verifier (`tools/s2r_verify`) checks the magic, fmt, a valid class,
  `count` vs. file size, and recomputes the CRC32.

Compatibility: `fmt = 1` files written in LE are read by any version >= 3.3.0.
