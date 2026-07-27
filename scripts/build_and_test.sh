#!/usr/bin/env bash
# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terencio Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
# Smart2Raw - build + test across all configurations.
# Usage: scripts/build_and_test.sh   (needs gcc; the header is header-only)
# Expected result: 25 suites OK, 0 failures.
set -u
cd "$(dirname "$0")/.."           # repository root
INC="-I include -I tests -I tests/neon_emu -I tests/rvv_emu -I tests/sve2_emu"
H="include/smart2raw.h"
PASS=0; FAIL=0
ok(){ printf "  \033[32mOK\033[0m  %-42s %s\n" "$1" "$2"; PASS=$((PASS+1)); }
no(){ printf "  \033[31mXX\033[0m  %-42s %s\n" "$1" "$2"; FAIL=$((FAIL+1)); }
run(){ # label ; flags... ; src(last arg)
  local label="$1"; shift; local src="${!#}"; local flags=("${@:1:$#-1}")
  if gcc "${flags[@]}" $INC -o /tmp/s2r_t "tests/$src" 2>/tmp/s2r_e; then
    local out rc; out=$(/tmp/s2r_t 2>&1 | sed 's/\x1b\[[0-9;]*m//g'); rc=$?
    if [ "$src" = "mcu_core.c" ]; then
      [ $rc -eq 0 ] && ok "$label" "exit 0 - adaptive core correct" || no "$label" "exit $rc"
    elif echo "$out" | grep -qE '0 FAIL|PASSED'; then
      ok "$label" "$(echo "$out"|grep -oE '[0-9]+ OK, 0 FAIL|PASSED'|tail -1)"
    else no "$label" "unexpected result"; fi
  else no "$label" "COMPILE FAILURE"; head -3 /tmp/s2r_e|sed 's/^/      /'; fi
}
echo "===== Smart2Raw v$(grep -m1 'define S2R_VERSION_STRING' $H | sed -E 's/.*"([0-9.]+)".*/\1/') - single file ($(wc -l < $H) lines) ====="
echo "-- Functionality (server: -O3 -march=native) --"
run "Single-file C - all modules (35)"         -O3 -march=native test_completo.c
run "Block-wise width PFOR (11)"               -O3 -march=native test_blocked.c
run "Signed PFOR + SIMD sum (7)"               -O3 -march=native test_blocked2.c
run "Integrated regression (30)"               -O3 -march=native test_regressao.c
run "Backward compatibility (26)"              -O3 -march=native test_compat_original.c
run "Signed lazy-carry (11)"                   -O3 -march=native test_signed_lazy.c
run "mmap big-endian COW (7)"                  -O3 -march=native test_be_cow.c
run "Analytics histogram/group-by (13)"        -O3 -march=native test_analytics.c
run "Analytics v2 sort/unique (31)"            -O3 -march=native test_analytics_v2.c
run "Format hardening (.s2r contract)"         -O3 -march=native test_format_hardening.c
run "SIMD predicates vs scalar (143k)"         -O3 -march=native test_filters_simd.c
run "PFOR frame/zone/sorted/serial (5.4k)"     -O3 -march=native test_pfor_frame.c
run "PFOR: binary search forced off"           -O2 -DS2R_BLK_BSEARCH_MIN_BYTES=999999999 test_pfor_frame.c
run "PFOR: binary search forced on"            -O2 -DS2R_BLK_BSEARCH_MIN_BYTES=0 test_pfor_frame.c
echo "-- Adaptability (same file, different gates) --"
run "Edge x86 -O2"                             -O2 test_regressao.c
run "No SIMD (-DS2R_NO_SIMD)"                  -O2 -DS2R_NO_SIMD test_regressao.c
run "Strict ISO C11 (-pedantic)"              -O2 -std=c11 -pedantic test_regressao.c
run "MCU -Os (no stdio/mmap/simd)"             -Os -DS2R_NO_STDIO -DS2R_NO_MMAP -DS2R_NO_SIMD mcu_core.c
echo "-- Emulated hardware (real code on x86; CI repeats on real ARM/BE/RISC-V via QEMU) --"
run "NEON ARM emulated (26)"                   -O2 -D__ARM_NEON=1 test_neon_emu.c
run "Big-endian emulated (6)"                  -O2 test_be_emu.c
run "RISC-V RVV emulated (logic)"              -O2 -DS2R_FORCE_RVV=1 test_rvv_emu.c
run "RISC-V RVV VLEN sweep (128 bits)"         -O2 -DS2R_FORCE_RVV=1 -DS2R_RVV_EMU_K=2 test_rvv_emu.c
run "ARM SVE2 emulated (logic)"                 -O2 -DS2R_FORCE_SVE2=1 test_sve2_emu.c
# The SVE kernels are vector-length agnostic, so the emulated VL is swept rather
# than fixed: the same object code must give the same total at 128..1024 bits.
run "ARM SVE2 VL sweep (128..1024 bits)"        -O2 -DS2R_FORCE_SVE2=1 -DS2R_SVE_EMU_K=2 test_sve2_emu.c
run "ARM SVE2 VL 512 + tiny strip-mine chunk"   -O2 -DS2R_FORCE_SVE2=1 -DS2R_SVE_EMU_K=8 -DS2R_SVE_U8_CHUNK=3 test_sve2_emu.c
echo "------------------------------------------------------------"
echo "  TOTAL: $PASS OK, $FAIL failures"
[ $FAIL -eq 0 ]
