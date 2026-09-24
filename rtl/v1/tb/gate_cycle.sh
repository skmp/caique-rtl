#!/bin/bash
# gate_cycle.sh -- co-simulate every model case (or the ones named) against the cycle model through the SH4 port, in
# parallel (tb/cosim_cycle.cpp: request and ack clocks, read data, every output sample).
#   tb/gate_cycle.sh [-j N] [case ...]      summary in build/rtl_v1/gate_cycle.txt; exit 0 iff every case is clean
#   CAIQUE_SEED=n tb/gate_cycle.sh ...       the harness delays every access by a seeded random 0..2047 ns (TODO 1.5);
#                                            outputs go to gate_cycle_s<n>.txt, gate_cycle_s<n>/, trc_cycle_s<n>/
#   REPLAY_FROM=hw9 tb/gate_cycle.sh ...      each case applies its console run's replay parameters
#                                            (model/tests/<case>/hw9/replay.txt, when present: the 'P' load path);
#                                            outputs get the suffix _hw9
cd "$(dirname "$0")/.." || exit 2
J=12
if [ "$1" = "-j" ]; then J=$2; shift 2; fi
CASES="$*"
[ -z "$CASES" ] && CASES=$(ls ../../model/cases/*.c | xargs -n1 basename | sed 's/\.c$//')
B=../../build/rtl_v1
TAG=${CAIQUE_SEED:+_s$CAIQUE_SEED}${REPLAY_FROM:+_$REPLAY_FROM}
G=$B/gate_cycle$TAG; TR=$B/trc_cycle$TAG
make -s cosim-cycle || exit 2
make -s -C ../../model/cycle-model $CASES >/dev/null || exit 2
mkdir -p $G $TR; rm -f $G/*.txt
VT=$PWD/$B/obj_cosim_cycle/Vtb_top
RF=${REPLAY_FROM:-}
export G TR VT RF
printf '%s\n' $CASES | xargs -P "$J" -I{} sh -c '
  mkdir -p $TR/out/{}
  rp=""; [ -n "$RF" ] && [ -f ../../model/tests/{}/$RF/replay.txt ] && rp=$PWD/../../model/tests/{}/$RF/replay.txt
  (cd ../../model && CAIQUE_REPLAY=$rp CAIQUE_TRACE=$OLDPWD/$TR/{}.trc CAIQUE_TRACE_OUT=1 build/cycle/{} $OLDPWD/$TR/out/{} >/dev/null 2>&1) ||
    echo "{}: trace failed" > $G/{}.txt
  [ -s $G/{}.txt ] || $VT $TR/{}.trc -max 3 > $G/{}.txt 2>&1'
for c in $CASES; do
  l=$(grep -E "^cosim_cycle|trace failed|RTL is at" $G/$c.txt | head -1)
  echo "$c: ${l#cosim_cycle $TR/$c.trc: }"
done | tee $G.txt
! grep -vqE " 0 mismatches, outputs 0/[0-9]+ differ$" $G.txt
