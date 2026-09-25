#!/bin/sh
# Build the Utgard client for Windows x64 and arm64.
#
# Works in two places:
#   - Git Bash / MSYS2 / w64devkit on Windows  -> native gcc (x64)
#   - Linux with mingw-w64                      -> x86_64-w64-mingw32-gcc
# arm64 needs an aarch64 mingw toolchain, such as llvm-mingw
# (aarch64-w64-mingw32-clang) on PATH.
#
# Usage: [ARCH=x64|arm64] ./build.sh [debug|clean]
#   (no ARCH)     both architectures
#   (no argument) release build, symbols stripped
#   debug         keep symbols for gdb
#   clean         remove the build and output folders
#
# Output: bin/x64/ and bin/arm64/, each a complete product folder -
# utgard.exe and licenses/ - ready to be packed into its own archive.
# CC and WINDRES may be set in the environment to override detection
# (only when building a single ARCH).

set -e

# ---- what every architecture shares ---------------------------------

SRC="$(ls src/core/*.c src/win/*.c src/ui/*.c | sort | tr '\n' ' ')vendor/parson/parson.c vendor/puff/puff.c"
RC="res/utgard.rc"
NEED="src/version.h vendor/puff/puff.h $RC res/utgard.manifest.in res/utgard.ico res/utgard-tray-off.ico res/utgard-tray-on.ico licenses/UTGARD-MIT.txt licenses/PARSON-MIT.txt licenses/THIRD-PARTY-NOTICES.txt"

MISSING=""
for f in $SRC $NEED; do
    [ -f "$f" ] || MISSING="$MISSING $f"
done
if [ -n "$MISSING" ]; then
    echo "Нет файлов:$MISSING" >&2
    echo "Запускать build.sh надо из корня репозитория, рядом с src/ и res/." >&2
    exit 1
fi

# The version lives in src/version.h only; the manifest is stamped from it.
VERSION=$(sed -n 's/^#define UTGARD_VERSION "\([0-9]*\.[0-9]*\.[0-9]*\)"$/\1/p' src/version.h)
if [ -z "$VERSION" ]; then
    echo "В src/version.h нет строки #define UTGARD_VERSION \"X.Y.Z\"." >&2
    exit 1
fi

WARN="-std=c11 -Wall -Wextra -Wmissing-declarations -Werror"
# Stack canaries on functions with local arrays or taken addresses. ASLR,
# DEP and high-entropy ASLR are already on: modern binutils sets them by
# default for PE, and the check below confirms it. -static keeps the
# support code for the canaries inside the executable: without it gcc links
# libssp-0.dll dynamically, and a bare utgard.exe would refuse to start.
HARDEN="-fstack-protector-strong -static"
INCLUDE="-Ivendor/parson -Ivendor/puff -Isrc -Isrc/core -Isrc/win -Isrc/ui"
DEFS="-DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE"
LINK="-municode -mwindows"
LIBS="-lgdi32 -luser32 -ldwmapi -lole32 -luuid -lcrypt32 -lwinhttp -lws2_32 -liphlpapi -luxtheme -lbcrypt -lcomctl32 -loleaut32"

if [ "$1" = "debug" ]; then
    MODE="-g -O0"
    MODE_NAME="отладочная, с символами"
else
    MODE="-O2 -s"
    MODE_NAME="рабочая, символы убраны"
fi

# ---- one architecture -------------------------------------------------

build_arch() {
    ARCH=$1
    case "$ARCH" in
        x64)   TRIPLE=x86_64-w64-mingw32 ;;
        arm64) TRIPLE=aarch64-w64-mingw32 ;;
    esac
    BIN="bin/$ARCH"
    BUILD="build/$ARCH"
    OUT="$BIN/utgard.exe"

    if [ "$2" = "clean" ]; then
        rm -rf "$BUILD" "$BIN"
        echo "Очищено: $ARCH"
        return
    fi

    # Toolchain: the caller's, then gcc or clang for the triple, then plain
    # gcc for x64 (MSYS2 UCRT64, w64devkit).
    if [ -n "$CC_ONE" ]; then
        CC=$CC_ONE; WINDRES=${WINDRES_ONE:-windres}
    elif command -v "$TRIPLE-gcc" >/dev/null 2>&1; then
        CC=$TRIPLE-gcc; WINDRES=$TRIPLE-windres
    elif command -v "$TRIPLE-clang" >/dev/null 2>&1; then
        CC=$TRIPLE-clang; WINDRES=$TRIPLE-windres
    elif [ "$ARCH" = x64 ] && command -v gcc >/dev/null 2>&1; then
        CC=gcc; WINDRES=windres
    else
        echo "Компилятор для $ARCH не найден ($TRIPLE-gcc или $TRIPLE-clang)." >&2
        if [ "$ARCH" = arm64 ]; then
            echo "Для arm64 нужен llvm-mingw (https://github.com/mstorsjo/llvm-mingw/releases," >&2
            echo "на Windows x64 — архив ...-ucrt-x86_64.zip); путь к его bin добавьте в конец PATH:" >&2
            echo "  PATH=\"\$PATH:/путь/к/llvm-mingw/bin\" ./build.sh" >&2
            echo "Только x64: ARCH=x64 ./build.sh" >&2
        else
            echo "В обычном Git Bash gcc отсутствует — нужен MSYS2 UCRT64 или w64devkit." >&2
        fi
        exit 1
    fi
    if ! command -v "$WINDRES" >/dev/null 2>&1; then
        echo "Нет $WINDRES — без него не встроить манифест." >&2
        exit 1
    fi

    # gcc names the target *-w64-mingw32, clang (llvm-mingw) *-w64-windows-gnu.
    TARGET=$("$CC" -dumpmachine 2>/dev/null || echo unknown)
    case "$ARCH:$TARGET" in
        x64:x86_64*mingw*|x64:x86_64-w64-windows-gnu|arm64:aarch64*mingw*|arm64:aarch64-w64-windows-gnu) ;;
        *)  echo "Компилятор $CC нацелен на $TARGET, а для $ARCH нужен $TRIPLE." >&2
            exit 1 ;;
    esac

    mkdir -p "$BUILD" "$BIN"

    # The version string can lie about what the toolchain supports; a wide
    # entry point is what actually breaks on MinGW.org, so test exactly that.
    cat > "$BUILD"/.probe.c <<'PROBE'
#include <windows.h>
int WINAPI wWinMain(HINSTANCE a, HINSTANCE b, PWSTR c, int d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
PROBE
    if ! "$CC" -municode -mwindows "$BUILD"/.probe.c -o "$BUILD"/.probe.exe >/dev/null 2>&1; then
        rm -f "$BUILD"/.probe.c "$BUILD"/.probe.exe
        echo "Компилятор не собрал пробу с -municode и wWinMain — нужен mingw-w64." >&2
        exit 1
    fi
    rm -f "$BUILD"/.probe.c "$BUILD"/.probe.exe

    # Source is UTF-8 and UI strings are wide literals. The charset options
    # need libiconv, which some builds ship without: used only if taken.
    CHARSET=""
    echo 'int main(void){return 0;}' > "$BUILD"/.probe.c
    if "$CC" -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE \
            -c "$BUILD"/.probe.c -o "$BUILD"/.probe.o >/dev/null 2>&1; then
        CHARSET="-finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE"
    fi
    rm -f "$BUILD"/.probe.c "$BUILD"/.probe.o

    # The manifest carries the version as four numbers. The generated copy
    # sits in the build folder, found first through --include-dir.
    sed "s/@VERSION4@/$VERSION.0/" res/utgard.manifest.in > "$BUILD/utgard.manifest"
    "$WINDRES" -J rc --include-dir="$BUILD" --include-dir=res "$RC" -O coff -o "$BUILD/utgard.res"
    # shellcheck disable=SC2086
    "$CC" $WARN $HARDEN $INCLUDE $DEFS $LINK $MODE $CHARSET $SRC "$BUILD/utgard.res" -o "$OUT" $LIBS

    # Check the executable itself, not the flags that were meant to produce
    # it: a gcc support DLL sneaking in as a dependency, or missing ASLR/DEP.
    case "$CC" in
        "$TRIPLE"-*) OBJDUMP=$TRIPLE-objdump ;;
        *)           OBJDUMP=objdump ;;
    esac
    if command -v "$OBJDUMP" >/dev/null 2>&1; then
        HEAD=$("$OBJDUMP" -p "$OUT")
        EXTRA=$(printf '%s\n' "$HEAD" | grep "DLL Name:" | grep -i "lib.*-[0-9]*\.dll" || true)
        if [ -n "$EXTRA" ]; then
            echo "Экзешник $ARCH зависит от библиотек, которых нет в Windows:" >&2
            echo "$EXTRA" >&2
            exit 1
        fi
        for flag in DYNAMIC_BASE NX_COMPAT HIGH_ENTROPY_VA; do
            if ! printf '%s\n' "$HEAD" | grep -q "$flag"; then
                echo "В экзешнике $ARCH не включён $flag" >&2
                exit 1
            fi
        done
        CHECKED="проверен: только системные DLL, ASLR, DEP, high-entropy VA"
    else
        CHECKED="не проверен — нет $OBJDUMP"
    fi

    # The exe carries Parson and puff, whose notices must travel with it.
    mkdir -p "$BIN/licenses"
    cp licenses/UTGARD-MIT.txt licenses/PARSON-MIT.txt licenses/THIRD-PARTY-NOTICES.txt "$BIN/licenses/"

    echo "Собрано: $OUT (Utgard $VERSION)"
    echo "  компилятор: $CC ($TARGET)"
    echo "  экзешник:   $CHECKED"
    echo "  сборка:     $MODE_NAME"
    if [ -n "$CHARSET" ]; then
        echo "  кодировки:  заданы явно"
    else
        echo "  кодировки:  по умолчанию (компилятор без libiconv)"
    fi
}

# ---- which architectures ----------------------------------------------

case "${ARCH:-both}" in
    both)      ARCHES="x64 arm64"; CC_ONE=""; WINDRES_ONE="" ;;
    x64|arm64) ARCHES=$ARCH; CC_ONE=${CC:-}; WINDRES_ONE=${WINDRES:-} ;;
    *)         echo "ARCH=$ARCH не поддерживается: x64 или arm64 (без ARCH — обе)." >&2
               exit 1 ;;
esac

for a in $ARCHES; do
    build_arch "$a" "$1"
done
