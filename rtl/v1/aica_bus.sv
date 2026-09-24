// aica_bus.sv -- caique AICA v1: the register map, the access executor and the wave RAM / register arbiter.
//
// Masters: the SH4 (G2), the ARM7DI (wren7) and a test port that works while the engine is frozen (ce = 0; the
// co-simulation delivers every access at a sample boundary through it).  Two lanes, each running an access in two
// clocks (X0 issues, X1 returns the data): the RAM lane (wave RAM) and the register lane (channel / common / DSP
// registers).  Both issue at t2 of a DSP step (c2 / c6: the DSP buffers' CPU cycle and the bus's wave RAM clock).
//
// Arbitration follows the console measurements of wren7-rtl/model NOTES ("Bus timing", "DSP step clock and wave RAM
// slots", "Contention", DcArmBus / DcWaits::dreamcast()):
//   - an access may start only at a DSP step boundary (the ARM's 4-MCLK grid); a request present at t0 competes for
//     that step, decided at t1;
//   - wave RAM has one slot per step: a DSP MRD / MWT of the step, slot K's fetch (step 2K - 14) or the fixed pair
//     hold it (aica_dsp, aica_sgc); otherwise the SH4 (a write one slot, a read two slots >= 2 steps apart) and then
//     the ARM get it;
//   - registers need no wave RAM slot; a TEMP (0x4000-0x43FF) / EFREG (0x4580-0x45BF) access waits while the step's
//     TWT / EWT holds that buffer's CPU port; the ARM wins the register lane over the SH4 (SH4 register traffic
//     costs the ARM nothing);
//   - an ARM memory cycle lasts 8 MCLK from its step boundary (a SWP's locked write 4): arm_ack is high in its last
//     clock.  L / M (0x2D00 / 0x2D04) are local to the ARM interface: no slot, acknowledged in the request's clock.
// The timers and the interrupt registers (SCIEB/SCIPD/SCIRE, SCILV0-2, MCIEB/MCIPD/MCIRE) are here, with the SH4's
// interrupt line; the ARM's FIQ delivery behind L / M is v2: L reads 0, M writes are ignored.
module aica_bus import aica_pkg::*; (
  input  logic         clk,
  input  logic         ce,
  input  logic [8:0]   ph,
  // test port (only while ce = 0)
  input  logic         dbg_req,
  input  logic         dbg_we,
  input  logic         dbg_ram,             // 1: wave RAM (byte address), 0: register (offset)
  input  logic [20:0]  dbg_addr,
  input  logic [31:0]  dbg_wdata,
  output logic [31:0]  dbg_rdata,
  output logic         dbg_ack,
  // SH4
  input  logic         sh_req,
  input  logic         sh_we,
  input  logic         sh_ram,
  input  logic [20:0]  sh_addr,
  input  logic [31:0]  sh_wdata,
  input  logic [3:0]   sh_be,
  output logic [31:0]  sh_rdata,
  output logic         sh_ack,
  // ARM7DI: a memory cycle the core wants to start in this clock, held until arm_ack (high in the cycle's last
  // clock; the next cycle may start the clock after)
  input  logic         arm_req,
  input  logic         arm_we,
  input  logic         arm_ram,
  input  logic         arm_lock,
  input  logic [20:0]  arm_addr,
  input  logic [31:0]  arm_wdata,
  input  logic [3:0]   arm_be,
  output logic [31:0]  arm_rdata,
  output logic         arm_ack,
  // wave RAM (the bus's clocks)
  output logic         ram_req,
  output logic         ram_we,
  output logic [19:0]  ram_waddr,
  output logic [31:0]  ram_wdata,
  output logic [3:0]   ram_be,
  input  logic [31:0]  ram_rdata,
  // slot engine
  output logic         cpu_cw,
  output logic [5:0]   cpu_cw_slot,
  output logic [4:0]   cpu_cw_reg,
  output logic [15:0]  cpu_cw_data,
  output logic         cpu_kyonex,
  output logic [5:0]   cpu_cr_slot,
  output logic [4:0]   cpu_cr_reg,
  input  logic [15:0]  cpu_cr_data,
  output logic [5:0]   mon_slot,
  output logic         mon_afsel,
  output logic         mon_rd,
  input  logic [15:0]  mon_eg,
  input  logic [15:0]  mon_ca,
  output logic         cpu_mw,
  output logic [3:0]   cpu_mw_bus,
  output logic         cpu_mw_hi,
  output logic [15:0]  cpu_mw_data,
  output logic [3:0]   cpu_mr_bus,
  input  logic [19:0]  cpu_mr_data,
  // DSP
  output logic         dsp_we,
  output logic [14:0]  dsp_off,
  output logic [15:0]  dsp_wdata,
  input  logic [15:0]  dsp_rdata,
  output logic         dsp_ramrd,
  output logic [15:0]  dsp_ramrd_hi,
  // common registers other units use
  output logic [15:0]  r2800,               // MVOL / DAC18B / MEM8MB / MONO (write-only)
  output logic [15:0]  r2804,               // RBP / RBL
  output logic [15:0]  lvl [18],            // 0x2000 .. 0x2044: EFSDL / EFPAN x16, EXTS x2
  // timers and interrupts
  input  logic [15:0]  mdec,                // MDEC_CT (the running DSP sample's): the timers' prescaler phase
  input  logic [6:0]   tim_phase,           // the prescaler's MDEC_CT phase (a console constant; simulation input)
  output logic         sh4_irq,             // the AICA's SH4 interrupt line: MCIEB & MCIPD
  // step owners (valid at t1)
  input  logic         slot_sgc,            // (even steps, c1) the stage B slot's fetch
  input  logic         slot_dsp,            // the step's MRD / MWT
  input  logic         slot_fix,            // the fixed pair
  input  logic         port_temp,           // TWT
  input  logic         port_efreg           // EWT
);
  wire [2:0] c = ph[2:0];
  wire [1:0] t = ph[1:0];                  // DSP step phase (DSP_OFFSET is a multiple of 4)

  // ---- channel register masks (model chan_mask; tests/probe) ----
  function automatic logic [15:0] chan_mask(input logic [6:0] o);
    case (o)
      7'h00: return 16'h47FF;
      7'h04, 7'h08, 7'h0C: return 16'hFFFF;
      7'h10: return 16'hFFDF;
      7'h14: return 16'h7FFF;
      7'h18, 7'h1C: return 16'hFFFF;
      7'h20: return 16'h00FF;
      7'h24, 7'h28: return 16'hFFFF;
      7'h2C, 7'h30, 7'h34, 7'h38, 7'h3C: return 16'h1FFF;
      7'h40, 7'h44: return 16'h1F1F;
      default: return 16'h0000;
    endcase
  endfunction

  typedef enum logic [1:0] {SRC_NONE, SRC_DBG, SRC_SH, SRC_ARM} src_t;

  // ---- register lane ----
  logic        g0;                         // this clock is a register access's X0
  src_t        g_src;
  logic        g_we;
  logic [14:0] off;
  logic [15:0] g_wdata;
  logic        g1;
  src_t        g1_src;
  logic [14:0] g1_off;
  // ---- RAM lane ----
  logic        r0;
  src_t        r_src;
  logic        r_we;
  logic [20:0] r_addr;
  logic [31:0] r_wdata;
  logic [3:0]  r_be;
  logic        r1, r1_we;
  src_t        r1_src;

  // ---- common register store 0x2000 .. 0x2FFF ----
  logic [9:0] cm_addr; logic [15:0] cm_q; logic cm_we;
  aica_ram #(.W(16), .D(1024)) u_common (.clk, .re(1'b1), .raddr(cm_addr), .rdata(cm_q), .we(cm_we), .waddr(cm_addr),
    .wlane(1'b1), .wdata(g_wdata));

  wire        is_chan = off < 15'h2000;
  wire        is_comm = off >= 15'h2000 && off < 15'h3000;
  wire        is_dsp  = (off >= 15'h3000 && off < 15'h3C00) || (off >= 15'h4000 && off < 15'h4500) ||
                        (off >= 15'h4580 && off < 15'h45C0);
  wire        is_mixs = off >= 15'h4500 && off < 15'h4580;

  always_comb begin
    // RAM
    ram_req = r0;
    ram_we = r_we;
    ram_waddr = r_addr[20:1];
    ram_wdata = r_wdata;
    ram_be = r_be;
    // channel registers
    cpu_cw = g0 && g_we && is_chan;
    cpu_cw_slot = off[12:7];
    cpu_cw_reg = off[6:2];
    cpu_cw_data = g_wdata & chan_mask(off[6:0]);
    cpu_kyonex = cpu_cw && off[6:0] == 7'h00 && g_wdata[15];
    cpu_cr_slot = off[12:7];
    cpu_cr_reg = off[6:2];
    // common
    cm_addr = off[11:2];
    cm_we = g0 && g_we && is_comm;
    mon_rd = g0 && !g_we && off == 15'h2810;
    // DSP
    dsp_we = g0 && g_we && is_dsp;
    dsp_off = off;
    dsp_wdata = g_wdata;
    // MIXS
    cpu_mw = g0 && g_we && is_mixs;
    cpu_mw_bus = off[6:3];
    cpu_mw_hi = off[2];
    cpu_mw_data = g_wdata;
    cpu_mr_bus = off[6:3];
  end

  // ---- timers and interrupt registers (model/cycle-model reg_write and its EDGE_PH block; tests/timer_irq,
  // timer_phase): SCIPD (ARM) and MCIPD (SH4) are separate pending registers set by the same sources and cleared by
  // SCIRE / MCIRE; bit 5 (SCPU) is the only bit a write sets; bit 10 at every sample edge (EDGE_PH); the timers tick
  // at the edge of the samples whose MDEC_CT = tim_phase mod 2^prescale (one prescaler, never restarted by a write) and
  // set bit 6 / 7 / 8 when the count wraps to 0 (no reload).  A write lands at X0, before that clock's edge.
  logic [10:0] scieb, scipd, mcieb, mcipd;
  logic [7:0]  tcnt [3];
  logic [2:0]  tpre [3];
  logic [10:0] scipd_n, mcipd_n;
  logic [7:0]  tcnt_n [3];
  logic [2:0]  tpre_n [3];
  always_comb begin
    logic wr, edge_, tick;
    logic [10:0] eset;
    logic [15:0] md;
    wr = g0 && g_we && is_comm;
    edge_ = ce && ph == 9'(EDGE_PH);
    md = mdec - 16'd1 - 16'(tim_phase);          // the model's MDEC_CT (mdec = MDEC_CT + 1 before ph 64)
    eset = edge_ ? 11'h400 : 11'd0;
    for (int tm = 0; tm < 3; tm++) begin
      tcnt_n[tm] = tcnt[tm]; tpre_n[tm] = tpre[tm];
      if (wr && off == 15'h2890 + 15'(4 * tm)) begin tcnt_n[tm] = g_wdata[7:0]; tpre_n[tm] = g_wdata[10:8]; end
      tick = edge_ && ((md & ((16'd1 << tpre_n[tm]) - 16'd1)) == 16'd0);
      if (tick && tcnt_n[tm] == 8'hFF) eset[6 + tm] = 1'b1;
      if (tick) tcnt_n[tm] = tcnt_n[tm] + 8'd1;
    end
    scipd_n = scipd; mcipd_n = mcipd;
    if (wr && off == 15'h28A0) scipd_n = scipd_n | (g_wdata[10:0] & 11'h020);
    if (wr && off == 15'h28A4) scipd_n = scipd_n & ~g_wdata[10:0];
    if (wr && off == 15'h28B8) mcipd_n = mcipd_n | (g_wdata[10:0] & 11'h020);
    if (wr && off == 15'h28BC) mcipd_n = mcipd_n & ~g_wdata[10:0];
    scipd_n = scipd_n | eset; mcipd_n = mcipd_n | eset;
  end
  always_ff @(posedge clk) begin
    scipd <= scipd_n; mcipd <= mcipd_n;
    for (int tm = 0; tm < 3; tm++) begin tcnt[tm] <= tcnt_n[tm]; tpre[tm] <= tpre_n[tm]; end
    if (g0 && g_we && is_comm && off == 15'h289C) scieb <= g_wdata[10:0];
    if (g0 && g_we && is_comm && off == 15'h28B4) mcieb <= g_wdata[10:0];
  end
  initial begin scieb = '0; scipd = '0; mcieb = '0; mcipd = '0; for (int tm = 0; tm < 3; tm++) begin tcnt[tm] = '0; tpre[tm] = '0; end end
  assign sh4_irq = |(mcieb & mcipd);

  // X1 read data (model AicaModel::read)
  logic [19:0] mr_q;
  logic [31:0] g1_rdata;
  always_comb begin
    g1_rdata = 32'd0;
    if (g1_off < 15'h2000) g1_rdata = {16'd0, cpu_cr_data & chan_mask(g1_off[6:0])};
    else if (g1_off < 15'h3000) begin
      case (g1_off)
        15'h2800: g1_rdata = 32'h0010;          // VER = 1; MVOL etc. write-only
        15'h2804: g1_rdata = 32'd0;
        15'h2808: g1_rdata = 32'h0900;
        15'h2810: g1_rdata = {16'd0, mon_eg};
        15'h2814: g1_rdata = {16'd0, mon_ca};
        15'h2890, 15'h2894, 15'h2898: g1_rdata = 32'd0;          // timers: write-only (tests/timer_irq)
        15'h289C: g1_rdata = {21'd0, scieb};
        15'h28A0: g1_rdata = {21'd0, scipd};
        15'h28B4: g1_rdata = {21'd0, mcieb};
        15'h28B8: g1_rdata = {21'd0, mcipd};
        15'h28A4, 15'h28A8, 15'h28AC, 15'h28B0, 15'h28BC: g1_rdata = 32'd0;   // SCIRE, SCILV0-2, MCIRE
        default: g1_rdata = {16'd0, cm_q};
      endcase
    end else if (g1_off >= 15'h4500 && g1_off < 15'h4580) g1_rdata = {16'd0, g1_off[2] ? mr_q[19:4] : {12'd0, mr_q[3:0]}};
    else if (g1_off < 15'h45C0) g1_rdata = {16'd0, dsp_rdata};
    dsp_ramrd = r1 && !r1_we;                  // a CPU wave RAM read: the DSP's read latch takes the upper half
    dsp_ramrd_hi = ram_rdata[31:16];
  end

  // ---- arbitration ----
  // ARM: L / M are local; everything else is a memory cycle on the step grid
  wire arm_local = arm_req && !arm_ram && ({arm_addr[14:3], 3'b000} == 15'h2D00);
  function automatic logic port_free(input logic [20:0] a);   // the TEMP / EFREG CPU port of this step
    logic [14:0] o;
    o = {a[14:2], 2'b00};
    return !((o >= 15'h4000 && o < 15'h4400 && port_temp) || (o >= 15'h4580 && o < 15'h45C0 && port_efreg));
  endfunction
  logic        arm_busy, arm_ack_q;
  logic [2:0]  arm_left;                   // clocks to the ack (from D - 2)
  logic [31:0] arm_rdata_q;
  logic        sh_busy, sh_ack_q;
  logic [1:0]  sh_rd2;                     // an SH4 RAM read waiting for its second slot: 2 = next step, 1 = eligible
  logic        sh_fin;                     // the SH4 access ends at this step's t3
  logic        arm_t0, sh_t0;              // requests present at the step boundary
  logic        g_pend, r_pend;             // granted at t1, X0 at t2
  src_t        g_psrc, r_psrc;

  wire ram_free = !(slot_dsp || slot_fix || (c == 3'd1 && slot_sgc));
  always_comb begin
    arm_ack = arm_ack_q || (arm_local && !arm_busy);
    arm_rdata = arm_local ? 32'd0 : arm_rdata_q;           // L: the v2 interrupt controller's level
  end

  always_ff @(posedge clk) begin
    dbg_ack <= 1'b0; sh_ack_q <= 1'b0; arm_ack_q <= 1'b0;
    g1 <= g0; g1_src <= g_src; g1_off <= off;
    r1 <= r0; r1_src <= r_src; r1_we <= r_we;
    mr_q <= cpu_mr_data;
    if (g0 && g_we && is_comm) begin                        // copies other units read
      if (off == 15'h2800) r2800 <= g_wdata;
      if (off == 15'h2804) r2804 <= g_wdata;
      if (off == 15'h280C) begin mon_slot <= g_wdata[13:8]; mon_afsel <= g_wdata[14]; end
      if (off < 15'h2048) lvl[off[6:2]] <= g_wdata;
    end
    // X1: data back to the master
    if (g1) case (g1_src)
      SRC_DBG: begin dbg_rdata <= g1_rdata; dbg_ack <= 1'b1; end
      SRC_SH:  begin sh_rdata <= g1_rdata; end
      SRC_ARM: arm_rdata_q <= g1_rdata;
      default: ;
    endcase
    if (r1) case (r1_src)
      SRC_DBG: begin dbg_rdata <= ram_rdata; dbg_ack <= 1'b1; end
      SRC_SH:  begin sh_rdata <= ram_rdata; end
      SRC_ARM: arm_rdata_q <= ram_rdata;
      default: ;
    endcase
    // the ARM's cycle: ack in its last clock (8 MCLK from the step boundary, a locked write 4)
    if (arm_busy) begin
      if (arm_ack_q) arm_busy <= 1'b0;
      else if (arm_left == 3'd2) arm_ack_q <= 1'b1;
      arm_left <= arm_left - 3'd1;
    end
    // the SH4's: ack at t3 of its (last) slot's step
    if (ce && t == 2'd3 && sh_fin) begin sh_ack_q <= 1'b1; sh_busy <= 1'b0; sh_fin <= 1'b0; end
    if (ce) begin
      // t0: who asks for this step
      if (t == 2'd0) begin
        arm_t0 <= arm_req && !arm_local && !arm_busy;
        sh_t0 <= sh_req && !sh_busy && !sh_ack_q;
      end
      // t1: grants
      if (t == 2'd1) begin
        logic ram_taken;
        ram_taken = !ram_free;
        if (sh_rd2 == 2'd2) sh_rd2 <= 2'd1;
        else if (sh_rd2 == 2'd1 && !ram_taken) begin sh_rd2 <= 2'd0; sh_fin <= 1'b1; ram_taken = 1'b1; end
        r_pend <= 1'b0; g_pend <= 1'b0;
        if (sh_t0 && sh_ram && !ram_taken) begin
          r_pend <= 1'b1; r_psrc <= SRC_SH; sh_busy <= 1'b1; ram_taken = 1'b1;
          if (sh_we) sh_fin <= 1'b1; else sh_rd2 <= 2'd2;
        end
        if (arm_t0 && arm_ram && !ram_taken) begin
          r_pend <= 1'b1; r_psrc <= SRC_ARM; arm_busy <= 1'b1;
          arm_left <= (arm_lock && arm_we) ? 3'd2 : 3'd6;
        end
        if (arm_t0 && !arm_ram && port_free(arm_addr)) begin
          g_pend <= 1'b1; g_psrc <= SRC_ARM; arm_busy <= 1'b1;
          arm_left <= (arm_lock && arm_we) ? 3'd2 : 3'd6;
        end else if (sh_t0 && !sh_ram && port_free(sh_addr)) begin
          g_pend <= 1'b1; g_psrc <= SRC_SH; sh_busy <= 1'b1; sh_fin <= 1'b1;
        end
        arm_t0 <= 1'b0; sh_t0 <= 1'b0;
      end
      if (t == 2'd2) begin g_pend <= 1'b0; r_pend <= 1'b0; end
    end
  end
  assign sh_ack = sh_ack_q;

  // X0 selection: a frozen engine serves the test port at once; otherwise the granted accesses at t2
  always_comb begin
    g0 = 1'b0; g_src = SRC_NONE; g_we = 1'b0; off = '0; g_wdata = '0;
    r0 = 1'b0; r_src = SRC_NONE; r_we = 1'b0; r_addr = '0; r_wdata = '0; r_be = 4'hF;
    if (!ce) begin
      if (dbg_req && dbg_ram) begin r0 = 1'b1; r_src = SRC_DBG; r_we = dbg_we; r_addr = dbg_addr; r_wdata = dbg_wdata; end
      if (dbg_req && !dbg_ram) begin g0 = 1'b1; g_src = SRC_DBG; g_we = dbg_we; off = {dbg_addr[14:2], 2'b00}; g_wdata = dbg_wdata[15:0]; end
    end else if (t == 2'd2) begin
      if (r_pend) begin
        r0 = 1'b1; r_src = r_psrc;
        if (r_psrc == SRC_SH) begin r_we = sh_we; r_addr = sh_addr; r_wdata = sh_wdata; r_be = sh_be; end
        else begin r_we = arm_we; r_addr = arm_addr; r_wdata = arm_wdata; r_be = arm_be; end
      end
      if (g_pend) begin
        g0 = 1'b1; g_src = g_psrc;
        if (g_psrc == SRC_SH) begin g_we = sh_we; off = {sh_addr[14:2], 2'b00}; g_wdata = sh_wdata[15:0]; end
        else begin g_we = arm_we; off = {arm_addr[14:2], 2'b00}; g_wdata = arm_wdata[15:0]; end
      end
    end
  end

  initial begin
    dbg_ack = 1'b0; sh_ack_q = 1'b0; arm_ack_q = 1'b0; g1 = 1'b0; r1 = 1'b0; arm_busy = 1'b0; sh_busy = 1'b0;
    arm_left = '0; sh_rd2 = '0; sh_fin = 1'b0; arm_t0 = 1'b0; sh_t0 = 1'b0; g_pend = 1'b0; r_pend = 1'b0;
    r2800 = '0; r2804 = '0; mon_slot = '0; mon_afsel = 1'b0; arm_rdata_q = '0;
    for (int i = 0; i < 18; i++) lvl[i] = '0;
  end
endmodule
