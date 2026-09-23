#!/bin/bash
# tail_cmp_all.sh [-v] <tests/slot_tail/PLATFORM dir> <K> -- run tail_cmp on the three runs of a slot_tail output
# directory, taking each run's c0 from the cap_start lines of its slot_tail.txt (they appear in run order a, b, c).
# Builds build/work/tail_cmp if needed.  Run from caique-rtl/model.  Exit 0 when every stream of every run is FULL.
#   e.g. work/tail/tail_cmp_all.sh tests/slot_tail/model 6491
#        work/tail/tail_cmp_all.sh tests/slot_tail/hw 6491      (K: refit on the console's boot if the model's fails)
set -u
V=""
if [ "${1:-}" = "-v" ]; then V="-v"; shift; fi
DIR=${1:?dir}; K=${2:?K}
M=$(cd "$(dirname "$0")/../.." && pwd)
cd "$M" || exit 2
BIN=build/work/tail_cmp
if [ ! -x $BIN ] || [ work/tail/tail_cmp.cpp -nt $BIN ] || [ src/aica_model.cpp -nt $BIN ]; then
  mkdir -p build/work
  g++ -O2 -std=c++17 -o $BIN work/tail/tail_cmp.cpp src/aica_model.cpp || exit 2
fi
mapfile -t C0 < <(grep -o 'c0 [0-9a-f]\{4\}' "$DIR/slot_tail.txt" | awk '{print $2}')
[ ${#C0[@]} -eq 3 ] || { echo "expected 3 cap_start lines in $DIR/slot_tail.txt, found ${#C0[@]}"; exit 2; }
rc=0
i=0
for kind in a b c; do
  echo "== $BIN $V $DIR/tail_$kind ${C0[$i]} $K $kind"
  $BIN $V "$DIR/tail_$kind" "${C0[$i]}" "$K" $kind || rc=1
  i=$((i + 1))
done
exit $rc
