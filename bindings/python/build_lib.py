#!/usr/bin/env python3
# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Builds the Smart2Raw shared library for the current OS.

Linux  -> libsmart2raw.so      Windows -> smart2raw.dll
macOS  -> libsmart2raw.dylib

Needs a C compiler (gcc/clang on POSIX; mingw gcc or MSVC 'cl' on Windows).
Usage: python build_lib.py
"""
import os, sys, subprocess, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "s2r_capi.c")
INC = os.path.join(HERE, "..", "..", "include")

def run(cmd):
    print(" ", " ".join(cmd)); subprocess.check_call(cmd)

def main():
    plat = sys.platform
    if plat.startswith("win"):
        out = os.path.join(HERE, "smart2raw.dll")
        if shutil.which("gcc"):
            run(["gcc", "-O2", "-shared", "-I", INC, SRC, "-o", out])
        elif shutil.which("cl"):
            run(["cl", "/O2", "/LD", f"/I{INC}", SRC, f"/Fe:{out}"])
        else:
            sys.exit("Nenhum compilador C encontrado (instale MinGW gcc ou MSVC).")
    else:
        ext = "dylib" if plat == "darwin" else "so"
        out = os.path.join(HERE, f"libsmart2raw.{ext}")
        cc = os.environ.get("CC", "cc")
        run([cc, "-O2", "-shared", "-fPIC", "-I", INC, SRC, "-o", out])
    print("OK:", out)

if __name__ == "__main__":
    main()
