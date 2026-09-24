// aica_core.sv -- caique AICA v1 top: phase sequencer, MDEC_CT, the shared multiplier, the wave RAM clock owners and
// the output mixer, around aica_sgc (slot engine), aica_dsp (effect DSP) and aica_bus (registers, masters).
// Clock: 22.5792 MHz, 512 per 44.1 kHz sample (ph).  ce = 0 freezes the engine (the test port then owns the ports).
module aica_core import aica_pkg::*; (
  input  logic         clk,
  input  logic         ce,
  input  logic [13:0]  eg_k,          // envelope counter boot constant (NOTES "Envelope clock": 6491 / 0 measured)
  input  logic         eg_par,        // the MDEC_CT parity the envelope clock ticks on (0; 0x2804 bit 15 can flip it)
  input  logic [6:0]   tim_phase,     // the timers' prescaler MDEC_CT phase (simulation; a console constant)
  output logic         sh4_irq,       // the AICA's SH4 interrupt line
  // replay parameters (model/cases/common/replay.c; simulation): ld in the clock before a sample's ph 0 loads MDEC_CT
  // and the noise LFSR, so that sample runs with them
  input  logic         ld, input logic [15:0] ld_mdec, input logic [16:0] ld_lfsr,
  output logic [8:0]   ph_o,
  // test port
  input  logic         dbg_req, input logic dbg_we, input logic dbg_ram, input logic [20:0] dbg_addr,
  input  logic [31:0]  dbg_wdata, output logic [31:0] dbg_rdata, output logic dbg_ack,
  // SH4
  input  logic         sh_req, input logic sh_we, input logic sh_ram, input logic [20:0] sh_addr,
  input  logic [31:0]  sh_wdata, input logic [3:0] sh_be, output logic [31:0] sh_rdata, output logic sh_ack,
  // ARM7DI
  input  logic         arm_req, input logic arm_we, input logic arm_ram, input logic arm_lock,
  input  logic [20:0]  arm_addr, input logic [31:0] arm_wdata, input logic [3:0] arm_be,
  output logic [31:0]  arm_rdata, output logic arm_ack,
  // wave RAM: one access per clock, data the next clock ({word(a+1), word(a)}); be[1:0] word a, be[3:2] word a+1
  output logic         ram_req, output logic ram_we, output logic [19:0] ram_waddr,
  output logic [31:0]  ram_wdata, output logic [3:0] ram_be, input logic [31:0] ram_rdata,
  // DAC side (one pair per sample, 18 clocks after the DSP boundary)
  output logic signed [15:0] out_l, output logic signed [15:0] out_r, output logic out_valid,
  output logic [15:0]  mdec_o,
  output logic         warn_step
);
  logic [8:0]  ph;
  logic [15:0] mdec;
  wire  [2:0]  c = ph[2:0];
  always_ff @(posedge clk) if (ce) begin
    ph <= ph + 9'd1;
    if (ph == 9'(DSP_OFFSET - 1)) mdec <= mdec - 16'd1;    // MDEC_CT counts down at the DSP's sample boundary
    if (ld) mdec <= ld_mdec;
  end
  initial begin ph = '0; mdec = 16'd2; end                  // the model's first DSP sample has MDEC_CT 1
  assign ph_o = ph;
  assign mdec_o = mdec;

  // ---- units ----
  logic cpu_cw, cpu_kyonex, mon_afsel, mon_rd, cpu_mw, cpu_mw_hi, dsp_we, dsp_ramrd;
  logic [5:0] cpu_cw_slot, cpu_cr_slot, mon_slot;
  logic [4:0] cpu_cw_reg, cpu_cr_reg;
  logic [15:0] cpu_cw_data, cpu_cr_data, mon_eg, mon_ca, cpu_mw_data, dsp_wdata, dsp_rdata, dsp_ramrd_hi, r2800, r2804;
  logic [3:0] cpu_mw_bus, cpu_mr_bus, dsp_mixs_bus;
  logic [19:0] cpu_mr_data, dsp_mixs;
  logic [14:0] dsp_off;
  logic [15:0] lvl [18];
  logic signed [15:0] efreg [16];
  logic signed [24:0] sgc_ma, dsp_ma; logic signed [12:0] sgc_mb, dsp_mb; logic signed [37:0] mul_p;
  logic sgc_rreq, dsp_rreq, dsp_rwe, bus_rreq, bus_rwe;
  logic [19:0] sgc_raddr, dsp_raddr, bus_raddr;
  logic [15:0] dsp_rwdata; logic [31:0] bus_rwdata; logic [3:0] bus_rbe;
  logic signed [31:0] dsum_l, dsum_r;
  logic slot_sgc, slot_dsp, slot_fix, port_temp, port_efreg;
  logic [15:0] sgc_lw;

  aica_sgc u_sgc (.clk, .ce, .ph, .mdec, .eg_k, .eg_par, .ld, .ld_lfsr,
    .cpu_cw, .cpu_cw_slot, .cpu_cw_reg, .cpu_cw_data, .cpu_kyonex,
    .cpu_cr(1'b0), .cpu_cr_slot, .cpu_cr_reg, .cpu_cr_data,
    .mon_slot, .mon_afsel, .mon_rd, .mon_eg, .mon_ca,
    .cpu_mw, .cpu_mw_bus, .cpu_mw_hi, .cpu_mw_data, .cpu_mr_bus, .cpu_mr_data,
    .dsp_mixs_bus, .dsp_mixs,
    .ram_req(sgc_rreq), .ram_waddr(sgc_raddr), .ram_rdata, .slot_sgc, .sgc_lw,
    .mul_a(sgc_ma), .mul_b(sgc_mb), .mul_p, .dsum_l, .dsum_r, .warn_step);

  aica_dsp u_dsp (.clk, .ce, .ph, .mdec, .rbpl(r2804),
    .cpu_we(dsp_we), .cpu_off(dsp_off), .cpu_wdata(dsp_wdata), .cpu_rdata(dsp_rdata),
    .cpu_ramrd(dsp_ramrd), .cpu_ramrd_hi(dsp_ramrd_hi),
    .mixs_bus(dsp_mixs_bus), .mixs_val(dsp_mixs),
    .ram_req(dsp_rreq), .ram_we(dsp_rwe), .ram_waddr(dsp_raddr), .ram_wdata(dsp_rwdata), .ram_rdata, .slot_sgc, .sgc_lw,
    .mul_a(dsp_ma), .mul_b(dsp_mb), .mul_p, .efreg, .slot_dsp, .slot_fix, .port_temp, .port_efreg);

  aica_bus u_bus (.clk, .ce, .ph,
    .dbg_req, .dbg_we, .dbg_ram, .dbg_addr, .dbg_wdata, .dbg_rdata, .dbg_ack,
    .sh_req, .sh_we, .sh_ram, .sh_addr, .sh_wdata, .sh_be, .sh_rdata, .sh_ack,
    .arm_req, .arm_we, .arm_ram, .arm_lock, .arm_addr, .arm_wdata, .arm_be, .arm_rdata, .arm_ack,
    .ram_req(bus_rreq), .ram_we(bus_rwe), .ram_waddr(bus_raddr), .ram_wdata(bus_rwdata), .ram_be(bus_rbe), .ram_rdata,
    .cpu_cw, .cpu_cw_slot, .cpu_cw_reg, .cpu_cw_data, .cpu_kyonex, .cpu_cr_slot, .cpu_cr_reg, .cpu_cr_data,
    .mon_slot, .mon_afsel, .mon_rd, .mon_eg, .mon_ca,
    .cpu_mw, .cpu_mw_bus, .cpu_mw_hi, .cpu_mw_data, .cpu_mr_bus, .cpu_mr_data,
    .dsp_we, .dsp_off, .dsp_wdata, .dsp_rdata, .dsp_ramrd, .dsp_ramrd_hi,
    .r2800, .r2804, .lvl, .mdec, .tim_phase, .sh4_irq, .slot_sgc, .slot_dsp, .slot_fix, .port_temp, .port_efreg);

  // ---- the shared multiplier: DSP on c2 / c6, the slot engine on the other six clocks ----
  wire dsp_mul = (c == 3'd2 || c == 3'd6);
  assign mul_p = 38'(dsp_mul ? dsp_ma : sgc_ma) * 38'(dsp_mul ? dsp_mb : sgc_mb);

  // ---- wave RAM clock owners: c0 / c4 / c5 slot engine, c1 / c3 / c7 DSP, c2 / c6 bus (test port when frozen) ----
  always_comb begin
    ram_req = 1'b0; ram_we = 1'b0; ram_waddr = sgc_raddr; ram_wdata = '0; ram_be = 4'hF;
    if (!ce) begin ram_req = bus_rreq; ram_we = bus_rwe; ram_waddr = bus_raddr; ram_wdata = bus_rwdata; ram_be = bus_rbe; end
    else case (c)
      3'd0, 3'd4, 3'd5: begin ram_req = sgc_rreq; ram_waddr = sgc_raddr; end
      3'd1, 3'd3, 3'd7: begin ram_req = dsp_rreq; ram_we = dsp_rwe; ram_waddr = dsp_raddr; ram_wdata = {16'd0, dsp_rwdata}; ram_be = 4'b0011; end
      default: begin ram_req = bus_rreq; ram_we = bus_rwe; ram_waddr = bus_raddr; ram_wdata = bus_rwdata; ram_be = bus_rbe; end
    endcase
  end

  // ---- output mixer (model step(): direct sends + EFREG through EFSDL / EFPAN, MONO, MVOL, DAC18B; the
  //      analog side is not observable digitally, so this is the model's unverified law).  Sample n's direct sum is
  //      complete at ph 8 of n+1 and held; EFREG of the DSP sample that ends at ph DSP_OFFSET is snapshot the clock
  //      after; one EFREG per clock. ----
  logic signed [15:0] efs [16];
  logic signed [31:0] ml, mr;
  function automatic void volpan(input logic signed [31:0] value, input logic [15:0] v,
                                 inout logic signed [31:0] l, inout logic signed [31:0] r);
    logic signed [31:0] tmp, sc;
    tmp = send_level(value, v[11:8]);
    sc = (v[3:0] == 4'hF) ? 32'sd0 : send_level(tmp, 4'hF - v[3:0]);
    if (v[4]) begin l = l + tmp; r = r + sc; end else begin l = l + sc; r = r + tmp; end
  endfunction
  always_ff @(posedge clk) begin
    out_valid <= 1'b0;
    if (ce) begin
      if (ph == 9'(DSP_OFFSET + 1)) begin
        for (int i = 0; i < 16; i++) efs[i] <= efreg[i];
        ml <= dsum_l; mr <= dsum_r;
      end else if (ph >= 9'(DSP_OFFSET + 2) && ph < 9'(DSP_OFFSET + 18)) begin
        logic signed [31:0] l, r;
        logic [3:0] e;
        e = 4'(ph - 9'(DSP_OFFSET + 2));
        l = ml; r = mr;
        volpan(32'(efs[e]), lvl[e], l, r);
        ml <= l; mr <= r;
      end else if (ph == 9'(DSP_OFFSET + 18)) begin
        logic signed [31:0] l, r;
        l = ml; r = mr;
        if (r2800[15]) begin l = l + r; r = l; end
        l = send_level(l, r2800[3:0]); r = send_level(r, r2800[3:0]);
        if (r2800[8]) begin l = l >>> 2; r = r >>> 2; end
        out_l <= (l > 32767) ? 16'sd32767 : (l < -32768) ? -16'sd32768 : 16'(l);
        out_r <= (r > 32767) ? 16'sd32767 : (r < -32768) ? -16'sd32768 : 16'(r);
        out_valid <= 1'b1;
      end
    end
  end
  initial begin out_l = '0; out_r = '0; out_valid = 1'b0; ml = '0; mr = '0; end
endmodule
