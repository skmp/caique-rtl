#!/bin/bash
# armjob_all.sh -- every wren7 console job suite (wren7-rtl/model/tests/hw/*/jobs.txt) on wren7's ARM7DI model with
# rtl/v1 as its bus (tb/armjob_tb.cpp), in parallel chunks.   tb/armjob_all.sh [-j N] [suite ...]
# Results: build/rtl_v1/armjob/<suite>.<chunk>.txt, summary build/rtl_v1/armjob_all.txt; exit 0 iff every job matches.
cd "$(dirname "$0")/.." || exit 2
J=16
if [ "$1" = "-j" ]; then J=$2; shift 2; fi
make -s armjob || exit 2
B=$(realpath ../../build/rtl_v1)
M=$(realpath ../../../wren7-rtl/model)
SUITES="$*"
[ -z "$SUITES" ] && SUITES=$(cd $M/tests/hw && ls -d */ | tr -d /)
mkdir -p $B/armjob; rm -f $B/armjob/*.txt
CH=24                                   # jobs per chunk
for s in $SUITES; do
  n=$(grep -c "^run" $M/tests/hw/$s/jobs.txt)
  for ((f = 0; f < n; f += CH)); do echo "$s $f $((f + CH - 1))"; done
done | xargs -P "$J" -L 1 sh -c "cd $M && $B/obj_armjob/Vtb_top tests/hw/\$0/jobs.txt -j \$1:\$2 -p 8 > $B/armjob/\$0.\$1.txt 2>&1"
for s in $SUITES; do
  cat $B/armjob/$s.*.txt | awk -v s=$s '/^armjob_tb/ {j+=$3; d+=$5; m+=$10; w+=$13} / DIFF /{x=x" "$1} / TIMEOUT /{t++}
    /readback DIFFERS/ {r++} /time went|no ack|cannot read/ {e++}
    END {printf "%-10s %4d jobs, %d differ, %d wait mismatches of %d memory cycles, %d readbacks differ from the model, %d timeouts, %d errors%s\n", s, j, d, w, m, r, t, e, (x ? "  [" x " ]" : "")}'
done | tee $B/armjob_all.txt
! grep -qvE " 0 differ, 0 wait mismatches.* 0 errors" $B/armjob_all.txt
