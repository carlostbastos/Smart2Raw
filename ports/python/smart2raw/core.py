# Smart2Raw Python port
# Copyright (C) 2026 Carlos Alberto Terencio Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later

from __future__ import annotations

import struct
import zlib
from pathlib import Path
from typing import Iterable, List

# Canonical portable v3.3 magic used by the C core: uint32 LE 0x33335253.
# On disk this is the byte sequence 53 52 33 33, i.e. b"SR33".
MAGIC = 0x33335253
MAGIC_BYTES = struct.pack("<I", MAGIC)
FORMAT_VERSION = 1
FLAG_SIGNED = 1

_U_LIMITS = (
    (8, 0, 0xFF),
    (16, 0, 0xFFFF),
    (32, 0, 0xFFFFFFFF),
    (64, 0, 0xFFFFFFFFFFFFFFFF),
)
_I_LIMITS = (
    (-8, -(1 << 7), (1 << 7) - 1),
    (-16, -(1 << 15), (1 << 15) - 1),
    (-32, -(1 << 31), (1 << 31) - 1),
    (-64, -(1 << 63), (1 << 63) - 1),
)


class S2RFormatError(ValueError):
    """Raised when a .s2r byte stream is invalid or corrupted."""


def _as_int(value: int) -> int:
    if not isinstance(value, int):
        raise TypeError("Smart2Raw values must be integers")
    return value


def byte_width(size: int) -> int:
    bits = abs(int(size))
    if bits not in (8, 16, 32, 64):
        raise ValueError(f"invalid Smart2Raw class: {size!r}")
    return bits // 8


def _limits_for(size: int):
    table = _I_LIMITS if size < 0 else _U_LIMITS
    for row in table:
        if row[0] == size:
            return row
    raise ValueError(f"invalid Smart2Raw class: {size!r}")


def classify_unsigned(value: int) -> int:
    v = _as_int(value)
    for size, lo, hi in _U_LIMITS:
        if lo <= v <= hi:
            return size
    raise OverflowError("unsigned value does not fit in 64 bits")


def classify_signed(value: int) -> int:
    v = _as_int(value)
    for size, lo, hi in _I_LIMITS:
        if lo <= v <= hi:
            return size
    raise OverflowError("signed value does not fit in 64 bits")


def classify_range(min_value: int, max_value: int, signed: bool = False) -> int:
    lo_v = _as_int(min_value)
    hi_v = _as_int(max_value)
    if lo_v > hi_v:
        raise ValueError("min_value must be <= max_value")
    table = _I_LIMITS if signed else _U_LIMITS
    for size, lo, hi in table:
        if lo <= lo_v and hi_v <= hi:
            return size
    raise OverflowError("range does not fit in 64 bits")


def crc32(data: bytes | bytearray | memoryview) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _encode_le(value: int, width: int, signed: bool) -> bytes:
    return int(value).to_bytes(width, byteorder="little", signed=signed)


def _decode_le(data: bytes, offset: int, width: int, signed: bool) -> int:
    return int.from_bytes(data[offset : offset + width], byteorder="little", signed=signed)


class Smart2RawPool:
    """Pure-Python Smart2Raw pool for demos, notebooks, and conformance tests."""

    def __init__(self, values: Iterable[int] | None = None, *, signed: bool = False, size: int | None = None):
        self.signed = bool(signed)
        self.size = int(size if size is not None else (-8 if self.signed else 8))
        if (self.signed and self.size > 0) or ((not self.signed) and self.size < 0):
            raise ValueError("size sign must match signed option")
        _limits_for(self.size)
        self._values: List[int] = []
        if values is not None:
            self.push_many(values)

    @property
    def count(self) -> int:
        return len(self._values)

    @property
    def byte_length(self) -> int:
        return self.count * byte_width(self.size)

    @property
    def values(self) -> tuple[int, ...]:
        return tuple(self._values)

    def _class_for(self, value: int) -> int:
        return classify_signed(value) if self.signed else classify_unsigned(value)

    def _ensure_fits(self, value: int) -> None:
        v = _as_int(value)
        _, lo, hi = _limits_for(self.size)
        if lo <= v <= hi:
            return
        new_size = self._class_for(v)
        if byte_width(new_size) > byte_width(self.size):
            self.size = new_size

    def push(self, value: int) -> "Smart2RawPool":
        v = _as_int(value)
        self._ensure_fits(v)
        self._values.append(v)
        return self

    def push_many(self, values: Iterable[int]) -> "Smart2RawPool":
        for value in values:
            self.push(value)
        return self

    def get(self, index: int) -> int:
        return self._values[index]

    def set(self, index: int, value: int) -> "Smart2RawPool":
        v = _as_int(value)
        self._ensure_fits(v)
        self._values[index] = v
        return self

    def remove_swap(self, index: int) -> "Smart2RawPool":
        if index < 0 or index >= len(self._values):
            raise IndexError("index out of range")
        self._values[index] = self._values[-1]
        self._values.pop()
        return self

    def fit_class(self) -> "Smart2RawPool":
        if not self._values:
            self.size = -8 if self.signed else 8
            return self
        self.size = classify_range(min(self._values), max(self._values), self.signed)
        return self

    def sum(self) -> int:
        return sum(self._values)


    def sort(self) -> "Smart2RawPool":
        """Sort the pool in place while preserving the current class."""
        self._values.sort()
        return self

    def is_sorted(self) -> bool:
        return all(self._values[i - 1] <= self._values[i] for i in range(1, len(self._values)))

    def unique_sorted(self) -> "Smart2RawPool":
        """Remove adjacent duplicates from an already sorted pool."""
        if len(self._values) < 2:
            return self
        out = [self._values[0]]
        for value in self._values[1:]:
            if value != out[-1]:
                out.append(value)
        self._values = out
        return self.fit_class()

    def nunique(self) -> int:
        return len(set(self._values))

    def value_counts(self) -> dict[int, int]:
        out: dict[int, int] = {}
        for value in self._values:
            out[value] = out.get(value, 0) + 1
        return out

    def to_bytes(self) -> bytes:
        width = byte_width(self.size)
        payload = b"".join(_encode_le(v, width, self.signed) for v in self._values)
        flags = FLAG_SIGNED if self.signed else 0
        header = MAGIC_BYTES + struct.pack("<bBBBQ", self.size, flags, FORMAT_VERSION, 0, len(self._values))
        return header + payload + struct.pack("<I", crc32(payload))

    def save(self, path: str | Path) -> None:
        Path(path).write_bytes(self.to_bytes())

    @classmethod
    def from_bytes(cls, blob: bytes | bytearray | memoryview, *, verify_crc: bool = True) -> "Smart2RawPool":
        data = bytes(blob)
        if len(data) < 20:
            raise S2RFormatError("file too small")
        magic = struct.unpack_from("<I", data, 0)[0]
        if magic != MAGIC:
            raise S2RFormatError(f"bad magic: 0x{magic:08x}")
        size, flags, fmt, reserved, count = struct.unpack_from("<bBBBQ", data, 4)
        if reserved != 0:
            raise S2RFormatError("reserved header byte must be zero")
        if fmt != FORMAT_VERSION:
            raise S2RFormatError(f"unsupported format version: {fmt}")
        signed = (flags & FLAG_SIGNED) != 0 or size < 0
        if (size < 0) != signed:
            raise S2RFormatError("class sign and signed flag disagree")
        width = byte_width(size)
        payload_len = int(count) * width
        expected = 16 + payload_len + 4
        if len(data) != expected:
            raise S2RFormatError(f"length mismatch: got {len(data)}, expected {expected}")
        payload = data[16 : 16 + payload_len]
        if verify_crc:
            want = struct.unpack_from("<I", data, 16 + payload_len)[0]
            got = crc32(payload)
            if want != got:
                raise S2RFormatError(f"crc mismatch: got 0x{got:08x}, want 0x{want:08x}")
        pool = cls(signed=signed, size=size)
        pool._values = [_decode_le(payload, i * width, width, signed) for i in range(int(count))]
        return pool

    @classmethod
    def load(cls, path: str | Path, *, verify_crc: bool = True) -> "Smart2RawPool":
        return cls.from_bytes(Path(path).read_bytes(), verify_crc=verify_crc)


def load(path: str | Path, *, verify_crc: bool = True) -> Smart2RawPool:
    return Smart2RawPool.load(path, verify_crc=verify_crc)
