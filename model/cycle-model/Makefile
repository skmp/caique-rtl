# caique cycle model: the test cases against the clocked model (io_cycle.cpp: every access through the SH4 port) and
# the comparison tools.  Builds go to ../build/cycle (git-ignored).  Use ../run_cycle.sh CASE... to run cases.
#   make [CASE ...]        case binaries -> ../build/cycle/<case>
#   make trace_replay      the sample model's traces replayed into the cycle model (replay_all.sh, TODO gate 1.2)
BUILD = ../build/cycle
CASES = $(basename $(notdir $(wildcard ../cases/*.c)))
CFLAGS = -O2 -g -Wall -Wno-unused-function
CXXFLAGS = -O2 -g -Wall -std=c++17
HDRS = $(wildcard ../cases/*.h ../src/*.h *.h)
.PHONY: all clean trace_replay $(CASES)
all: $(CASES) trace_replay
$(CASES): %: $(BUILD)/%
trace_replay: $(BUILD)/trace_replay
$(BUILD)/aica_model.o: aica_model.cpp $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -c -o $@ $<
$(BUILD)/io_cycle.o: io_cycle.cpp $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -c -o $@ $<
$(BUILD)/replay.o: ../cases/common/replay.c $(HDRS)
	@mkdir -p $(BUILD)
	gcc $(CFLAGS) -std=gnu11 -c -o $@ $<
$(BUILD)/%.case.o: ../cases/%.c $(HDRS)
	@mkdir -p $(BUILD)
	gcc $(CFLAGS) -std=gnu11 -DCASE=\"$*\" -c -o $@ $<
$(CASES:%=$(BUILD)/%): $(BUILD)/%: $(BUILD)/%.case.o $(BUILD)/io_cycle.o $(BUILD)/replay.o $(BUILD)/aica_model.o
	g++ -o $@ $^
$(BUILD)/trace_replay: trace_replay.cpp $(BUILD)/aica_model.o
	g++ $(CXXFLAGS) -o $@ $^
clean:
	-rm -rf $(BUILD)
