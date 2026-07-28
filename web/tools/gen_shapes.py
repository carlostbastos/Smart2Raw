#!/usr/bin/env python3
"""Deterministic column shapes, written once and read by BOTH the native build
and the wasm build, so the two reports are comparable field by field."""
import struct, sys

M = (1 << 64) - 1
class R:
    def __init__(s, seed): s.s = seed
    def n(s):
        s.s ^= (s.s << 13) & M; s.s ^= s.s >> 7; s.s ^= (s.s << 17) & M
        return s.s

def shapes():
    r = R(0x9E3779B97F4A7C15)
    out = []
    out.append(("telemetria 0..200",        0, [r.n() % 201 for _ in range(20000)]))
    out.append(("12 distintos 500..11500",  0, [500 + (r.n() % 12) * 1000 for _ in range(20000)]))
    out.append(("timestamps a cada 60 s",   0, [1700000000 + i * 60 for i in range(20000)]))
    out.append(("centavos, passo 25",       0, [1000 + (r.n() % 400) * 25 for _ in range(20000)]))
    out.append(("booleano",                 0, [r.n() & 1 for _ in range(20000)]))
    out.append(("u64 aleatorio",            0, [r.n() for _ in range(20000)]))
    out.append(("ids 0..1e6",               0, [r.n() % 1000000 for _ in range(20000)]))
    out.append(("constante",                0, [777] * 20000))
    out.append(("acima de 2^63",            0, [1 if r.n() % 50 else M for _ in range(20000)]))
    out.append(("topo de u64",              0, [M - (r.n() % 4) for _ in range(20000)]))
    out.append(("crescente",                0, list(range(20000))))
    out.append(("com sinal -500..500",      1, [((r.n() % 1001) - 500) & M for _ in range(20000)]))
    out.append(("com sinal, passo 250",     1, [((r.n() % 41) * 250 - 5000) & M for _ in range(20000)]))
    out.append(("um elemento",              0, [42]))
    out.append(("dois elementos extremos",  0, [1, M]))
    out.append(("platos",                   0, [i // 13 for i in range(20000)]))
    out.append(("esparso",                  0, [0 if i % 7 else 255 for i in range(20000)]))
    return out

def main(path):
    sh = shapes()
    with open(path, "wb") as f:
        f.write(struct.pack("<I", len(sh)))
        for name, sg, v in sh:
            f.write(struct.pack("<II", len(v), sg))
            f.write(struct.pack("<%dQ" % len(v), *v))
    with open(path + ".names", "w") as f:
        for name, sg, v in sh:
            f.write("%s\t%d\t%d\n" % (name, sg, len(v)))
    print("%d shapes -> %s" % (len(sh), path))

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "out/shapes.bin")
