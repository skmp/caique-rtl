#!/bin/bash
# run_model.sh CASE... -- build caique test cases against the model and run them; outputs in tests/<case>/model/.
M=$(cd "$(dirname "$0")" && pwd)
set -u
make -C "$M/host" "$@" >/dev/null || { echo "build failed"; exit 1; }
rc_all=0
for t in "$@"; do
  d="$M/tests/$t/model"; mkdir -p "$d"
  "$M/build/host/$t" "$d" > "$d/console.log" 2>&1
  rc=$?; echo "$rc" > "$d/status"
  echo "== model $t exit $rc"
  [ $rc -ne 0 ] && rc_all=$rc
done
exit $rc_all
