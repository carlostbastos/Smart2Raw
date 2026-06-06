# bindings/python — the Windows path ("Python in charge")

Works on Windows, Linux and macOS. Python loads a shared library and drives it
via `ctypes`.

## Steps
```sh
python build_lib.py          # builds libsmart2raw.so / .dylib / smart2raw.dll
python -c "import smart2raw; print(smart2raw.version())"
```

## API (`smart2raw.py`)
```python
from smart2raw import Pool, S2R_8
p = Pool(S2R_8)
p.extend([25, 30, 40, 1000, 70000, 5_000_000_000])  # grows the class by itself
print(p.class_bits, p.used_bytes, p.sum(), p.min(), p.max())
p.add_scalar(5)            # safe arithmetic (lazy-carry), no truncation
p.save("data.s2r")
q = Pool.load("data.s2r") # interoperates with the C CLI/library
```

## Single-cycle converter (`s2r_convert.py`)
Same cycle as `tools/s2r_convert.c`: convert -> process on the compact data ->
unconvert, single-core, with a `--cap` ceiling (default 32; refuses instead of
truncating).
```sh
python s2r_convert.py input.txt output.txt --op add --by 5 --cap 32
```

Locating the lib: next to the module, or via the `S2R_LIB` environment variable.
`.s2r` files are portable between Python and the C CLI (same format, CRC32).
