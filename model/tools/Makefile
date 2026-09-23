# caique analysis tools (C++, integer math): every tools/<name>.cpp -> ../build/tools/<name>, plus
# filt_validate_model (filt_validate.cpp checked against the real AicaModel, -DVERIFY_MODEL).
# Session-5 standalone fitters (single file, no model link, built by the generic rule below): koff_fit, kfit, koffdir_check,
# koffatt_check (both include feg_law.h), mixsw_check; the model-linked validators eg_model / eg_replay / tail_cmp have their own rules.
# Build: make -C tools [name ...]; run the tools from caique-rtl/model (they read tests/ and work/ relative to it).
BUILD = ../build/tools
CXXFLAGS = -O2 -std=c++17 -fopenmp
NAMES = $(basename $(notdir $(wildcard *.cpp))) filt_validate_model filt_overflow_model
HDRS = $(wildcard *.h)
.PHONY: all clean $(NAMES)
all: $(NAMES)
$(NAMES): %: $(BUILD)/%
$(BUILD)/feg_validate: feg_validate.cpp ../src/aica_model.cpp $(HDRS) $(wildcard ../src/*.h)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -o $@ feg_validate.cpp ../src/aica_model.cpp
$(BUILD)/filt_overflow_model: filt_overflow.cpp ../src/aica_model.cpp $(HDRS) $(wildcard ../src/*.h)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -DVERIFY_MODEL -o $@ filt_overflow.cpp ../src/aica_model.cpp
$(BUILD)/filt_validate_model: filt_validate.cpp ../src/aica_model.cpp $(HDRS) $(wildcard ../src/*.h)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -DVERIFY_MODEL -o $@ filt_validate.cpp ../src/aica_model.cpp
$(BUILD)/%: %.cpp $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -o $@ $<
clean:
	-rm -rf $(BUILD)
$(BUILD)/eg_model: eg_model.cpp ../src/aica_model.cpp $(HDRS) $(wildcard ../src/*.h)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -o $@ eg_model.cpp ../src/aica_model.cpp
$(BUILD)/eg_replay: eg_replay.cpp ../src/aica_model.cpp $(HDRS) $(wildcard ../src/*.h)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -o $@ eg_replay.cpp ../src/aica_model.cpp
$(BUILD)/tail_cmp: tail_cmp.cpp ../src/aica_model.cpp $(HDRS) $(wildcard ../src/*.h)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -o $@ tail_cmp.cpp ../src/aica_model.cpp
