#!/bin/bash
# run_hw.sh CASE... -- build caique test cases for the console and run them one after another through shrike4's
# hwrun.sh (the only way to use the console; it serializes all users).  Outputs: tests/<case>/hw/ (files written by
# the case through dcload /pc/), tests/<case>/hw/console.log, tests/<case>/hw/status (exit code).
M=$(cd "$(dirname "$0")" && pwd)
HWRUN=/home/skmp/projects/dreamster/shrike4-rtl/tools/hw/hwrun.sh
source /opt/toolchains/dc/kos/environ.sh
set -u
make -C "$M/hw" "${@/%/.elf}" >/dev/null || { echo "build failed"; exit 1; }
rc_all=0
for t in "$@"; do
  d="$M/tests/$t/hw"; mkdir -p "$d"
  echo "== hw $t $(date +%T)"
  "$HWRUN" "$M/hw/$t.elf" "$d/console.log.raw" > "$d/console.log" 2>&1
  rc=$?; echo "$rc" > "$d/status"; rm -f "$d/console.log.raw"
  echo "== hw $t exit $rc $(date +%T)"
  [ $rc -ne 0 ] && rc_all=$rc
done
exit $rc_all
