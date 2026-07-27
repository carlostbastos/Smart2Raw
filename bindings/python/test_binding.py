#!/usr/bin/env python3
# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terencio Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Regression tests for the ctypes binding.

The binding had no tests, which is how two silent wrong-answer bugs survived:

  * `signed` was a plain Python attribute that push() flipped on the first
    negative value. The underlying C pool stayed unsigned, so the signed push was
    rejected, the return code was discarded, and the value vanished -- while every
    later read went through the signed accessors and reinterpreted bytes that had
    been stored as unsigned. Pool(S2R_8); push(200); push(-1) left ONE element
    that read back as -56.
  * `sum()` always called the unsigned reduction, so a signed pool summing to -34
    came back as 734. min()/max() were already sign-aware, which made the
    inconsistency easy to miss.

Run: python build_lib.py && python test_binding.py
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smart2raw as sr  # noqa: E402


class TestSignedness(unittest.TestCase):
    def test_pool_reports_c_side_signedness(self):
        self.assertFalse(sr.Pool(sr.S2R_8).signed)
        self.assertTrue(sr.Pool(sr.S2R_I8).signed)

    def test_negative_push_on_unsigned_pool_raises(self):
        p = sr.Pool(sr.S2R_8)
        p.push(200)
        with self.assertRaises(ValueError):
            p.push(-1)
        # the rejected push must not corrupt or truncate what is already there
        self.assertEqual(len(p), 1)
        self.assertEqual(p[0], 200)
        self.assertFalse(p.signed)

    def test_signedness_cannot_be_flipped_at_runtime(self):
        p = sr.Pool(sr.S2R_8)
        p.push(200)
        try:
            p.push(-1)
        except ValueError:
            pass
        # the read path must still be the unsigned one (this used to return -56)
        self.assertEqual(p[0], 200)

    def test_signed_pool_accepts_negatives(self):
        p = sr.Pool(sr.S2R_I8)
        p.extend([-1, -128, -5, 100])
        self.assertEqual(len(p), 4)
        self.assertEqual(p.to_list(), [-1, -128, -5, 100])


class TestReductions(unittest.TestCase):
    def setUp(self):
        self.signed = sr.Pool(sr.S2R_I8).extend([-1, -128, -5, 100])
        self.unsigned = sr.Pool(sr.S2R_8).extend([25, 30, 40, 1000, 70000])

    def test_signed_sum(self):
        self.assertEqual(self.signed.sum(), -34)

    def test_signed_sum_fast_matches_sum(self):
        self.assertEqual(self.signed.sum_fast(), self.signed.sum())

    def test_signed_min_max(self):
        self.assertEqual(self.signed.min(), -128)
        self.assertEqual(self.signed.max(), 100)

    def test_unsigned_sum(self):
        self.assertEqual(self.unsigned.sum(), 71095)
        self.assertEqual(self.unsigned.sum_fast(), 71095)

    def test_unsigned_min_max(self):
        self.assertEqual(self.unsigned.min(), 25)
        self.assertEqual(self.unsigned.max(), 70000)


class TestAdaptiveClass(unittest.TestCase):
    def test_class_grows_without_truncation(self):
        p = sr.Pool(sr.S2R_8)
        p.extend([25, 30, 40, 1000, 70000, 5000000000])
        self.assertEqual(p.class_bits, 64)
        self.assertEqual(p.to_list(), [25, 30, 40, 1000, 70000, 5000000000])

    def test_signed_class_grows(self):
        p = sr.Pool(sr.S2R_I8)
        p.extend([-1, -30000])
        self.assertEqual(p.class_bits, -16)
        self.assertEqual(p.to_list(), [-1, -30000])


class TestScalarArithmetic(unittest.TestCase):
    def test_mul_promotes_and_stays_exact(self):
        p = sr.Pool(sr.S2R_8).extend([200, 100])
        p.mul_scalar(300)
        self.assertEqual(p.to_list(), [60000, 30000])
        self.assertEqual(p.class_bits, 16)   # 60000 still fits u16

    def test_mul_promotes_past_u16(self):
        p = sr.Pool(sr.S2R_8).extend([200, 100])
        p.mul_scalar(30000)
        self.assertEqual(p.to_list(), [6000000, 3000000])
        self.assertEqual(p.class_bits, 32)

    def test_signed_add(self):
        p = sr.Pool(sr.S2R_I8).extend([-100, 50])
        p.add_scalar(-100)
        self.assertEqual(p.to_list(), [-200, -50])
        self.assertEqual(p.class_bits, -16)


class TestPersistence(unittest.TestCase):
    def test_round_trip_preserves_signedness_and_values(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "signed.s2r")
            src = sr.Pool(sr.S2R_I8).extend([-1, -128, -5, 100])
            src.save(path)
            got = sr.Pool.load(path)
            self.assertTrue(got.signed)          # derived from the pool, not cached
            self.assertEqual(got.to_list(), [-1, -128, -5, 100])
            self.assertEqual(got.sum(), -34)

    def test_load_missing_file_raises(self):
        with self.assertRaises(IOError):
            sr.Pool.load("/nonexistent/nope.s2r")


if __name__ == "__main__":
    print("Smart2Raw C library v" + sr.version())
    unittest.main(verbosity=2)
