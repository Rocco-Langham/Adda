#!/bin/sh
# Build Adda.   Usage: ./build.sh [debug|release]
set -e
cd "$(dirname "$0")"

MODE="${1:-debug}"
CC="${CC:-gcc}"

# MSYS2 does not put its compiler on the system PATH, so look in the usual
# places before giving up.
if ! command -v "$CC" >/dev/null 2>&1; then
    for dir in /c/msys64/ucrt64/bin /c/msys64/mingw64/bin \
               /c/mingw64/bin /c/w64devkit/bin; do
        if [ -x "$dir/gcc.exe" ] || [ -x "$dir/gcc" ]; then
            PATH="$dir:$PATH"
            export PATH
            break
        fi
    done
fi

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "build.sh: no C compiler found." >&2
    echo "  Install one with:  winget install -e --id MSYS2.MSYS2" >&2
    echo "  then:              /c/msys64/usr/bin/bash -lc 'pacman -S --noconfirm mingw-w64-ucrt-x86_64-gcc'" >&2
    exit 1
fi

WARN="-std=c99 -Wall -Wextra -Wpedantic -Werror"
DEFS="-D__USE_MINGW_ANSI_STDIO=1"

case "$MODE" in
  debug)   FLAGS="-g -O0 -DADDA_DEBUG=1" ;;
  release) FLAGS="-O2 -DNDEBUG" ;;
  *) echo "usage: $0 [debug|release]" >&2; exit 2 ;;
esac

# shellcheck disable=SC2086
"$CC" $WARN $DEFS $FLAGS -o adda src/*.c -lm
echo "built ./adda ($MODE)"
