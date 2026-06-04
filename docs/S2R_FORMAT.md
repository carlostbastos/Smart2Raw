# Smart2Raw `.s2r` portable file format

`fmt = 1` is the canonical portable Smart2Raw format. It is intentionally small, fixed-header, little-endian, and directly usable by mmap-capable readers.

## Header

All multibyte integers are little-endian.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `magic` | `0x33335253` as uint32 LE; bytes `53 52 33 33` (`SR33`) |
| 4 | 1 | `size` | signed int8 class: `8`, `16`, `32`, `64`, or negative for signed classes |
| 5 | 1 | `flags` | bit 0 = signed |
| 6 | 1 | `fmt` | format version, currently `1` |
| 7 | 1 | `reserved` | must be `0` |
| 8 | 8 | `count` | number of elements as uint64 LE |
| 16 | `count * abs(size)/8` | `payload` | elements in canonical little-endian order |
| `16 + payload_len` | 4 | `crc32` | CRC32 IEEE 802.3 of the payload bytes, uint32 LE |

## Class rules

Unsigned classes use positive `size` values:

- `8`: uint8 values `0..255`
- `16`: uint16 values `0..65535`
- `32`: uint32 values `0..4294967295`
- `64`: uint64 values `0..18446744073709551615`

Signed classes use negative `size` values:

- `-8`: int8 values `-128..127`
- `-16`: int16 values `-32768..32767`
- `-32`: int32 values `-2147483648..2147483647`
- `-64`: int64 values `-9223372036854775808..9223372036854775807`

For signed classes, `flags & 1` must be set. For unsigned classes, it must not be set.

## Compatibility contract

A conforming reader must:

1. reject files shorter than 20 bytes;
2. reject unknown magic;
3. reject classes outside `±8/16/32/64`;
4. reject unsupported `fmt` values;
5. reject nonzero reserved byte;
6. verify that the file length equals `16 + payload_len + 4`;
7. verify CRC32 before trusting the payload;
8. decode payload elements as little-endian values.

## Conformance fixtures

Canonical test fixtures live in `conformance/fixtures/`. They are intentionally small and are read by the C CLI plus the Go, JavaScript, and Python ports.

Regenerate them with:

```sh
python3 conformance/scripts/generate_fixtures.py
```

Run the full conformance check with:

```sh
conformance/run_conformance.sh
```
