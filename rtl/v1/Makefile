# caique-rtl/rtl/v1 -- AICA v1 RTL (sound generator, DSP, bus).  Simulation build (Verilator); builds go to
# ../../build/rtl_v1 (git-ignored).
#   make lint              Verilator lint of the core
#   make cosim             build the co-simulation (tb/cosim.cpp + tb/tb_top.sv)
#   make trace CASE=x      record the model's access trace of a case (model/host, CAIQUE_TRACE)
#   make run CASE=x        trace + co-simulate one case (compares every CPU read with the model)
#   make gate              run the gate set of cases (tb/gate_all.sh: every case, in parallel)
#   make arb               the ARM7DI port against wren7's measured bus model (12 random seeds)
#   make cosim-vcd / arb-vcd   traced builds (query with build/rtl_v1/vcdq)
#   make armjob            wren7's ARM7DI model + this RTL as its bus (tb/armjob_all.sh runs the console job suites)
#   make cosim-cycle       the co-simulation against the cycle model through the SH4 port (tb/cosim_cycle.cpp)
#   make trace-cycle CASE=x   record the cycle model's access trace of a case (model/cycle-model, CAIQUE_TRACE)
#   make run-cycle CASE=x     trace-cycle + cosim-cycle of one case (tb/gate_cycle.sh: every case)
# Architecture, port contracts, co-simulation contract, approximations: README.md
VERILATOR ?= verilator
NJOBS  := $(shell nproc 2>/dev/null || echo 4)
ROOT   := $(abspath ../..)
BUILD  := $(ROOT)/build/rtl_v1
MODEL  := $(ROOT)/model
SRC    := aica_pkg.sv aica_ram.sv aica_sgc.sv aica_dsp.sv aica_bus.sv aica_core.sv
WNO    := -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-WIDTH
GATE   ?= sgc_level sgc_mix sgc_pitch sgc_formats sgc_loop sgc_keys sgc_krs sgc_lfo sgc_lfo2 aeg_dl0 \
          dsp_basic dsp_mem dsp_mem2 dsp_mem3 dsp_wslot mixs_write mixs_rd slot_tail filt_id2 feg_track probe
.PHONY: lint cosim cosim-vcd cosim-cycle arb arb-vcd armjob trace trace-cycle run run-cycle gate clean

lint:
	$(VERILATOR) --lint-only -Wall $(WNO) --top-module aica_core $(SRC)

$(BUILD)/obj_cosim/Vtb_top: $(SRC) tb/tb_top.sv tb/cosim.cpp
	@mkdir -p $(BUILD)
	$(VERILATOR) --cc --exe --build -O3 -j $(NJOBS) $(WNO) -Wno-LATCH --x-assign fast --x-initial unique \
	  --top-module tb_top $(SRC) tb/tb_top.sv $(abspath tb/cosim.cpp) -CFLAGS "-O2" --Mdir $(BUILD)/obj_cosim -o Vtb_top

cosim: $(BUILD)/obj_cosim/Vtb_top

# the co-simulation against the cycle model, clock for clock through the SH4 port (engine never frozen)
$(BUILD)/obj_cosim_cycle/Vtb_top: $(SRC) tb/tb_top.sv tb/cosim_cycle.cpp
	@mkdir -p $(BUILD)
	$(VERILATOR) --cc --exe --build -O3 -j $(NJOBS) $(WNO) -Wno-LATCH --x-assign fast --x-initial unique \
	  --top-module tb_top $(SRC) tb/tb_top.sv $(abspath tb/cosim_cycle.cpp) -CFLAGS "-O2" --Mdir $(BUILD)/obj_cosim_cycle -o Vtb_top
cosim-cycle: $(BUILD)/obj_cosim_cycle/Vtb_top

# the same with VCD dumping: Vtb_top <trace> -vcd <from gap> <to gap> <file.vcd>
$(BUILD)/obj_cosim_vcd/Vtb_top: $(SRC) tb/tb_top.sv tb/cosim.cpp
	@mkdir -p $(BUILD)
	$(VERILATOR) --cc --exe --build -O1 -j $(NJOBS) --trace $(WNO) -Wno-LATCH --x-assign fast --x-initial unique \
	  --top-module tb_top $(SRC) tb/tb_top.sv $(abspath tb/cosim.cpp) -CFLAGS "-O1" --Mdir $(BUILD)/obj_cosim_vcd -o Vtb_top
cosim-vcd: $(BUILD)/obj_cosim_vcd/Vtb_top

# the ARM7DI port against the console's bus timing (wren7 DcWaits::dreamcast)
WREN7 := $(abspath ../../../wren7-rtl/model/src)
$(BUILD)/obj_arb/Vtb_top: $(SRC) tb/tb_top.sv tb/arb_tb.cpp $(WREN7)/dc_arm_map.cpp $(WREN7)/dc_arm_map.h
	@mkdir -p $(BUILD)
	$(VERILATOR) --cc --exe --build -O3 -j $(NJOBS) $(WNO) -Wno-LATCH --x-assign fast --x-initial unique \
	  --top-module tb_top $(SRC) tb/tb_top.sv $(abspath tb/arb_tb.cpp) $(WREN7)/dc_arm_map.cpp \
	  -CFLAGS "-O2 -I$(WREN7)" --Mdir $(BUILD)/obj_arb -o Vtb_top
$(BUILD)/obj_arb_vcd/Vtb_top: $(SRC) tb/tb_top.sv tb/arb_tb.cpp $(WREN7)/dc_arm_map.cpp $(WREN7)/dc_arm_map.h
	@mkdir -p $(BUILD)
	$(VERILATOR) --cc --exe --build -O1 -j $(NJOBS) --trace $(WNO) -Wno-LATCH --x-assign fast --x-initial unique \
	  --top-module tb_top $(SRC) tb/tb_top.sv $(abspath tb/arb_tb.cpp) $(WREN7)/dc_arm_map.cpp \
	  -CFLAGS "-O1 -I$(WREN7)" --Mdir $(BUILD)/obj_arb_vcd -o Vtb_top
arb-vcd: $(BUILD)/obj_arb_vcd/Vtb_top

# wren7's ARM7DI model running the console job files on the RTL (run from wren7-rtl/model: tb/armjob_all.sh)
$(BUILD)/obj_armjob/Vtb_top: $(SRC) tb/tb_top.sv tb/armjob_tb.cpp $(WREN7)/dc_arm_map.cpp $(WREN7)/dc_arm_map.h \
                             $(WREN7)/arm7di.cpp $(WREN7)/arm7di.h $(WREN7)/../tools/hwjob.h
	@mkdir -p $(BUILD)
	$(VERILATOR) --cc --exe --build -O3 -j $(NJOBS) $(WNO) -Wno-LATCH --x-assign fast --x-initial unique \
	  --top-module tb_top $(SRC) tb/tb_top.sv $(abspath tb/armjob_tb.cpp) $(WREN7)/dc_arm_map.cpp $(WREN7)/arm7di.cpp \
	  -CFLAGS "-O2 -I$(WREN7) -I$(WREN7)/../tools" --Mdir $(BUILD)/obj_armjob -o Vtb_top
armjob: $(BUILD)/obj_armjob/Vtb_top
arb: $(BUILD)/obj_arb/Vtb_top
	@for s in 1 2 3 4 5 6 7 8 9 10 11 12; do out=$$($(BUILD)/obj_arb/Vtb_top 4000 $$s) || { echo "$$out"; exit 1; }; \
	  echo "$$out" | tail -1; done

trace:
	@mkdir -p $(BUILD)/trc $(BUILD)/out/$(CASE)
	@$(MAKE) -s -C $(MODEL)/host $(CASE) >/dev/null
	@cd $(MODEL) && CAIQUE_TRACE=$(BUILD)/trc/$(CASE).trc CAIQUE_TRACE_OUT=1 build/host/$(CASE) $(BUILD)/out/$(CASE) >/dev/null

trace-cycle:
	@mkdir -p $(BUILD)/trc_cycle $(BUILD)/out_cycle/$(CASE)
	@$(MAKE) -s -C $(MODEL)/cycle-model $(CASE) >/dev/null
	@cd $(MODEL) && CAIQUE_TRACE=$(BUILD)/trc_cycle/$(CASE).trc CAIQUE_TRACE_OUT=1 build/cycle/$(CASE) $(BUILD)/out_cycle/$(CASE) >/dev/null

run-cycle: cosim-cycle trace-cycle
	$(BUILD)/obj_cosim_cycle/Vtb_top $(BUILD)/trc_cycle/$(CASE).trc

run: cosim trace
	$(BUILD)/obj_cosim/Vtb_top $(BUILD)/trc/$(CASE).trc -out

gate: cosim
	@fail=0; for c in $(GATE); do $(MAKE) -s trace CASE=$$c || exit 1; \
	  $(BUILD)/obj_cosim/Vtb_top $(BUILD)/trc/$$c.trc -max 3 | tail -n +1 | head -8 || fail=1; done; exit $$fail

clean:
	rm -rf $(BUILD)
