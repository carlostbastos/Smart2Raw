#!/usr/bin/env python3
# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
"""s2r_convert.py - single-cycle, single-core converter, in Python.

Same cycle as s2r_convert.c, driven by Python over the shared library:
CONVERT (text -> compact, classify) -> PROCESS in place on the compact form
(overflow-safe arithmetic, no truncation) -> DECONVERT (compact -> text). The
overflow ceiling is --cap (default 32): if the operation would exceed it, REFUSE.

Usage: python s2r_convert.py <in.txt|-> <out.txt> [--op none|add|mul] [--by N]
                            [--signed] [--cap 32|64]
"""
import sys, argparse
from smart2raw import Pool, S2R_8, S2R_I8

def read_ints(path):
    f = sys.stdin if path == "-" else open(path)
    data = [int(tok) for tok in f.read().split()]
    if f is not sys.stdin: f.close()
    return data

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp"); ap.add_argument("out")
    ap.add_argument("--op", choices=["none", "add", "mul"], default="none")
    ap.add_argument("--by", type=int, default=0)
    ap.add_argument("--signed", action="store_true")
    ap.add_argument("--cap", type=int, default=32)
    a = ap.parse_args()

    vals = read_ints(a.inp)
    # (1) CONVERTE
    p = Pool(S2R_I8 if a.signed else S2R_8)
    p.extend(vals)
    cls_in = p.class_bits

    # (2) PROCESSA com teto de overflow --cap
    if a.op != "none" and len(p) > 0:
        mx = abs(p.max())
        proj = mx + a.by if a.op == "add" else mx * a.by
        teto = (1 << 64) - 1 if a.cap >= 64 else (1 << 32) - 1
        if abs(proj) > teto:
            sys.stderr.write(f"REFUSED: would exceed the {a.cap}-bit ceiling (no truncation).\n")
            sys.exit(7)
        (p.add_scalar if a.op == "add" else p.mul_scalar)(a.by)
    cls_out = p.class_bits

    # (3) DESCONVERTE
    with open(a.out, "w") as f:
        for v in p.to_list():
            f.write(f"{v}\n")
    sys.stderr.write(
        f"1-core cycle: converted ({abs(cls_in)} bits) -> processed "
        f"[op={a.op} by={a.by} cap={a.cap}] -> deconverted ({abs(cls_out)} bits) -> {a.out}\n")

if __name__ == "__main__":
    main()
