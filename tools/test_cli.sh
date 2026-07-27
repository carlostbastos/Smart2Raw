#!/usr/bin/env bash
# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# CLI regression tests. The tools had no coverage at all, which is how three
# silent wrong-answer bugs survived:
#
#   * `agg sum` on a signed pool used (int64_t)s2r_sum(), which only works at
#     class 64; {-1,-128,-5,100} as i8 summed to 734 instead of -34.
#   * `agg count-gt` / `count-range` parsed the threshold with strtoull and
#     called the unsigned kernels even on signed pools; `count-gt 0` over that
#     same pool answered 4 instead of 1.
#   * `info` read `count` from offset 8 for every magic, but pre-3.3 files keep
#     it at offset 12, so legacy files reported a nonsense count and size.
#
# Usage: tools/test_cli.sh    (builds the tools first)
set -u
cd "$(dirname "$0")"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
PASS=0; FAIL=0

check() { # label ; expected ; actual
  if [ "$2" = "$3" ]; then
    printf "  \033[32mOK\033[0m  %-44s %s\n" "$1" "$3"; PASS=$((PASS+1))
  else
    printf "  \033[31mXX\033[0m  %-44s got '%s', want '%s'\n" "$1" "$3" "$2"; FAIL=$((FAIL+1))
  fi
}

make >/dev/null 2>&1 || { echo "build failed"; exit 1; }

# ---- signed pool: every aggregation must be sign-aware ----
printf -- '-1\n-128\n-5\n100\n' > "$TMP/sg.txt"
./s2r pack "$TMP/sg.txt" "$TMP/sg.s2r" --signed >/dev/null
check "signed sum"                 "-34"  "$(./s2r agg "$TMP/sg.s2r" sum)"
check "signed min"                 "-128" "$(./s2r agg "$TMP/sg.s2r" min)"
check "signed max"                 "100"  "$(./s2r agg "$TMP/sg.s2r" max)"
check "signed count-gt 0"          "1"    "$(./s2r agg "$TMP/sg.s2r" count-gt 0)"
check "signed count-gt -6"         "3"    "$(./s2r agg "$TMP/sg.s2r" count-gt -6)"
check "signed count-range -10..0"  "2"    "$(./s2r agg "$TMP/sg.s2r" count-range -10 0)"

# ---- unsigned pool must keep working (class promotion u8 -> u32) ----
printf '1\n2\n300\n70000\n' > "$TMP/us.txt"
./s2r pack "$TMP/us.txt" "$TMP/us.s2r" >/dev/null
check "unsigned sum"               "70303" "$(./s2r agg "$TMP/us.s2r" sum)"
check "unsigned count-gt 2"        "2"     "$(./s2r agg "$TMP/us.s2r" count-gt 2)"
check "unsigned count-range 2..300" "2"    "$(./s2r agg "$TMP/us.s2r" count-range 2 300)"

# ---- round trip through text must be exact ----
./s2r unpack "$TMP/sg.s2r" "$TMP/sg_out.txt" >/dev/null
check "signed round trip"   "$(tr -d ' ' < "$TMP/sg.txt")" "$(cat "$TMP/sg_out.txt")"
./s2r unpack "$TMP/us.s2r" "$TMP/us_out.txt" >/dev/null
check "unsigned round trip" "$(tr -d ' ' < "$TMP/us.txt")" "$(cat "$TMP/us_out.txt")"

# ---- verify: intact accepted, corrupted rejected ----
./s2r verify "$TMP/sg.s2r" >/dev/null 2>&1
check "verify intact -> exit 0" "0" "$?"
cp "$TMP/sg.s2r" "$TMP/bad.s2r"
printf '\x99' | dd of="$TMP/bad.s2r" bs=1 seek=16 count=1 conv=notrunc status=none
./s2r verify "$TMP/bad.s2r" >/dev/null 2>&1
check "verify corrupted -> nonzero" "1" "$?"

# ---- info: count and expected size, for BOTH header layouts ----
check "info v3.3 count"    "count   : 4 elements" "$(./s2r info "$TMP/sg.s2r" | grep '^count')"
check "info v3.3 size"     "size    : 24 bytes  (expected 24 = 16 + 4*1 + 4)" \
                           "$(./s2r info "$TMP/sg.s2r" | grep '^size')"
python3 - "$TMP/legacy.s2r" <<'PY'
import struct, sys
# pre-3.3 layout: magic(4) + hdr(8) + count(8) + payload, no CRC trailer
blob = (struct.pack('<I', 0x32523253) + bytes(8) + struct.pack('<Q', 5)
        + struct.pack('<5H', 1, 2, 3, 70, 300))
blob = blob[:4] + bytes([16]) + blob[5:]      # class = u16
open(sys.argv[1], 'wb').write(blob)
PY
check "info legacy count"  "count   : 5 elements" "$(./s2r info "$TMP/legacy.s2r" | grep '^count')"
check "info legacy size"   "size    : 30 bytes  (expected 30 = 20 + 5*2)" \
                           "$(./s2r info "$TMP/legacy.s2r" | grep '^size')"

# ---- a nonexistent / truncated file must not crash ----
./s2r info "$TMP/nope.s2r" >/dev/null 2>&1
check "info missing file -> nonzero" "1" "$?"
head -c 10 "$TMP/sg.s2r" > "$TMP/trunc.s2r"
./s2r info "$TMP/trunc.s2r" >/dev/null 2>&1
check "info truncated -> nonzero" "1" "$?"

echo "------------------------------------------------------------"
echo "  CLI: $PASS OK, $FAIL failures"
[ "$FAIL" -eq 0 ]
