# caique analysis tools (C++, integer math): every tools/<name>.cpp -> ../build/tools/<name>, plus
# filt_validate_model (filt_validate.cpp checked against the real AicaModel, -DVERIFY_MODEL).
# Session-5 standalone fitters (single file, no model link, built by the generic rule below): koff_fit, kfit, koffdir_check,
# koffatt_check (both include feg_law.h), mixsw_check; the model-linked validators eg_model / eg_replay / tail_cmp have their own rules.
# The model-linked tools are built twice: <name> links the sample model (../sample-model), <name>_cycle the clocked model
# (../cycle-model); the source includes "aica_model.h" and the include path picks the model.
# Build: make -C tools [name ...]; run the tools from caique-rtl/model (they read tests/ and work/ relative to it).
BUILD = ../build/tools
CXXFLAGS = -O2 -std=c++17 -fopenmp
LINKED = eg_model eg_replay tail_cmp feg_validate filt_validate_model filt_overflow_model
NAMES = $(basename $(notdir $(wildcard *.cpp))) filt_validate_model filt_overflow_model $(LINKED:%=%_cycle)
HDRS = $(wildcard *.h)
SAMPLE = ../sample-model/aica_model.cpp $(wildcard ../src/*.h ../sample-model/*.h)
CYCLE = ../cycle-model/aica_model.cpp $(wildcard ../src/*.h ../cycle-model/*.h)
.PHONY: all clean $(NAMES)
all: $(NAMES)
$(NAMES): %: $(BUILD)/%
# name -> source file and extra flags of the model-linked tools
src_of = $(patsubst filt_validate_model,filt_validate,$(patsubst filt_overflow_model,filt_overflow,$(1))).cpp
def_of = $(if $(filter filt_%_model,$(1)),-DVERIFY_MODEL)
define linked
$(BUILD)/$(1): $(call src_of,$(1)) $(HDRS) $(SAMPLE)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) $(call def_of,$(1)) -I../sample-model -o $$@ $(call src_of,$(1)) ../sample-model/aica_model.cpp
$(BUILD)/$(1)_cycle: $(call src_of,$(1)) $(HDRS) $(CYCLE)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) $(call def_of,$(1)) -I../cycle-model -o $$@ $(call src_of,$(1)) ../cycle-model/aica_model.cpp
endef
$(foreach n,$(LINKED),$(eval $(call linked,$(n))))
$(BUILD)/%: %.cpp $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -o $@ $<
clean:
	-rm -rf $(BUILD)
# tools that only exist against the cycle model
$(BUILD)/sub_check: sub_check.cpp $(HDRS) $(CYCLE)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -I../cycle-model -o $@ sub_check.cpp ../cycle-model/aica_model.cpp
$(BUILD)/sched_check: sched_check.cpp $(HDRS) $(CYCLE)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -I../cycle-model -o $@ sched_check.cpp ../cycle-model/aica_model.cpp
$(BUILD)/stream_replay: stream_replay.cpp $(HDRS) $(CYCLE)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -I../cycle-model -o $@ stream_replay.cpp ../cycle-model/aica_model.cpp
