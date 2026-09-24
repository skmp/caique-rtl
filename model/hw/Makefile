# caique AICA test cases, console build (KOS).  Use ../run_hw.sh CASE..., which builds and runs through hwrun.sh.
# make needs the KOS environment: source /opt/toolchains/dc/kos/environ.sh
# Output: ../build/hw/<HWDIR>/<case>.elf  (build/ is git-ignored).  `make CASE` builds one case.  HWDIR (default hw) is
# the output directory the case writes on the host: tests/<case>/<HWDIR>/.
KOS_CFLAGS += -O2 -std=gnu11
HWDIR ?= hw
BUILD = ../build/hw/$(HWDIR)
CASES = $(basename $(notdir $(wildcard ../cases/*.c)))
all: $(CASES)
include $(KOS_BASE)/Makefile.rules
HDRS = $(wildcard ../cases/*.h ../src/*.h)
.PHONY: all clean $(CASES)
$(CASES): %: $(BUILD)/%.elf
$(BUILD)/%.elf: ../cases/%.c io_kos.c ../cases/common/replay.c $(HDRS)
	@mkdir -p $(BUILD)
	kos-cc $(CFLAGS) -DMODEL_ROOT=\"$(abspath ..)\" -DCASE=\"$*\" -DHWDIR=\"$(HWDIR)\" -o $@ ../cases/$*.c io_kos.c ../cases/common/replay.c
clean:
	-rm -rf $(BUILD)
