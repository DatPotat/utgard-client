#!/bin/sh
# Nothing in src/core depends on Windows: file reading and DPAPI live in
# src/win (confread.c, profstore.c). run.sh runs this too.
#
# Usage: sh tests/core_portable.sh   (from the repository root or from tests/)
#   1. text check: no _WIN32 branches, Windows headers or fileio.h in src/core;
#   2. with mingw-w64 on PATH, each src/core/*.c is compiled for Windows with
#      tests/poison first on the include path, where windows.h, wincrypt.h
#      and strsafe.h are #error - a Windows header reached from core fails.
set -u
cd "$(dirname "$0")/.."
fail=0
hits=$(grep -nE '_WIN32|<windows\.h>|<wincrypt\.h>|<strsafe\.h>|"fileio\.h"' src/core/*.c src/core/*.h)
if [ -n "$hits" ]; then
    echo "Windows in src/core:"; echo "$hits"; fail=1
fi
CC=""
for c in x86_64-w64-mingw32-gcc x86_64-w64-mingw32-clang; do
    command -v "$c" >/dev/null 2>&1 && { CC=$c; break; }
done
if [ -n "$CC" ]; then
    for f in src/core/*.c; do
        "$CC" -std=c11 -fsyntax-only -Itests/poison -Isrc -Isrc/core -Ivendor/parson -Ivendor/puff "$f" 2>/dev/null ||
            { echo "reaches a Windows header when built for Windows: $f"; fail=1; }
    done
else
    echo "note: no mingw-w64 compiler on PATH; only the text check ran"
fi
[ $fail -eq 0 ] && echo "core_portable: ok" || { echo "core_portable: FAILED"; exit 1; }
