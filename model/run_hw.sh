#!/bin/bash
# run_hw.sh CASE... -- build caique test cases for the console and run them one after another through shrike4's
# hwrun.sh (the only way to use the console; it serializes all users).  Outputs: tests/<case>/hw/ (files written by
# the case through dcload /pc/), tests/<case>/hw/console.log, tests/<case>/hw/status (exit code), and the replay
# parameters of the run (tools/replay_fit on the preamble's capture): tests/<case>/hw/replay.txt, replay_fit.txt.
# HWDIR=<name> writes to tests/<case>/<name>/ instead (a new console session next to the old evidence).
M=$(cd "$(dirname "$0")" && pwd)
HWRUN=/home/skmp/projects/dreamster/shrike4-rtl/tools/hw/hwrun.sh
source /opt/toolchains/dc/kos/environ.sh
set -u
HWDIR=${HWDIR:-hw}
make -C "$M/hw" HWDIR="$HWDIR" "$@" >/dev/null || { echo "build failed"; exit 1; }
make -C "$M/tools" replay_fit >/dev/null || { echo "replay_fit build failed"; exit 1; }
rc_all=0
for t in "$@"; do
  d="$M/tests/$t/$HWDIR"; mkdir -p "$d"
  echo "== hw $t $(date +%T)"
  "$HWRUN" "$M/build/hw/$HWDIR/$t.elf" "$d/console.log.raw" > "$d/console.log" 2>&1
  rc=$?; echo "$rc" > "$d/status"; rm -f "$d/console.log.raw"
  rp=""
  if [ -f "$d/replay_log.txt" ]; then
    (cd "$M" && build/tools/replay_fit "$d") > "$d/replay_fit.txt" 2>&1 && rp=" replay: $(grep -h '^mdec' "$d/replay.txt")" || rp=" replay_fit FAILED"
  fi
  echo "== hw $t exit $rc $(date +%T)$rp"
  [ $rc -ne 0 ] && rc_all=$rc
done
exit $rc_all
