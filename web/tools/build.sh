#!/bin/sh
# Constrói o núcleo wasm a partir do include/smart2raw.h do próprio repositório,
# embute na página de arquivo único e deixa também o binário nativo de referência.
#   uso:  sh tools/build.sh
set -e
cd "$(dirname "$0")/.."
INC=${S2R_INCLUDE:-../include}
mkdir -p dist out

EXPORTS="s2r_probe_input s2r_probe_run s2r_probe_report s2r_probe_slots
         s2r_probe_file s2r_probe_file_len s2r_probe_version
         s2r_probe_count_gt_naive s2r_probe_count_gt_s2r s2r_probe_count_gt_flat
         s2r_probe_count_gt_affine s2r_probe_count_gt_blocked
         s2r_probe_count_range_index s2r_probe_count_range_flat
         s2r_probe_count_range_naive"
EXPFLAGS=""
for f in $EXPORTS; do EXPFLAGS="$EXPFLAGS -Wl,--export=$f"; done

echo "== wasm32 (sem libc: shim/ + src/s2r_rt.c) =="
clang --target=wasm32 -O2 -std=c11 -Wall -Wextra \
      -nostdlib -nostdlibinc -ffreestanding -mbulk-memory -DS2R_NO_SIMD \
      -I shim -I $INC src/s2r_probe.c src/s2r_rt.c -o out/s2r.wasm \
      -Wl,--no-entry -Wl,--initial-memory=16777216 $EXPFLAGS
ls -l out/s2r.wasm

echo "== nativo (a referência contra a qual o wasm é conferido) =="
cc -O2 -std=c11 -Wall -Wextra -I $INC \
   src/s2r_probe.c src/native_shim.c src/probe_main_native.c -o out/probe_native

echo "== página de arquivo único =="
python3 - <<'EOF'
import base64
w = open('out/s2r.wasm','rb').read()
t = open('page/template.html').read()
open('dist/smart2raw-live.html','w').write(t.replace('__WASM_B64__', base64.b64encode(w).decode()))
import os
print("  dist/smart2raw-live.html  %.1f KB" % (os.path.getsize('dist/smart2raw-live.html')/1024))
EOF
