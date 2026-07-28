#!/bin/sh
# A verificação inteira, em um comando.
#   1. wasm x nativo: o MESMO relatório, campo por campo, em 17 formas de coluna
#   2. alocador sob repetição: 8 rodadas idênticas, memória estável
#   3. a página num navegador de verdade (headless), 10 formas, com download
#   4. o gêmeo POSIX do executável de Windows, rodando de fato
set -e
cd "$(dirname "$0")/.."
sh tools/build.sh
python3 tools/gen_shapes.py out/shapes.bin
./out/probe_native out/shapes.bin > out/native.txt
node tools/wasm_report.js out/s2r.wasm out/shapes.bin > out/wasm.txt
if diff -q out/native.txt out/wasm.txt >/dev/null; then
  echo "OK  wasm x nativo: relatorios identicos ($(wc -l < out/native.txt) formas)"
else
  echo "FALHA  wasm e nativo divergiram"; diff out/native.txt out/wasm.txt | head; exit 1
fi
node tools/stress.js
clang -O2 -std=c11 -Wall -Wextra -DS2R_HOSTED -I ${S2R_INCLUDE:-../include} \
      src/probe_main_win.c src/s2r_probe.c src/native_shim.c src/win_posix_shim.c -o out/probe_hosted
python3 tools/gen_csv.py out/amostra.csv
./out/probe_hosted out/amostra.csv --coluna 3 > out/hosted.txt && echo "OK  gemeo POSIX do .exe: saiu 0"
tail -6 out/hosted.txt
if node -e "require('playwright')" 2>/dev/null; then node tools/page_test.js; else
  echo "-- playwright ausente: teste de navegador pulado"; fi
