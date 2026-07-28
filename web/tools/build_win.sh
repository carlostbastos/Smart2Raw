#!/bin/sh
# Gera s2r-probe.exe: PE64 de console, sem CRT, sem DLL de runtime, só kernel32.
# Cruzado a partir do Linux com clang + lld-link + llvm-dlltool (nenhum SDK da
# Microsoft é necessário).
#   uso:  sh tools/build_win.sh
set -e
cd "$(dirname "$0")/.."
INC=${S2R_INCLUDE:-../include}
mkdir -p dist out
TGT=x86_64-pc-windows-msvc
CFLAGS="-O2 -std=c11 -Wall -Wextra -nostdlib -nostdlibinc -ffreestanding -fno-builtin"

llvm-dlltool -m i386:x86-64 -d win/kernel32.def -l out/kernel32.lib
clang --target=$TGT $CFLAGS -DS2R_NO_SIMD -I shim -I $INC -c src/s2r_probe.c      -o out/w_probe.obj
clang --target=$TGT $CFLAGS               -I shim -I $INC -c src/s2r_rt.c        -o out/w_rt.obj
clang --target=$TGT $CFLAGS               -I shim -I $INC -c src/probe_main_win.c -o out/w_main.obj
clang --target=$TGT $CFLAGS               -I shim -I $INC -c src/win_stubs.c      -o out/w_stubs.obj
lld-link /nologo /subsystem:console /entry:mainCRTStartup /out:dist/s2r-probe.exe \
         out/w_probe.obj out/w_rt.obj out/w_main.obj out/w_stubs.obj out/kernel32.lib
ls -l dist/s2r-probe.exe
llvm-readobj --coff-imports dist/s2r-probe.exe | grep -E "Name: |Symbol: " || true
