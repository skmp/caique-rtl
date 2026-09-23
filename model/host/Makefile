# caique AICA test cases, model build: one host binary per case, linked with the model.  Use ../run_model.sh CASE...
# Output: ../build/host/<case> and its objects (build/ is git-ignored).  `make CASE` builds one case.
BUILD = ../build/host
CASES = $(basename $(notdir $(wildcard ../cases/*.c)))
CFLAGS = -O2 -g -Wall -Wno-unused-function
CXXFLAGS = -O2 -g -Wall -std=c++17
HDRS = $(wildcard ../cases/*.h ../src/*.h)
.PHONY: all clean $(CASES)
all: $(CASES)
$(CASES): %: $(BUILD)/%
$(BUILD)/aica_model.o: ../src/aica_model.cpp $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -c -o $@ $<
$(BUILD)/io_model.o: io_model.cpp $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -c -o $@ $<
$(BUILD)/%.case.o: ../cases/%.c $(HDRS)
	@mkdir -p $(BUILD)
	gcc $(CFLAGS) -std=gnu11 -DCASE=\"$*\" -c -o $@ $<
$(CASES:%=$(BUILD)/%): $(BUILD)/%: $(BUILD)/%.case.o $(BUILD)/io_model.o $(BUILD)/aica_model.o
	g++ -o $@ $^
clean:
	-rm -rf $(BUILD)
