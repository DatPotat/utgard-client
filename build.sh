#!/bin/sh
# Build the Utgard client.
#
# Works in two places:
#   - Git Bash / MSYS2 / w64devkit on Windows  -> native gcc
#   - Linux with mingw-w64 installed           -> x86_64-w64-mingw32-gcc
#
# Usage: ./build.sh [debug|clean]
#   (no argument) release build, symbols stripped
#   debug         keep symbols for gdb
#   clean         remove build/
#
# CC and WINDRES may be set in the environment to override detection.

set -e

SRC="src/main.c src/zapret.c src/link.c src/profiles.c src/ask.c src/net.c src/genconf.c src/singbox.c src/lists.c src/zapret_exclude.c src/apps.c src/pick.c src/settings.c src/tray.c src/fileio.c src/autostart.c src/update.c src/shellopen.c vendor/parson/parson.c"
RC="res/utgard.rc"
# The executable ships in bin\, and the client treats bin\'s parent as the
# product root. Building anywhere else would give development a different
# root than the real layout.
OUT="bin/utgard.exe"
RES="build/utgard.res"

# ---- pick a toolchain ------------------------------------------------

if [ -n "$CC" ]; then
    : "${WINDRES:=windres}"
elif command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    CC=x86_64-w64-mingw32-gcc
    WINDRES=x86_64-w64-mingw32-windres
elif command -v gcc >/dev/null 2>&1; then
    CC=gcc
    WINDRES=windres
else
    echo "Компилятор не найден." >&2
    echo "В обычном Git Bash gcc отсутствует — нужен MSYS2 UCRT64 или w64devkit." >&2
    exit 1
fi

if ! command -v "$WINDRES" >/dev/null 2>&1; then
    echo "Нет $WINDRES — без него не встроить манифест." >&2
    exit 1
fi

# The compiler must target Windows, not the MSYS/Cygwin emulation layer:
# an MSYS-targeted gcc produces a binary that needs msys-2.0.dll to start.
TARGET=$("$CC" -dumpmachine)
case "$TARGET" in
    x86_64*mingw*) ;;
    *msys*|*cygwin*)
        echo "Компилятор нацелен на $TARGET." >&2
        echo "Это gcc среды MSYS/Cygwin: его программы требуют msys-2.0.dll рядом." >&2
        echo "Нужен gcc из UCRT64/MINGW64 или из w64devkit." >&2
        exit 1 ;;
    mingw32|i686*)
        echo "Компилятор нацелен на $TARGET — это 32-битный MinGW.org." >&2
        echo "Он не умеет 64-битные программы и не поддерживает -municode." >&2
        echo "Нужен mingw-w64 (w64devkit или MSYS2 UCRT64)." >&2
        exit 1 ;;
    *)  echo "Компилятор нацелен на $TARGET, а нужен x86_64-*-mingw32." >&2
        exit 1 ;;
esac

mkdir -p build

# The version string can lie about what the toolchain supports; a wide entry
# point is what actually breaks on MinGW.org, so test exactly that.
cat > build/.probe.c <<'PROBE'
#include <windows.h>
int WINAPI wWinMain(HINSTANCE a, HINSTANCE b, PWSTR c, int d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
PROBE
if ! "$CC" -municode -mwindows build/.probe.c -o build/.probe.exe >/dev/null 2>&1; then
    rm -f build/.probe.c build/.probe.exe
    echo "Компилятор не собрал пробу с -municode и wWinMain." >&2
    echo "В crt нет стартового кода для Unicode — нужен mingw-w64." >&2
    exit 1
fi
rm -f build/.probe.c build/.probe.exe

# ---- flags -----------------------------------------------------------

WARN="-std=c11 -Wall -Wextra -Werror"
# Stack canaries on functions with local arrays or taken addresses. ASLR,
# DEP and high-entropy ASLR are already on: modern binutils sets them by
# default for PE, and the check at the end of this script confirms it.
# -static keeps the support code for the canaries inside the executable:
# without it gcc links libssp-0.dll dynamically, and a bare utgard.exe in an
# empty folder would refuse to start. Windows' own DLLs stay imports either
# way - mingw only has import libraries for them.
HARDEN="-fstack-protector-strong -static"
INCLUDE="-Ivendor/parson -Isrc"
DEFS="-DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE"
LINK="-municode -mwindows"

if [ "$1" = "debug" ]; then
    MODE="-g -O0"
    MODE_NAME="отладочная, с символами"
else
    MODE="-O2 -s"
    MODE_NAME="рабочая, символы убраны"
fi
LIBS="-lgdi32 -luser32 -ldwmapi -lole32 -luuid -lcrypt32 -lwinhttp -lws2_32 -liphlpapi -luxtheme -lbcrypt -lcomctl32 -loleaut32 -ltaskschd"

# Source is UTF-8 and UI strings are wide literals. Pinning the charsets
# makes that explicit, but the options need libiconv, which some GCC
# builds (w64devkit) ship without. Use them only if this compiler takes them.
CHARSET=""
if echo 'int main(void){return 0;}' > build/.probe.c 2>/dev/null &&
   "$CC" -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE \
        -c build/.probe.c -o build/.probe.o >/dev/null 2>&1; then
    CHARSET="-finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE"
fi
rm -f build/.probe.c build/.probe.o

# ---- build -----------------------------------------------------------

MISSING=""
for f in $SRC src/zapret.h src/link.h src/profiles.h src/ask.h src/net.h src/lists.h src/genconf.h src/singbox.h src/autostart.h src/update.h src/shellopen.h src/version.h res/utgard.rc res/utgard.manifest res/utgard.ico; do
    [ -f "$f" ] || MISSING="$MISSING $f"
done
if [ -n "$MISSING" ]; then
    echo "Нет файлов:$MISSING" >&2
    echo "Запускать build.sh надо из корня репозитория, рядом с src/ и res/." >&2
    exit 1
fi

if [ "$1" = "clean" ]; then
    rm -rf build "$OUT"
    echo "Очищено."
    exit 0
fi

mkdir -p build bin

# -J sets the input format, --include-dir finds utgard.manifest next to the
# .rc file. Older windres also accepted -I as the input format, so the long
# form is the only unambiguous spelling.
"$WINDRES" -J rc --include-dir=res "$RC" -O coff -o "$RES"
# shellcheck disable=SC2086
"$CC" $WARN $HARDEN $INCLUDE $DEFS $LINK $MODE $CHARSET $SRC "$RES" -o "$OUT" $LIBS

# Check the executable itself, not the flags that were meant to produce it.
# Catches two things that already went wrong once: a gcc support DLL sneaking
# in as a dependency (the exe would not start alone), and a missing ASLR/DEP.
case "$CC" in
    x86_64-w64-mingw32-*) OBJDUMP=x86_64-w64-mingw32-objdump ;;
    *)                    OBJDUMP=objdump ;;
esac
if command -v "$OBJDUMP" >/dev/null 2>&1; then
    HEAD=$("$OBJDUMP" -p "$OUT")
    EXTRA=$(printf '%s\n' "$HEAD" | grep "DLL Name:" | grep -i "lib.*-[0-9]*\.dll" || true)
    if [ -n "$EXTRA" ]; then
        echo "Экзешник зависит от библиотек, которых нет в Windows:" >&2
        echo "$EXTRA" >&2
        exit 1
    fi
    for flag in DYNAMIC_BASE NX_COMPAT HIGH_ENTROPY_VA; do
        if ! printf '%s\n' "$HEAD" | grep -q "$flag"; then
            echo "В экзешнике не включён $flag" >&2
            exit 1
        fi
    done
    CHECKED="проверен: только системные DLL, ASLR, DEP, high-entropy VA"
else
    CHECKED="не проверен — нет $OBJDUMP"
fi

echo "Собрано: $OUT"
echo "  компилятор: $CC ($TARGET)"
echo "  экзешник:   $CHECKED"
echo "  сборка:     $MODE_NAME"
if [ -n "$CHARSET" ]; then
    echo "  кодировки:  заданы явно"
else
    echo "  кодировки:  по умолчанию (компилятор без libiconv)"
fi
