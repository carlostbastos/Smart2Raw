# `.s2r` format — specification

The canonical, full specification of the portable `.s2r` format (`fmt = 1`) lives
in **[S2R_FORMAT.md](S2R_FORMAT.md)**.

Summary: a fixed 16-byte header — `magic` (`0x33335253`, little-endian; bytes
`53 52 33 33`), `class` (int8; negative = signed), `flags`, `fmt` (1), `count`
(uint64 LE) — followed by the payload in canonical little-endian and a trailing
`CRC32` (IEEE 802.3) over the payload. See S2R_FORMAT.md for the full field table
and endianness rules.
