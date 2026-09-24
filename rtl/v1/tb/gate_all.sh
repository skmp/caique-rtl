#!/bin/bash
# gate_all.sh -- co-simulate every model case (or the ones named) against rtl/v1, in parallel.
#   tb/gate_all.sh [-j N] [case ...]      summary in build/rtl_v1/gate_all.txt; exit 0 iff every case is clean
cd "$(dirname "$0")/.." || exit 2
J=12
if [ "$1" = "-j" ]; then J=$2; shift 2; fi
CASES="$*"
[ -z "$CASES" ] && CASES=$(ls ../../model/cases/*.c | xargs -n1 basename | sed 's/\.c$//')
B=../../build/rtl_v1
make -s cosim || exit 2
mkdir -p $B/gate; rm -f $B/gate/*.txt
for c in $CASES; do make -s trace CASE=$c >/dev/null 2>&1 || echo "$c: trace failed" > $B/gate/$c.txt; done
printf '%s\n' $CASES | xargs -P "$J" -I{} sh -c \
  "[ -s $B/gate/{}.txt ] && grep -q 'trace failed' $B/gate/{}.txt || $B/obj_cosim/Vtb_top $B/trc/{}.trc -out -max 3 > $B/gate/{}.txt 2>&1"
fail=0
for c in $CASES; do
  l=$(grep -E "^cosim|trace failed|phase lost|no ack" $B/gate/$c.txt | head -1)
  echo "$c: ${l#cosim $B/trc/$c.trc: }"
  echo "$l" | grep -qE " 0 mismatches, outputs 0/" || fail=1
done | tee $B/gate_all.txt
! grep -vqE " 0 mismatches, outputs 0/" $B/gate_all.txt
