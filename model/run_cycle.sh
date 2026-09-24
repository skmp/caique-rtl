#!/bin/bash
# run_cycle.sh CASE... -- build caique test cases against the cycle model (cycle-model/, every access through the SH4
# port) and run them; outputs in tests/<case>/cycle/.  With tests/<case>/<REPLAY_FROM>/replay.txt (default hw) the run
# applies that console run's replay parameters (CAIQUE_REPLAY).
M=$(cd "$(dirname "$0")" && pwd)
set -u
make -C "$M/cycle-model" "$@" >/dev/null || { echo "build failed"; exit 1; }
rc_all=0
for t in "$@"; do
  d="$M/tests/$t/cycle"; mkdir -p "$d"
  rp="$M/tests/$t/${REPLAY_FROM:-hw}/replay.txt"   # the console run's replay parameters, when it has them
  if [ -f "$rp" ]; then export CAIQUE_REPLAY="$rp"; else unset CAIQUE_REPLAY; fi
  "$M/build/cycle/$t" "$d" > "$d/console.log" 2>&1
  rc=$?; echo "$rc" > "$d/status"
  echo "== cycle $t exit $rc"
  [ $rc -ne 0 ] && rc_all=$rc
done
exit $rc_all
