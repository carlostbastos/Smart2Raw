# Smart2Raw Python port tests
# Copyright (C) 2026 Carlos Alberto Terencio Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later

import tempfile
import unittest
from pathlib import Path

from smart2raw import S2RFormatError, Smart2RawPool, classify_range, classify_signed, classify_unsigned, load


class Smart2RawPythonTests(unittest.TestCase):
    def test_unsigned_promotion_and_sum(self):
        p = Smart2RawPool()
        p.push_many([1, 2, 255, 256, 65536])
        self.assertEqual(p.size, 32)
        self.assertEqual(p.count, 5)
        self.assertEqual(p.sum(), 1 + 2 + 255 + 256 + 65536)

    def test_signed_promotion_and_fit_class(self):
        p = Smart2RawPool(signed=True)
        p.push_many([-10, 20, 200])
        self.assertEqual(p.size, -16)
        p.remove_swap(2)
        p.fit_class()
        self.assertEqual(p.size, -8)
        self.assertEqual(sorted(p.values), [-10, 20])

    def test_classification(self):
        self.assertEqual(classify_unsigned(255), 8)
        self.assertEqual(classify_unsigned(256), 16)
        self.assertEqual(classify_signed(-129), -16)
        self.assertEqual(classify_range(-100, 100, signed=True), -8)

    def test_save_load_roundtrip_unsigned(self):
        p = Smart2RawPool([0, 1, 255, 256, 65535])
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "u16.s2r"
            p.save(path)
            q = load(path)
        self.assertEqual(q.size, 16)
        self.assertFalse(q.signed)
        self.assertEqual(q.values, p.values)

    def test_save_load_roundtrip_signed(self):
        p = Smart2RawPool([-128, -1, 0, 127], signed=True)
        blob = p.to_bytes()
        q = Smart2RawPool.from_bytes(blob)
        self.assertEqual(q.size, -8)
        self.assertTrue(q.signed)
        self.assertEqual(q.values, p.values)

    def test_crc_detects_corruption(self):
        p = Smart2RawPool([1, 2, 3])
        blob = bytearray(p.to_bytes())
        blob[16] ^= 0x7F
        with self.assertRaises(S2RFormatError):
            Smart2RawPool.from_bytes(blob)


    def test_analytics_v2(self):
        p = Smart2RawPool([10, -1, -128, 10, 0, -1], signed=True)
        self.assertFalse(p.is_sorted())
        p.sort()
        self.assertTrue(p.is_sorted())
        self.assertEqual(p.values, (-128, -1, -1, 0, 10, 10))
        self.assertEqual(p.nunique(), 4)
        self.assertEqual(p.value_counts()[-1], 2)
        self.assertEqual(p.value_counts()[10], 2)
        p.unique_sorted()
        self.assertEqual(p.values, (-128, -1, 0, 10))
        self.assertEqual(p.size, -8)


if __name__ == "__main__":
    unittest.main()
