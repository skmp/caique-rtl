# caique AICA test cases, console build (KOS).  Use ../run_hw.sh CASE..., which builds and runs through hwrun.sh.
# make needs the KOS environment: source /opt/toolchains/dc/kos/environ.sh
# Output: ../build/hw/<case>.elf  (build/ is git-ignored).  `make CASE` builds one case.
KOS_CFLAGS += -O2 -std=gnu11
BUILD = ../build/hw
CASES = $(basename $(notdir $(wildcard ../cases/*.c)))
all: $(CASES)
include $(KOS_BASE)/Makefile.rules
HDRS = $(wildcard ../cases/*.h ../src/*.h)
.PHONY: all clean $(CASES)
$(CASES): %: $(BUILD)/%.elf
$(BUILD)/%.elf: ../cases/%.c io_kos.c $(HDRS)
	@mkdir -p $(BUILD)
	kos-cc $(CFLAGS) -DCASE=\"$*\" -o $@ ../cases/$*.c io_kos.c
clean:
	-rm -rf $(BUILD)
