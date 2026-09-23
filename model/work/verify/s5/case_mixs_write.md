# case_mixs_write -- which slots rewrite their MIXS bus every sample? (probe case for the bus-0 retention asymmetry)

Prepared 2026-09-23 for the orchestrator's console run.  Model run done, console compile done, **console run NOT done**
(the console is the orchestrator's).  Everything is C++ / C integer code; nothing in /tmp; no existing file edited.

## Files

| Path | What |
|---|---|
| `cases/mixs_write.c` | the case: 8 probe captures P0 P1 P1b P2 P3 P5 P7 P8 (4 buses each), CPU writes with self-describing marks |
| `work/mixsw/mixsw_check.cpp` | analysis: RLE around every write, LOST / KEPT / OTHER per written bus, probe x bus table, rule predictions and which rules survive |
| `work/mixsw/check_model.txt` | `build/work/mixsw_check tests/mixs_write/model` (the model's verdicts, this report's section 5) |
| `tests/mixs_write/model/` | `./run_model.sh mixs_write` output: `mixs_write.txt` (log), `P*.hdr/.bin`, exit 0 |
| `build/work/mixsw_check`, `build/hw/mixs_write.elf`, `build/host/mixs_write` | binaries (git-ignored) |

Commands for the orchestrator:

```sh
cd /home/skmp/projects/dreamster/caique-rtl/model
./run_hw.sh mixs_write                                  # console, ~4 s of console time (8 probes; see section 6)
g++ -O2 -std=c++17 -o build/work/mixsw_check work/mixsw/mixsw_check.cpp   # if not built yet
build/work/mixsw_check tests/mixs_write/hw              # verdicts + which candidate rules survive
build/work/mixsw_check tests/mixs_write/model           # the model's verdicts for comparison (= work/mixsw/check_model.txt)
grep -v "^cap_start" tests/mixs_write/hw/mixs_write.txt # the register readbacks
```

## 1. The premise, corrected by the code of the two captures

The task's framing was: "buses 1/2 were pointed at by ONE slot each with IMXL 0 but non-zero registers (VOFF 1, filter
on, AEG off)".  That is not what the two cases do:

- `slot_tail.c` line 102: `aw(CH(k, 0x20), 0)` on slots 0..2.  Reg 0x20 is `IMXL << 4 | ISEL`, so this sets **ISEL 0
  as well as IMXL 0**: after mark 7 slots 0..2 (VOFF 1, filter on, non-zero registers) point at **bus 0**, and no slot
  at all points at buses 1 and 2.
- `eg_lock.c` mixs_run: `aica_quiet()` first (`ch_zero_regs` on all 64 slots), then only slot 3 is configured (ISEL 3,
  IMXL 15, VOFF 1).  So slots 0..2 and 4..63 all have reg 0x20 = 0 (bus 0, IMXL 0, every register 0); no slot points at
  buses 1 and 2.

So the evidence is: **a bus with 61..64 IMXL-0 slots pointing at it read 0; buses with no slot pointing at them
retained their value.**  There is no observation yet of a bus with an IMXL-0 slot pointing at it that retained.  The
NOTES rule ("IMXL 0 on every slot targeting it keeps its last value") was inferred from bus 2 of eg_lock mixs, where
nothing targeted bus 2.  This admits the simplest rule of all, which the task did not list:

> **H_G: every slot writes its ISEL bus every sample; IMXL is a pure gain (0 -> writes 0).  A bus retains iff no slot
> has ISEL = bus.**

H_G, H_B, H_V(=H_D), H_A(all-zero), H_C(filter) and H_0 (bus 0 special) all fit the two captures.  The probes below
separate them; the case also carries the two "nobody points at it" controls (KEPT under every rule) and one IMXL-15
"LOST control".

## 2. Candidate rules (as encoded in mixsw_check, same order and names)

| Rule | Writers of bus b every sample |
|---|---|
| H_M | the slots with ISEL = b **and IMXL != 0** (the production model: `sent[]` in `step()`); IMXL 0 never writes |
| H_G | every slot with ISEL = b, whatever its IMXL (IMXL 0 contributes 0) |
| H_B | as H_M, plus slot 0 always writes its ISEL bus (0 when IMXL 0) |
| H_V | as H_M, plus an IMXL-0 slot writes 0 iff VOFF 0 (the task's H_A(VOFF) and H_D: IMXL 0 = gain 0, VOFF 1 + IMXL 0 = no send) |
| H_F | as H_M, plus an IMXL-0 slot writes 0 iff LPOFF 0 (filter on; the task's H_C "unless its filter is on" is refuted already: the zeroed slots have LPOFF 0 and wrote) |
| H_O | as H_M, plus an IMXL-0 slot writes 0 iff it is off (not playing) |
| H_S | as H_M, plus an IMXL-0 slot writes 0 iff SA == 0 (the task's "all registers zero / SA 0"; "never keyed since reset" is not testable without a reset and is already refuted by eg_lock, where slot 0 had been keyed in every run before mixs) |
| H_0 | as H_M, plus bus 0 is rewritten every sample whoever points at it |

## 3. Probe technique

Each probe: `aica_quiet()` (every register of every slot 0: ISEL 0 IMXL 0 VOFF 0 LPOFF 0 TL 0 SA 0, AEG off), configure a
few slots, `cap_start` on 4 buses, 10 ms, then the CPU writes a distinctive positive 20-bit value `V = 0x1PB25`
(P = probe index 0..7, B = bus) to up to three of the captured buses 10 ms apart (`aw(R_MIXS(b,1), V >> 4);
aw(R_MIXS(b,0), V & 15)` exactly as eg_lock mixs_run), waits 30 ms after the last, `cap_stop`, `cap_save`.  Marks:
`0x1BVVVVV` right before a write, `0x2BVVVVV` right after (B = bus in bits 23:20, V in 19:0), so the checker needs no text
parsing.  The 4th captured bus is never written and is printed as a reference stream (e.g. bus 0 in P2/P3/P5).

Logged in `mixs_write.txt` (not used for the verdicts): reg 0x00/0x04/0x20/0x28 readbacks of the configured slots, the
`mixs_rd` readbacks of the 4 captured buses before the writes and at the end, the raw hi/lo readback of each written
bus right after its write (eg_lock saw hi 0 / lo as written on the console), and for P5 EGMON/CA of the playing slots
before and after the capture (EGMON 0xC000 = state 0, a = 0: playing at full level).

## 4. Probe table with the prediction of every rule (K = KEPT, L = LOST)

| Capture | bus | Who points at the bus (all other slots: ISEL 0 IMXL 0, or as stated) | H_M | H_G | H_B | H_V | H_F | H_O | H_S | H_0 | task name |
|---|---|---|---|---|---|---|---|---|---|---|---|
| P0 | 0 | all 64 zeroed slots (the eg_lock / tail_c situation) | K | L | L | L | L | L | L | L | P0 |
| P0 | 1 | nobody | K | K | K | K | K | K | K | K | P0 |
| P1 | 1 | slot 0 alone (reg 0x20 = 0x01), off, all else 0 | K | L | L | L | L | L | L | K | P1 |
| P1 | 0 | slots 1..63 zeroed | K | L | K | L | L | L | L | L | P1 |
| P1b | 0 | nobody (slot 0 at 1, slots 1..63 at 15) | K | K | K | K | K | K | K | L | P1b |
| P1b | 1 | slot 0 alone | K | L | L | L | L | L | L | K | P1b |
| P1b | 15 | slots 1..63 (reg 0x20 = 0x0F), off, all else 0 | K | L | K | L | L | L | L | K | P1b |
| P2 | 2 | slot 7 alone (0x02), off, all else 0 | K | L | K | L | L | L | L | K | P2 |
| P2 | 3 | slot 8 alone, **IMXL 15** (0xF3), off: LOST control | L | L | L | L | L | L | L | L | -- |
| P2 | 4 | slot 9 alone (0x04), SA 0x10000 (reg 0x00 = 1), never keyed | K | L | K | L | L | L | K | K | -- |
| P3 | 2 | slot 7 alone, VOFF 1 (0x28 = 0x40), LPOFF 0, off | K | L | K | K | L | L | L | K | P3 |
| P3 | 3 | slot 8 alone, VOFF 0, LPOFF 1 (0x28 = 0x20), off | K | L | K | L | K | L | L | K | P4 |
| P3 | 4 | slot 9 alone, VOFF 1, LPOFF 1 (0x28 = 0x60), off | K | L | K | K | K | L | L | K | -- |
| P5 | 2 | slot 7 alone, **playing** 0x7FFF (SA 0x10000, loop [0,32), AR 31, D1R 0, TL 0, KRS 15), IMXL 0, VOFF 0, LPOFF 1 | K | L | K | L | K | K | K | K | P5 |
| P5 | 3 | slot 8 alone, playing, IMXL 0, VOFF 1, LPOFF 1 | K | L | K | K | K | K | K | K | P6 |
| P5 | 4 | slot 9 alone, playing, IMXL 0, VOFF 0, LPOFF 0 (FLV 0x1FF8, Q 0) | K | L | K | L | L | K | K | K | -- |
| P7 | 3 | slot 0 alone, VOFF 1, LPOFF 0, off | K | L | L | K | L | L | L | K | P7 |
| P7 | 15 | slots 1..63 (0x0F) | K | L | K | L | L | L | L | K | -- |
| P7 | 0 | nobody | K | K | K | K | K | K | K | L | -- |
| P8 | 4 | slot 63 alone (0x04), off, all else 0 | K | L | K | L | L | L | L | K | P8 |
| P8 | 0 | slots 0..62 zeroed | K | L | L | L | L | L | L | L | -- |

Each rule has a distinct column, so one console run picks the survivor (or refutes all eight, in which case the RLE
printouts show what actually happens).  The decisive rows:

- **P0:0** reproduces the eg_lock / tail_c observation (expected LOST; if KEPT, the earlier zeros had another cause).
- **P2:2 / P8:4 / P1b:15 / P7:15**: a single (or 63) IMXL-0 zeroed slot(s) other than slot 0 pointing at a bus.
  LOST -> H_G-family (IMXL 0 writes), KEPT -> H_B (slot 0 is special) or the model rule.
- **P1:1 / P1b:1**: slot 0 alone, VOFF 0.  LOST under everything but H_M/H_0.
- **P7:3**: slot 0 alone with VOFF 1: separates H_B (L) from H_V (K).
- **P3:2 vs P3:3 vs P3:4**: VOFF / LPOFF on an off IMXL-0 slot: H_G L L L, H_V K L K, H_F L K K.
- **P5:x**: a playing IMXL-0 slot.  H_G L; H_O K; H_V L K L; H_F K K L.  If P5:3 (VOFF 1) comes out LOST with the bus
  showing 524272 (= 0x7FFF * 16) instead of 0, IMXL 0 + VOFF 1 sends the raw sample (the checker's "bus afterwards"
  column shows it; the verdict would still read LOST).
- **P0:1 / P1b:0 / P7:0**: nobody points at the bus: KEPT under every rule (H_0 predicts L for the two bus-0 ones).
- **P2:3**: IMXL 15 off slot: LOST under every rule (the NOTES observation that a silent sender rewrites 0).

## 5. Validation on the model (`work/mixsw/check_model.txt`)

`./run_model.sh mixs_write` exit 0; 8 captures, 0 errors each, 2368..2816 samples (54..64 ms).  Verdicts: 20 KEPT,
1 LOST (P2:3), no OTHER; `H_M 21 agree 0 disagree consistent`, every other rule REFUTED by the model output as its
column predicts (H_G by 17 rows, H_B by P0:0 P1:1 P1b:1 P7:3 P8:0, H_0 by the five bus-0 rows, ...).  So on the model the
checker recovers the model's own rule from the captures; the console differs from the model wherever the survivor's
column differs from H_M's.  Two details worth knowing when reading the console output:

- P3:2 on the model shows `exact 0 / hi16 1184`: the two register writes (hi then lo, ~2.4 us apart) straddled a
  sample boundary and landed in different banks (the log's "readback right after: hi 1422 lo 0").  The verdict uses
  the 16-bit high field, so this is still KEPT; on the console (~21 % chance per write) the same can happen.
- The "other bank" value is whatever the bus held before: on the model the previous probe's CPU value (nobody ever
  rewrites these buses there), on the console possibly the residue of an earlier case (like the -8 of eg_lock).

## 6. Console compile and time budget

`bash -c 'source /opt/toolchains/dc/kos/environ.sh && make -C .../model/hw mixs_write'` -> `build/hw/mixs_write.elf`
(2.5 MB, no warnings).  Per probe: aica_quiet ~27 ms, cap_start ~92 ms (the 128 KB ring clear is 32768 G2 writes at
2.4 us + 12 ms), capture 50..70 ms, cap_stop 5 ms, cap_save ~30..45 KB over dcload -- about 0.3..0.5 s each, 8 probes:
roughly 3..4 s plus boot.

## 7. How to read the verdicts

`build/work/mixsw_check tests/mixs_write/hw` prints, per capture and written bus: the window, how many samples carry
the CPU value (exact 20 bits / high 16 bits), where it first appears relative to the mark, the RLE `start:value*len`
of the 40 samples after the write (`-w N` for more), the verdict, and for the unwritten stream its value histogram.
Then the summary table (probe, bus, who points at it, result, counts, detail) and the rule table with `=` / `X` per
cell and a REFUTED / consistent line per rule.

- **LOST**: the CPU value appears on <= 2 samples (0 on the model: the SGC overwrites the bank before the DSP reads it;
  eg_lock saw it once on the console).  "bus afterwards: v*count" tells what the writer puts there (0 expected; a
  non-zero constant means the IMXL-0 slot sends something).
- **KEPT**: the CPU value persists to the end of the capture on every other sample, the samples in between holding one
  constant value (the other bank's old value, printed).  Also KEPT if it persists on both parities (the CPU write
  reached both banks).
- **OTHER**: everything else, e.g. it persists for a while and then vanishes (something rewrote the bus later -- a slot
  state change), or the alternate samples are not constant.  Read the RLE.

Rule verdict: a rule with `0 disagree` over the 21 rows is the survivor.  If two survive, the run did not separate them
(not possible by construction unless rows come out OTHER).  If none survives, the per-row `X` marks show which
assumption failed; the most likely reasons are a sub-sample race on a specific row (retry) or a rule outside the eight
(e.g. only the *first* slot in 0..63 order that points at a bus writes it -- P1b:15 vs P8:4 vs P2:2 would then all be
LOST like H_G, so that variant is not separable from H_G here; it is separable from H_M/H_B either way).

## 8. What to do with the answer

- If **H_G** survives: NOTES "MIXS retention" first bullet and `step()`'s `sent[]` must change to "a bus is rewritten iff
  some slot has ISEL = bus" (IMXL 0 -> adds 0), `MIXS retention` becomes "a bus no slot *points at* keeps its value"; the
  tail_c bus-0 and eg_lock bus-0 zeros then follow by construction, and buses 1/2 still retain.  The open item about the
  -8 residual is unchanged.
- If **H_V/H_D** survives: `sent[bus] = true` also when IMXL 0 and VOFF 0 (the `d` is already 0).
- If **H_B** survives: `sent[chr(0,0x20) & 0xF] = true` unconditionally for slot 0.
- H_F / H_O / H_S / H_0 analogously; the checker's description column names the property.
