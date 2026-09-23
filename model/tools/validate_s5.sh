#!/bin/bash
# validate_s5.sh [K] -- the session-5 console gates in one go (run from caique-rtl/model, ~2 min).  K (default 6491, the
# boot of 2026-09-23) goes to eg_replay and tail_cmp only: eg_model's runs carry their own K per boot (sgc_loop lo_2 has 165)
# and its -K is a global override that would break lo_2; a rebooted console needs new K values in eg_model's run table.
#   build/tools/eg_model                          89 runs (33 session-4 + eg_kprobe kp_p0..p7 + feg_koffdir kd_0..7 x 3 + feg_koffatt ka_0..7 x 3)
#   build/tools/eg_replay -case ... <run>          tests/aeg_koff koff_d2 koff_d2b koff_d1 koff_att kon_rel (all 4 streams, all cycles)
#   tools/tail_cmp_all.sh tests/slot_tail/hw K     tail_a / tail_b / tail_c (12 streams)
#   mixs_write gate: ./run_model.sh mixs_write, then build/tools/mixsw_check on tests/mixs_write/model and tests/mixs_write/hw:
#                                                  PASS iff both say "H_G ... consistent" and their probe/bus verdict rows
#                                                  ("P<n> <bus> <LOST|KEPT|OTHER>" of the rules table) are identical
# Prints every tool's summary line and one PASS/FAIL line; exit 0 iff everything is FULL / consistent.  If the console
# rebooted, refit K with eg_phase on a new eg_lock att_slow capture and pass it.
set -u
K=${1:-6491}
M=$(cd "$(dirname "$0")/.." && pwd)
cd "$M" || exit 2
make -C tools eg_model eg_replay tail_cmp mixsw_check >/dev/null || { echo "build failed"; exit 2; }
echo "model: $(md5sum src/aica_model.cpp | cut -c1-12) $(md5sum src/aica_model.h | cut -c1-12)  K $K"
rc=0
out=$(build/tools/eg_model) || rc=1
echo "$out" | grep -v FULL | grep -v '^GROUPS\|^TOTAL' | sed 's/^/  /'
echo "$out" | grep '^GROUPS\|^TOTAL' | sed 's/^/eg_model  /'
for r in koff_d2 koff_d2b koff_d1 koff_att kon_rel; do
  out=$(build/tools/eg_replay -K "$K" -case tests/aeg_koff/hw/aeg_koff.txt $r) || rc=1
  echo "$out" | grep 'FAIL\|prefix' | sed 's/^/  /'
  echo "$out" | grep '^TOTAL' | sed 's/^/eg_replay /'
done
out=$(tools/tail_cmp_all.sh tests/slot_tail/hw "$K") || rc=1
echo "$out" | grep 'first mismatch' | cut -c1-200 | sed 's/^/  /'
echo "$out" | grep '^RESULT\|^TOTAL' | sed 's/^/tail_cmp  /'
# mixs_write: the model's own run, then the writer-rule verdicts on both platforms
mrc=0
./run_model.sh mixs_write >/dev/null 2>&1 || { echo "  run_model.sh mixs_write failed"; mrc=1; }
verdicts() { grep -E '^P[0-9]+b? +[0-9]+ +(LOST|KEPT|OTHER)' | awk '{print $1, $2, $3}'; }
hw=$(build/tools/mixsw_check tests/mixs_write/hw) || mrc=1
md=$(build/tools/mixsw_check tests/mixs_write/model) || mrc=1
hg_hw=$(echo "$hw" | grep '^  H_G ' | grep -c consistent); hg_md=$(echo "$md" | grep '^  H_G ' | grep -c consistent)
nrow=$(echo "$hw" | verdicts | wc -l)
if [ "$hg_hw" -ne 1 ]; then echo "  mixsw_check hw: H_G not consistent:"; echo "$hw" | grep '^  H_[A-Z0-9] ' | sed 's/^/    /'; mrc=1; fi
if [ "$hg_md" -ne 1 ]; then echo "  mixsw_check model: H_G not consistent:"; echo "$md" | grep '^  H_[A-Z0-9] ' | sed 's/^/    /'; mrc=1; fi
if ! diff <(echo "$hw" | verdicts) <(echo "$md" | verdicts) >/dev/null; then
  echo "  mixsw_check: verdict rows differ (hw < > model):"; diff <(echo "$hw" | verdicts) <(echo "$md" | verdicts) | sed 's/^/    /'; mrc=1
fi
[ "$nrow" -ge 21 ] || { echo "  mixsw_check hw: only $nrow verdict rows (21 expected)"; mrc=1; }
echo "mixs_write hw:    $(echo "$hw" | grep '^  H_G ' | sed 's/^ *//')"
echo "mixs_write model: $(echo "$md" | grep '^  H_G ' | sed 's/^ *//')"
echo "mixs_write TOTAL: $nrow probe/bus verdict rows, hw and model $([ $mrc -eq 0 ] && echo identical || echo DIFFER), H_G $([ $mrc -eq 0 ] && echo 'consistent on both' || echo 'see above')"
[ $mrc -eq 0 ] || rc=1
[ $rc -eq 0 ] && echo "validate_s5: PASS (every stream FULL, mixs_write verdicts identical)" || echo "validate_s5: FAIL (see above)"
exit $rc
