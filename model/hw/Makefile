# caique AICA test cases, console build (KOS).  Use ../run_hw.sh CASE..., which builds and runs through hwrun.sh.
# make needs the KOS environment: source /opt/toolchains/dc/kos/environ.sh
KOS_CFLAGS += -O2 -std=gnu11
CASES = $(basename $(notdir $(wildcard ../cases/*.c)))
all: $(CASES:%=%.elf)
include $(KOS_BASE)/Makefile.rules
HDRS = $(wildcard ../cases/*.h ../src/*.h)
%.elf: ../cases/%.c io_kos.c $(HDRS)
	kos-cc $(CFLAGS) -DCASE=\"$*\" -o $@ ../cases/$*.c io_kos.c
clean:
	-rm -f *.o *.elf
