# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terêncio Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
smart2raw - Python (ctypes) wrapper over the Smart2Raw C library.

The "Python in charge": loads the shared library (libsmart2raw.so /
.dylib / smart2raw.dll) and exposes a Pythonic API. Build the lib once with
`python build_lib.py` (or point S2R_LIB to an already-built .so/.dll).

Usage:
    from smart2raw import Pool, S2R_8
    p = Pool(S2R_8)
    for v in range(1000):
        p.push(v)          # grows the class by itself (no truncation)
    print(p.sum(), p.min(), p.max(), p.class_bits)
    p.save("data.s2r")
    q = Pool.load("data.s2r")
"""
import ctypes as _ct
import os, sys, glob

# classes (negative = signed)
S2R_8, S2R_16, S2R_32, S2R_64 = 8, 16, 32, 64
S2R_I8, S2R_I16, S2R_I32, S2R_I64 = -8, -16, -32, -64


def _find_lib():
    if os.environ.get("S2R_LIB"):
        return os.environ["S2R_LIB"]
    here = os.path.dirname(os.path.abspath(__file__))
    names = ["libsmart2raw.so", "libsmart2raw.dylib", "smart2raw.dll",
             "libsmart2raw.dll"]
    for n in names:
        cand = os.path.join(here, n)
        if os.path.exists(cand):
            return cand
    # fallback: qualquer lib no diretorio
    for pat in ("libsmart2raw.*", "smart2raw.*"):
        hits = [h for h in glob.glob(os.path.join(here, pat))
                if h.rsplit(".", 1)[-1] in ("so", "dylib", "dll")]
        if hits:
            return hits[0]
    raise OSError(
        "Smart2Raw library not found. Run 'python build_lib.py' "
        "in this folder, or set S2R_LIB to the path of the .so/.dll.")


_lib = _ct.CDLL(_find_lib())
_P = _ct.c_void_p

def _sig(name, restype, *argtypes):
    f = getattr(_lib, name)
    f.restype = restype
    f.argtypes = list(argtypes)
    return f

_new          = _sig("s2r_capi_new", _P, _ct.c_int, _ct.c_size_t)
_free         = _sig("s2r_capi_free", None, _P)
_push         = _sig("s2r_capi_push_adaptive", _ct.c_int, _P, _ct.c_uint64)
_push_s       = _sig("s2r_capi_push_signed_adaptive", _ct.c_int, _P, _ct.c_int64)
_get          = _sig("s2r_capi_get", _ct.c_uint64, _P, _ct.c_size_t)
_get_s        = _sig("s2r_capi_get_signed", _ct.c_int64, _P, _ct.c_size_t)
_count        = _sig("s2r_capi_count", _ct.c_size_t, _P)
_classb       = _sig("s2r_capi_class_bits", _ct.c_int, _P)
_used         = _sig("s2r_capi_used_bytes", _ct.c_size_t, _P)
_is_signed    = _sig("s2r_capi_is_signed", _ct.c_int, _P)
_sum          = _sig("s2r_capi_sum", _ct.c_uint64, _P)
_sum_s        = _sig("s2r_capi_sum_signed", _ct.c_int64, _P)
_sumf         = _sig("s2r_capi_sum_fast", _ct.c_uint64, _P)
_min          = _sig("s2r_capi_min", _ct.c_uint64, _P)
_max          = _sig("s2r_capi_max", _ct.c_uint64, _P)
_min_s        = _sig("s2r_capi_min_signed", _ct.c_int64, _P)
_max_s        = _sig("s2r_capi_max_signed", _ct.c_int64, _P)
_add_safe     = _sig("s2r_capi_add_scalar_safe", _ct.c_int, _P, _ct.c_uint64)
_mul_safe     = _sig("s2r_capi_mul_scalar_safe", _ct.c_int, _P, _ct.c_uint64)
_add_safe_s   = _sig("s2r_capi_add_scalar_signed_safe", _ct.c_int, _P, _ct.c_int64)
_mul_safe_s   = _sig("s2r_capi_mul_scalar_signed_safe", _ct.c_int, _P, _ct.c_int64)
_save         = _sig("s2r_capi_save", _ct.c_int, _P, _ct.c_char_p)
_load         = _sig("s2r_capi_load", _P, _ct.c_char_p)
_version      = _sig("s2r_capi_version", _ct.c_char_p)


def version():
    return _version().decode()


_ERRORS = {
    -1: "null pointer", -2: "out of memory", -3: "overflow",
    -4: "invalid size / wrong signedness", -5: "value too large",
    -6: "value too small", -7: "empty pool", -8: "I/O error", -9: "corrupt data",
}


def _raise_rc(rc, what):
    raise RuntimeError(f"{what} failed: {_ERRORS.get(rc, f'error {rc}')} (code {rc})")


class Pool:
    """A collection of integers stored in the smallest native class that fits.

    A pool is signed or unsigned for its whole life: that is decided by the class
    passed to the constructor (S2R_8..S2R_64 vs S2R_I8..S2R_I64) and cannot change
    afterwards. The C core enforces it; this wrapper reports it.
    """

    def __init__(self, size_bits=S2R_8, capacity=16, _ptr=None):
        self._p = _ptr if _ptr is not None else _new(size_bits, capacity)
        if not self._p:
            raise MemoryError("failed to create S2RPool")

    # Signedness lives in the C pool, not here. It used to be a plain attribute
    # that push() flipped on the first negative value -- but the underlying pool
    # stayed unsigned, so the C side rejected the signed push, the return code was
    # discarded, and the value vanished. Worse, every later read went through the
    # signed accessors, reinterpreting bytes already stored as unsigned:
    # Pool(S2R_8); push(200); push(-1) left ONE element that read back as -56.
    @property
    def signed(self):
        return bool(_is_signed(self._p))

    # --- insertion ---
    def push(self, v):
        v = int(v)
        if self.signed:
            rc = _push_s(self._p, v)
        else:
            if v < 0:
                raise ValueError(
                    f"cannot push {v}: this pool is unsigned. "
                    "Create it as Pool(S2R_I8) (or another S2R_I* class) for signed data.")
            rc = _push(self._p, v)
        if rc != 0:                      # was discarded: OOM looked like success
            _raise_rc(rc, "push")
        return self

    def extend(self, it):
        for v in it:
            self.push(v)
        return self

    # --- acesso ---
    def __len__(self):
        return _count(self._p)

    def __getitem__(self, i):
        n = len(self)
        if i < 0:
            i += n
        if not (0 <= i < n):
            raise IndexError(i)
        return _get_s(self._p, i) if self.signed else _get(self._p, i)

    # --- metadata ---
    @property
    def class_bits(self):
        return _classb(self._p)            # negative = signed

    @property
    def used_bytes(self):
        return _used(self._p)

    # --- reductions ---
    def sum(self):
        # min()/max() already branched on signedness; sum() did not, so a signed
        # pool summing to -34 came back as 734 (payload read zero-extended).
        return _sum_s(self._p) if self.signed else _sum(self._p)

    def sum_fast(self):
        """SIMD-accelerated sum. Unsigned only.

        s2r_sum_fast is contractually 'same result as s2r_sum', i.e. it reads the
        payload as unsigned; there is no vector kernel for the signed classes. For
        a signed pool this defers to the exact signed reduction rather than
        returning a number that merely looks like a sum.
        """
        return _sum_s(self._p) if self.signed else _sumf(self._p)

    def min(self):
        return _min_s(self._p) if self.signed else _min(self._p)

    def max(self):
        return _max_s(self._p) if self.signed else _max(self._p)

    # --- safe arithmetic (lazy-carry); returns the new class in bits ---
    def add_scalar(self, s):
        rc = _add_safe_s(self._p, int(s)) if self.signed else _add_safe(self._p, int(s))
        if rc == 0:                      # 0 = failure, not "class 0 bits"
            _raise_rc(-2, "add_scalar")
        return rc

    def mul_scalar(self, s):
        rc = _mul_safe_s(self._p, int(s)) if self.signed else _mul_safe(self._p, int(s))
        if rc == 0:
            _raise_rc(-2, "mul_scalar")
        return rc

    # --- persistence ---
    def save(self, path):
        rc = _save(self._p, path.encode())
        if rc != 0:
            raise IOError(f"save failed: {_ERRORS.get(rc, rc)} (code {rc})")

    @classmethod
    def load(cls, path):
        ptr = _load(path.encode())
        if not ptr:
            raise IOError(f"load failed: {path}")
        obj = cls.__new__(cls)
        obj._p = ptr
        return obj   # signedness is read from the pool, never cached here

    def to_list(self):
        return [self[i] for i in range(len(self))]

    def __del__(self):
        try:
            if getattr(self, "_p", None):
                _free(self._p)
                self._p = None
        except Exception:
            pass

    def __repr__(self):
        return f"Pool(n={len(self)}, class={self.class_bits} bits, bytes={self.used_bytes})"


if __name__ == "__main__":
    print("Smart2Raw via Python, lib v" + version())
    p = Pool(S2R_8)
    p.extend([25, 30, 40, 1000, 70000, 5000000000])
    print(p, "sum =", p.sum(), "max =", p.max())
