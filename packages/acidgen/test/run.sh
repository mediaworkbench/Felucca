#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# acidgen's checks on the host: CC=gcc / clang ... test/run.sh (needs a C99 compiler, libm; python3 for the model's
# reference: without it the model check is skipped and reported). Exit 0: all passed.
#   1. src/acidgen.c strict: -std=c99 -pedantic -Wall -Wextra -Werror, with and without AICD
#   2. src/acidgen_model.h == tools/gen_aicd.py's from model/lm.js (the tables byte for byte)
#   3. test/acidgen_test.c: TB-3PO against X0X's float reference (test/ref/), AICD against the exact model and Aicd's
#      statistics, the self-test hashes, the settings bytes, a line's rules; again with ACIDGEN_AICD=0
#   4. examples/sequencer_adapter.c builds and runs
set -e
cd "$(dirname "$0")/.."
CC=${CC:-cc}
OUT=build
mkdir -p "$OUT"
fail=0

echo "== strict compile"
$CC -std=c99 -pedantic -Wall -Wextra -Werror -O2 -Isrc -c src/acidgen.c -o "$OUT/acidgen.o"
$CC -std=c99 -pedantic -Wall -Wextra -Werror -O2 -Isrc -c src/acidgen_selftest.c -o "$OUT/acidgen_selftest.o"
$CC -std=c99 -pedantic -Wall -Wextra -Werror -O2 -DACIDGEN_AICD=0 -Isrc -c src/acidgen.c -o "$OUT/acidgen_noaicd.o"
echo "ok"

REF=""
if command -v python3 >/dev/null 2>&1; then
    echo "== the model tables against model/lm.js"
    python3 tools/gen_aicd.py "$OUT/acidgen_model.h" "$OUT/aicd_ref.txt"
    if [ "$(tail -n +3 "$OUT/acidgen_model.h" | cksum)" = "$(tail -n +3 src/acidgen_model.h | cksum)" ]; then
        echo "ok"
    else
        echo "FAIL: src/acidgen_model.h is not tools/gen_aicd.py's (regenerate: python3 tools/gen_aicd.py src/acidgen_model.h)"
        fail=1
    fi
    REF="$OUT/aicd_ref.txt"
else
    echo "== no python3: the model's exact check is SKIPPED (the rest runs; install python3 for a full check)"
fi

echo "== acidgen_test"
$CC -O2 -w -ffp-contract=off -Isrc -o "$OUT/acidgen_test" test/acidgen_test.c test/ref/x0x_tb3po_ref.c -lm
"$OUT/acidgen_test" $REF || fail=1
echo "== acidgen_test, ACIDGEN_AICD=0"
$CC -O2 -w -ffp-contract=off -DACIDGEN_AICD=0 -Isrc -o "$OUT/acidgen_test_noaicd" test/acidgen_test.c \
    test/ref/x0x_tb3po_ref.c -lm
"$OUT/acidgen_test_noaicd" || fail=1

echo "== examples/sequencer_adapter.c"
$CC -std=c99 -Wall -Wextra -Werror -O2 -DACIDGEN_EXAMPLE_MAIN -Isrc -o "$OUT/sequencer_adapter" \
    examples/sequencer_adapter.c src/acidgen.c
"$OUT/sequencer_adapter" || fail=1

if [ $fail = 0 ]; then echo "ACIDGEN: ALL PASSED"; else echo "ACIDGEN: FAILED"; fi
exit $fail
