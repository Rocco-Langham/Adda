#!/bin/sh
# Golden-file tests for Adda.
#
#   ./tests/run.sh            run everything
#   ./tests/run.sh --accept   rewrite the .expected files from current output
#
# Each test captures stdout, stderr and the exit code together, so a change to
# an error message or an exit status shows up as a plain diff.
cd "$(dirname "$0")/.." || exit 1

ADDA=./adda.exe
[ -x "$ADDA" ] || ADDA=./adda
if [ ! -x "$ADDA" ]; then
    echo "no adda binary - run ./build.sh first" >&2
    exit 1
fi

accept=0
[ "$1" = "--accept" ] && accept=1

pass=0
fail=0

# tests/*.adda are run as files; tests/repl/*.in are typed at the prompt;
# tests/check/*.adda go through the problem finder (adda --check).
for f in tests/*.adda tests/errors/*.adda tests/repl/*.in tests/check/*.adda; do
    [ -e "$f" ] || continue
    expected="${f%.*}.expected"

    # A test that uses `ask` puts its answers in a sibling .stdin file.
    # Everything else reads from nowhere, so no test can ever sit waiting on
    # the terminal.
    input="${f%.*}.stdin"
    [ -f "$input" ] || input=/dev/null

    case "$f" in
        *.in) out=$("$ADDA" < "$f" 2>&1) ;;
        tests/check/*) out=$("$ADDA" --check "$f" < /dev/null 2>&1) ;;
        *)    out=$("$ADDA" "$f" < "$input" 2>&1) ;;
    esac
    code=$?
    actual=$(printf '%s\nexit: %d\n' "$out" "$code" | tr -d '\r')

    if [ "$accept" = "1" ]; then
        printf '%s\n' "$actual" > "$expected"
        echo "accepted $f"
        continue
    fi

    if [ ! -f "$expected" ]; then
        echo "MISSING GOLDEN: $expected"
        fail=$((fail + 1))
        continue
    fi

    if diff=$(printf '%s\n' "$actual" | diff --strip-trailing-cr -u "$expected" - 2>&1); then
        pass=$((pass + 1))
    else
        echo "FAIL $f"
        printf '%s\n' "$diff" | sed 's/^/    /'
        fail=$((fail + 1))
    fi
done

[ "$accept" = "1" ] && exit 0

# The tidy view is C, not Adda, so it gets a C test: raw -> tidy -> raw has to
# give back exactly what was typed. Skipped when there is no compiler.
CC="${CC:-gcc}"
command -v "$CC" >/dev/null 2>&1 ||
    for dir in /c/msys64/ucrt64/bin /c/msys64/mingw64/bin /c/mingw64/bin; do
        [ -x "$dir/gcc.exe" ] && { PATH="$dir:$PATH"; export PATH; break; }
    done
if command -v "$CC" >/dev/null 2>&1; then
    if "$CC" -std=c99 -I src -o adda-tidytest tests/tidy_roundtrip.c src/tidy.c 2>/dev/null; then
        if ./adda-tidytest; then pass=$((pass + 1)); else fail=$((fail + 1)); fi
        rm -f adda-tidytest adda-tidytest.exe
    else
        echo "FAIL tests/tidy_roundtrip.c did not build"
        fail=$((fail + 1))
    fi
else
    echo "(no compiler: skipped the tidy round-trip test)"
fi

echo
echo "passed $pass, failed $fail"
[ "$fail" -eq 0 ]
