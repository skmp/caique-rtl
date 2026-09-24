#!/bin/bash
# replay_all.sh [-j N] [case ...] -- gate 1.2 (../../TODO.md): every case's access trace from the sample model replayed
# into the cycle model at the sample boundary (trace_replay); summary in build/cycle/replay_all.txt, exit 0 iff clean.
# Run from anywhere; traces go to build/cycle/trc/.
cd "$(dirname "$0")/.." || exit 2
J=12
if [ "$1" = "-j" ]; then J=$2; shift 2; fi
CASES="$*"
[ -z "$CASES" ] && CASES=$(ls cases/*.c | xargs -n1 basename | sed 's/\.c$//')
B=build/cycle
mkdir -p $B/trc $B/out $B/replay
make -s -C cycle-model trace_replay || exit 2
make -s -C host $CASES >/dev/null || exit 2
printf '%s\n' $CASES | xargs -P "$J" -I{} sh -c \
  "mkdir -p $B/out/{} && CAIQUE_TRACE=$B/trc/{}.trc CAIQUE_TRACE_OUT=1 build/host/{} $B/out/{} >/dev/null 2>&1; $B/trace_replay $B/trc/{}.trc -max 5 > $B/replay/{}.txt 2>&1"
for c in $CASES; do
  l=$(grep -E "^trace_replay|no output" $B/replay/$c.txt | head -1)
  echo "$c: ${l#trace_replay $B/trc/$c.trc: }"
done | tee $B/replay_all.txt
! grep -vqE " 0 mismatches, outputs 0/" $B/replay_all.txt
