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

# interpreter (listed by hand, because gui.c has its own entry point and must
# not be linked into the console build)
"$CC" $WARN $DEFS $FLAGS -o adda \
    src/arena.c src/error.c src/value.c src/map.c \
    src/lexer.c src/parser.c src/interp.c src/repl.c src/main.c -lm
echo "built ./adda ($MODE)"

# GUI (Windows only). Same warning flags as everything else.
# The manifest resource is what gives themed controls and DPI awareness;
# without it the buttons are drawn in the Windows 95 style and the window is
# stretched and blurry on a high-DPI screen.
if [ "$(uname -o 2>/dev/null)" = "Msys" ] || [ "$OS" = "Windows_NT" ]; then
    "${WINDRES:-windres}" -I src src/adda-gui.rc -o adda-gui-res.o
    "$CC" $WARN $DEFS $FLAGS -mwindows -o adda-gui \
        src/gui.c adda-gui-res.o \
        -lcomctl32 -ldwmapi -luxtheme -lgdi32
    echo "built ./adda-gui ($MODE)"
fi
