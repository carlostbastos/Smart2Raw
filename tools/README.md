# tools/ — command-line utilities

Implemented (build: `make` in this folder; needs `gcc`):

## `s2r` — main CLI
```
s2r pack   <in.txt|-> <out.s2r> [--signed]    # text integers -> .s2r (classifies)
s2r unpack <in.s2r> <out.txt>                 # .s2r -> text integers
s2r info   <file.s2r>                         # metadata
s2r verify <file.s2r>                         # integrity (magic, class, count, CRC)
s2r agg    <file.s2r> sum|min|max|count-gt N|count-range A B
```

## `s2r_verify` — identity/integrity verifier (Form 2)
Checks field by field (magic 0x33335253, fmt, class, count vs. size) and
**recomputes the payload CRC32**. Exit code 0 = INTACT, !=0 = invalid/
corrupted — suitable for scripts and CI.
```
s2r_verify <file.s2r>
```

## `s2r_convert` — single-cycle, single-core converter
Cycle: **convert** (text -> compact, classifies) -> **process** in place on the
compact data (safe arithmetic, no truncation) -> **unconvert** (compact ->
text). The overflow ceiling is `--cap` (default **32**): if the operation would
push the result above 32 bits, it **refuses** instead of promoting beyond that
("32-bit overflow only"). `--cap 64` allows up to 64 bits.
```
s2r_convert <in|-> <out> [--op none|add|mul] [--by N] [--signed] [--cap 32|64]
```

> Note: the CLI reads/writes integers as decimal text (one per line or
> space-separated). For binary data, use the library directly (`s2r_save_portable`).
