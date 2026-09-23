// order_probe: in the model, at which step does (a) a MIXS3 CPU write, (b) an SA-hi toggle on a VOFF-1 slot, (c) a KYONEX
// key-on of a witness show up in the MIXS values the DSP sees (the capture)?  All three written between the same two steps.
#include "../../src/aica_model.h"
#include <cstdio>
using namespace caique;
int main() {
    AicaModel m;
    for (int i = 0; i < 4224; i++) { m.ram[0x10000 + 2*i] = 0x00; m.ram[0x10000 + 2*i + 1] = 0x40; m.ram[0x20000 + 2*i] = 0x00; m.ram[0x20000 + 2*i + 1] = 0x20; }
    auto w = [&](int ch, int o, int v) { m.write(0x80 * ch + o, v); };
    // slot 0: VOFF 1 constant, bus 0;  slot 1: witness (AR31 D1R31 DL31 D2R31 RR31 KRS1), bus 2, VOFF 0
    w(0, 0x04, 0); w(0, 0x08, 0); w(0, 0x0C, 4096); w(0, 0x10, 31); w(0, 0x14, (15 << 10)); w(0, 0x18, 0); w(0, 0x20, 0xF0); w(0, 0x28, 0x60); w(0, 0x00, 0x0200 | 1);
    w(1, 0x04, 0); w(1, 0x08, 0); w(1, 0x0C, 32); w(1, 0x10, (31 << 11) | (31 << 6) | 31); w(1, 0x14, (1 << 10) | (31 << 5) | 31); w(1, 0x18, 0); w(1, 0x20, 0xF2); w(1, 0x28, 0x20); w(1, 0x00, 0x0200 | 1);
    // capture-like observation: the DSP-visible MIXS of the step = m.MIXS[] after step()
    w(0, 0x00, 0x4201); w(0, 0x00, 0xC201);   // slot 0 on
    for (int i = 0; i < 200; i++) m.step();
    printf("step  MIXS0    MIXS2    MIXS3   (write group before step 200)\n");
    for (int i = 195; i < 200; i++) { m.step(); printf("%4d %8d %8d %8d\n", i, m.MIXS[0], m.MIXS[2], m.MIXS[3]); }
    // the group: SA toggle on slot 0, MIXS3 := 0x123, witness key-on (KYONB|KYONEX on slot 1)
    w(0, 0x00, 0x4202); m.write(0x4500 + 8 * 3 + 4, 0x123); m.write(0x4500 + 8 * 3, 0); w(1, 0x00, 0xC201);
    for (int i = 200; i < 208; i++) { m.step(); printf("%4d %8d %8d %8d\n", i, m.MIXS[0], m.MIXS[2], m.MIXS[3]); }
    return 0;
}
