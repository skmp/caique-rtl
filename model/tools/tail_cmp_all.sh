#!/bin/bash
# tail_cmp_all.sh [-v] <tests/slot_tail/PLATFORM dir> <K> -- run build/tools/tail_cmp on the three runs of a slot_tail
# output directory, taking each run's c0 from the ", c0 XXXX" of the cap_start lines of its slot_tail.txt (they appear
# in run order a, b, c).  Builds the tool (make -C tools tail_cmp) if needed.  Run from caique-rtl/model.  Exit 0 when
# every stream of every run is FULL; the per-run RESULT lines and a TOTAL line are printed.
#   e.g. tools/tail_cmp_all.sh tests/slot_tail/hw 6491      (K: the boot constant; refit with eg_phase on eg_lock att_slow if the console rebooted)
#        tools/tail_cmp_all.sh tests/slot_tail/model 6491
# CAIQUE_MODEL=cycle: build/tools/tail_cmp_cycle (the replay through the cycle model)
set -u
SUF=""; [ "${CAIQUE_MODEL:-sample}" = cycle ] && SUF=_cycle
V=""
if [ "${1:-}" = "-v" ]; then V="-v"; shift; fi
DIR=${1:?dir}; K=${2:?K}
M=$(cd "$(dirname "$0")/.." && pwd)
cd "$M" || exit 2
make -C tools tail_cmp$SUF >/dev/null || exit 2
BIN=build/tools/tail_cmp$SUF
mapfile -t C0 < <(grep -o ', c0 [0-9a-f]\{4\}' "$DIR/slot_tail.txt" | awk '{print $3}')
[ ${#C0[@]} -eq 3 ] || { echo "expected 3 cap_start lines in $DIR/slot_tail.txt, found ${#C0[@]}"; exit 2; }
rc=0
full=0
i=0
for kind in a b c; do
  echo "== $BIN $V $DIR/tail_$kind ${C0[$i]} $K $kind"
  out=$($BIN $V "$DIR/tail_$kind" "${C0[$i]}" "$K" $kind) || rc=1
  echo "$out"
  n=$(echo "$out" | sed -n 's/^RESULT .*: \([0-9]*\)\/4 streams FULL.*/\1/p')
  full=$((full + ${n:-0}))
  i=$((i + 1))
done
echo "TOTAL $DIR: streams FULL $full/12"
exit $rc
