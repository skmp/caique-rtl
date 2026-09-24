// aica_dsp.sv -- caique AICA v1: the effect DSP.  128 steps of 4 clocks per sample, starting 8 frames after the slot
// sweep (DSP_OFFSET, aica_pkg): step s runs at ph = 64 + 4 s, t = ph[1:0].  The manual's buffer table (T0 = DSP
// read, T1 = DSP write / CPU, two T per step) maps to:
//   t0  MPRO[s] / COEF[s] read; MADRS for a deferred even-step read; the memory read latch takes a landed read
//       (odd steps only: a read lands two steps after its odd step)
//   t1  decode; TEMP[(TRA + MDEC_CT)] / MEMS[IRA] / MADRS[MASA] reads; SHIFTED of the previous step's ACC
//   t2  X * Y on the shared multiplier (clock c2 / c6), ACC, Y_REG (YRL); the CPU's buffer window
//   t3  TEMP / MEMS / FRC / ADRS / EFREG writes; the memory write (even step c3, odd step c7); an odd step's read
//       address goes to the frame's single read slot (c1 of the next frame)
// Semantics: model/sample-model/aica_model.cpp dsp_step (tests/dsp_basic, dsp_mem, dsp_mem2, dsp_mem3, dsp_wslot).
// A playing channel's fetch owns the wave RAM slot of its even step (tests/dsp_coll, wren7 tests/hw/collide2): an MWT
// there is not written, and an MRD there returns the word the channel's access left (sgc_lw) instead of its own.
// For the bus arbiter (aica_bus) the DSP reports, at t1 of each step, whether the step holds the step's wave RAM slot
// (MRD or MWT, any step: wren7 tests/hw/dspmem) and the TEMP / EFREG CPU port (TWT / EWT: tests/hw/dspport), and
// where the fixed pair sits: MPRO rows 108 .. 112 are read ahead on the free MPRO port clock (t3 of steps 100 .. 104)
// and placed by the measured rule (fix_place).
module aica_dsp import aica_pkg::*; (
  input  logic         clk,
  input  logic         ce,
  input  logic [8:0]   ph,
  input  logic [15:0]  mdec,
  input  logic [15:0]  rbpl,         // 0x2804: RBP[11:0], RBL[14:13]
  // CPU buffer access (window cycle c2 / c6, or frozen); reads return the next clock
  input  logic         cpu_we,
  input  logic [14:0]  cpu_off,      // register offset 0x3000 .. 0x45BF
  input  logic [15:0]  cpu_wdata,
  output logic [15:0]  cpu_rdata,
  input  logic         cpu_ramrd,    // a CPU wave RAM read: the read latch takes its upper half (tests/dsp_mem3)
  input  logic [15:0]  cpu_ramrd_hi,
  // MIXS (the bank the slots filled in the previous sample)
  output logic [3:0]   mixs_bus,
  input  logic [19:0]  mixs_val,
  // wave RAM
  output logic         ram_req,
  output logic         ram_we,
  output logic [19:0]  ram_waddr,
  output logic [15:0]  ram_wdata,
  input  logic [31:0]  ram_rdata,
  input  logic         slot_sgc,     // (even steps, t1) a channel owns this step's wave RAM slot
  input  logic [15:0]  sgc_lw,       // (from t2) the word its access left
  // shared multiplier (t2)
  output logic signed [24:0] mul_a,
  output logic signed [12:0] mul_b,
  input  logic signed [37:0] mul_p,
  output logic signed [15:0] efreg [16],
  // bus arbiter (valid at t1 of the step)
  output logic         slot_dsp,     // this step's MRD / MWT holds the wave RAM slot
  output logic         slot_fix,     // the fixed pair holds it
  output logic         port_temp,    // TWT: the TEMP CPU port is the DSP's this step
  output logic         port_efreg    // EWT: the EFREG CPU port is the DSP's this step
);
  wire [8:0] dph = ph - 9'(DSP_OFFSET);
  wire [6:0] step = dph[8:2];
  wire [1:0] t = dph[1:0];
  wire [2:0] c = ph[2:0];
  wire       cpu_slot = !ce || t == 2'd2;       // the buffers' CPU cycle

  // ---- buffers ----
  logic [6:0] ip_raddr; logic [63:0] mpro_q; logic [12:0] coef_q;
  logic cpu_mpro_we, cpu_coef_we, cpu_madrs_we;
  wire  [6:0] cpu_mpro_row = cpu_off[10:4] ^ 7'h40;          // (offset - 0x3400) >> 4
  aica_ram #(.W(64), .D(128), .LANES(4)) u_mpro (.clk, .re(1'b1), .raddr(ip_raddr), .rdata(mpro_q),
    .we(cpu_mpro_we), .waddr(cpu_mpro_row), .wlane(4'b1 << cpu_off[3:2]), .wdata({4{cpu_wdata}}));
  logic [6:0] cf_raddr;
  aica_ram #(.W(13), .D(128)) u_coef (.clk, .re(1'b1), .raddr(cf_raddr), .rdata(coef_q),
    .we(cpu_coef_we), .waddr(cpu_off[8:2]), .wlane(1'b1), .wdata(cpu_wdata[15:3]));
  logic [6:0] ma_raddr; logic [15:0] ma_q;
  aica_ram #(.W(16), .D(128)) u_madrs (.clk, .re(1'b1), .raddr(ma_raddr), .rdata(ma_q),
    .we(cpu_madrs_we), .waddr(cpu_off[8:2]), .wlane(1'b1), .wdata(cpu_wdata));
  logic [6:0] tp_raddr, tp_waddr; logic [23:0] tp_q, tp_wdata; logic tp_we; logic [1:0] tp_lane;
  aica_ram #(.W(24), .D(128), .LANES(3)) u_temp (.clk, .re(1'b1), .raddr(tp_raddr), .rdata(tp_q),
    .we(tp_we), .waddr(tp_waddr), .wlane({tp_lane[1], tp_lane[1], tp_lane[0]}), .wdata(tp_wdata));
  logic [4:0] ms_raddr, ms_waddr; logic [23:0] ms_q, ms_wdata; logic ms_we; logic [1:0] ms_lane;
  aica_ram #(.W(24), .D(32), .LANES(3)) u_mems (.clk, .re(1'b1), .raddr(ms_raddr), .rdata(ms_q),
    .we(ms_we), .waddr(ms_waddr), .wlane({ms_lane[1], ms_lane[1], ms_lane[0]}), .wdata(ms_wdata));

  // ---- decoded instruction (dsp_asm.h field positions) ----
  typedef struct packed {
    logic [6:0] tra; logic twt; logic [6:0] twa;
    logic xsel; logic [1:0] ysel; logic [5:0] ira; logic iwt; logic [4:0] iwa;
    logic table_; logic mwt; logic mrd; logic ewt; logic [3:0] ewa; logic adrl; logic frcl; logic [1:0] shift;
    logic yrl; logic negb; logic zero; logic bsel;
    logic nofl; logic [5:0] masa; logic adreb; logic nxadr;
  } ins_t;
  function automatic ins_t dec(input logic [63:0] w);
    ins_t i; logic [15:0] w0, w1, w2, w3;
    w0 = w[15:0]; w1 = w[31:16]; w2 = w[47:32]; w3 = w[63:48];
    i.tra = w0[15:9]; i.twt = w0[8]; i.twa = w0[7:1];
    i.xsel = w1[15]; i.ysel = w1[14:13]; i.ira = w1[12:7]; i.iwt = w1[6]; i.iwa = w1[5:1];
    i.table_ = w2[15]; i.mwt = w2[14]; i.mrd = w2[13]; i.ewt = w2[12]; i.ewa = w2[11:8]; i.adrl = w2[7];
    i.frcl = w2[6]; i.shift = w2[5:4]; i.yrl = w2[3]; i.negb = w2[2]; i.zero = w2[1]; i.bsel = w2[0];
    i.nofl = w3[15]; i.masa = w3[14:9]; i.adreb = w3[8]; i.nxadr = w3[7];
    return i;
  endfunction

  // ---- state ----
  ins_t        in;                       // latched at the end of t1
  logic [12:0] coef;
  logic signed [25:0] acc;
  logic signed [23:0] shifted, inputs, yreg;
  logic [12:0] frc;
  logic [11:0] adrs;
  logic [15:0] memval, mem_pend;
  logic        mem_land, rd_fly;
  logic [1:0]  nofl_pipe;                // NOFL of steps s-1, s-2
  logic        ev_valid;                 // an even-step MRD waiting for the odd slot
  ins_t        ev_in;
  logic        rd_post;                  // the frame's read (executes at c1 of the next frame)
  logic [19:0] rd_addr;
  logic        run;                      // the DSP started (the first ph DSP_OFFSET)
  logic [15:0] ma_w, ma_r;               // MADRS[in.masa] (t1 read), MADRS[ev_in.masa] (t0 read)
  logic        coll_q;                   // this (even) step's slot belongs to a channel (latched at t1)
  logic        ev_coll, rd_coll;         // the waiting even-step read / the posted read was in a channel's slot
  logic [15:0] ev_word, rd_word;         // ... and the channel's word it returns

  wire ins_t in_c = dec(mpro_q);         // valid at t1 (read at t0)
  wire deferred = step[0] && ev_valid && !in.mrd;   // this odd step executes the waiting even-step read

  // ring / table address (dsp_step's ADDR)
  function automatic logic [19:0] maddr(input logic [15:0] madrs, input ins_t i, input logic [11:0] adrs_r,
                                        input logic [15:0] md, input logic [15:0] rbpl_r);
    logic [31:0] a; logic [31:0] rbl;
    a = 32'(madrs);
    if (i.adreb) a = a + 32'($signed(adrs_r));
    if (i.nxadr) a = a + 32'd1;
    rbl = (32'd8192 << rbpl_r[14:13]) - 32'd1;
    if (!i.table_) a = (a + 32'(md)) & rbl;
    else a = a & 32'hFFFF;
    return 20'(a + {rbpl_r[11:0], 10'd0});
  endfunction

  // ---- the fixed pair (wren7 dc_arm_map.cpp slot_used): nominally steps 109 and 111; where the DSP holds one, it
  //      moves one step earlier or later, the two staying >= 2 steps apart, earliest placement first; no placement:
  //      109 / 111.  d[i] / result[i]: step FIX_STEP0 + i.
  function automatic logic [4:0] fix_place(input logic [4:0] d);
    logic [4:0] m; logic found;
    m = 5'b01010; found = 1'b0;
    for (int f = 0; f <= 2; f++) begin
      // first: 109 (f = 1) when free, else 108 then 110
      automatic int fo = (f == 0) ? 1 : (f == 1) ? 0 : 2;
      if (!found && !d[fo] && (fo == 1 || d[1])) begin
        for (int g = 0; g <= 2; g++) begin
          automatic int so = (g == 0) ? 3 : (g == 1) ? 2 : 4;
          if (!found && !d[so] && (so == 3 || d[3]) && so - fo >= 2) begin
            m = 5'(1 << fo) | 5'(1 << so); found = 1'b1;
          end
        end
      end
    end
    return m;
  endfunction
  logic [4:0] fix_d;                     // MRD | MWT of steps 108 .. 112, read ahead in this sample
  logic [4:0] fix_m;
  always_ff @(posedge clk) if (ce && t == 2'd0 && step >= 7'd101 && step <= 7'd105)
    fix_d[step - 7'd101] <= mpro_q[45] | mpro_q[46];                 // w2 MRD / MWT of row step + 7
  always_comb begin
    fix_m = fix_place(fix_d);
    slot_fix = run && step >= 7'(FIX_STEP0) && step < 7'(FIX_STEP0 + 5) && fix_m[step - 7'(FIX_STEP0)];
    slot_dsp = run && (in_c.mrd || in_c.mwt);
    port_temp = run && in_c.twt;
    port_efreg = run && in_c.ewt;
  end

  // ---- read addresses ----
  always_comb begin
    ip_raddr = step;                      // t0: this step's instruction and coefficient
    if (t == 2'd3 && step >= 7'd100 && step <= 7'd104) ip_raddr = step + 7'd8;   // the fixed pair's look-ahead
    cf_raddr = step;
    ma_raddr = {1'b0, ev_in.masa};        // t0: the waiting even-step read
    if (t == 2'd1) ma_raddr = {1'b0, in_c.masa};
    tp_raddr = 7'(in_c.tra + mdec[6:0]);  // t1
    ms_raddr = in_c.ira[4:0];
    if (cpu_slot) begin
      ip_raddr = cpu_mpro_row; cf_raddr = cpu_off[8:2]; ma_raddr = cpu_off[8:2];
      tp_raddr = cpu_off[9:3]; ms_raddr = cpu_off[7:3];
    end
  end

  // ---- t2 datapath ----
  logic signed [23:0] inp_c, tempr, x, y, sh_c;
  logic signed [25:0] b;
  always_comb begin
    // INPUTS: MEMS (as stored), MIXS << 4, EXTS << 8 (0: no disc playing), else 0
    if (in.ira <= 6'h1F) inp_c = ms_q;
    else if (in.ira <= 6'h2F) inp_c = {mixs_val, 4'd0};
    else inp_c = 24'sd0;
    mixs_bus = in.ira[3:0];
    tempr = tp_q;
    b = in.zero ? 26'sd0 : in.bsel ? acc : 26'(tempr);
    if (in.negb) b = -b;
    x = in.xsel ? inp_c : tempr;
    case (in.ysel)
      2'd0: y = 24'($signed(frc));
      2'd1: y = 24'($signed(coef));
      2'd2: y = 24'($signed(yreg[23:11]));
      default: y = {12'd0, yreg[15:4]};
    endcase
    mul_a = 25'(x);
    mul_b = 13'(y);
  end
  // SHIFTED of the previous step's ACC (t1)
  always_comb begin
    case (in_c.shift)
      2'd0: sh_c = (acc > 26'sd8388607) ? 24'sd8388607 : (acc < -26'sd8388608) ? -24'sd8388608 : 24'(acc);
      2'd1: sh_c = ((acc <<< 1) > 26'sd8388607) ? 24'sd8388607 : ((acc <<< 1) < -26'sd8388608) ? -24'sd8388608 : 24'(acc <<< 1);
      2'd2: sh_c = 24'(acc <<< 1);
      default: sh_c = 24'(acc);
    endcase
  end

  // ---- memory (t3 writes on c3 / c7, the read on c1 of the next frame) ----
  logic [19:0] mw_addr_c, mr_addr_c; logic [15:0] mw_data_c;
  always_comb begin
    mw_addr_c = maddr(ma_w, in, adrs, mdec, rbpl);
    mw_data_c = in.nofl ? shifted[23:8] : dsp_pack(shifted);
    mr_addr_c = deferred ? maddr(ma_r, ev_in, adrs, mdec, rbpl) : mw_addr_c;
    ram_req = 1'b0; ram_we = 1'b0; ram_waddr = rd_addr; ram_wdata = mw_data_c;
    if (ce && run) begin
      if (c == 3'd1) begin ram_req = rd_post && !rd_coll; ram_we = 1'b0; ram_waddr = rd_addr; end
      if (t == 2'd3) begin ram_req = in.mwt && !coll_q; ram_we = 1'b1; ram_waddr = mw_addr_c; ram_wdata = mw_data_c; end
    end
  end

  // ---- buffer writes: the DSP at t3, the CPU in its window ----
  logic cpu_temp, cpu_mems;
  always_comb begin
    cpu_temp = cpu_off[14:10] == 5'b10000;                  // 0x4000 .. 0x43FF
    cpu_mems = cpu_off[14:8] == 7'b1000100;                 // 0x4400 .. 0x44FF
    cpu_coef_we = cpu_we && cpu_off[14:9] == 6'b011000;     // 0x3000 .. 0x31FF
    cpu_madrs_we = cpu_we && cpu_off[14:9] == 6'b011001;    // 0x3200 .. 0x33FF
    cpu_mpro_we = cpu_we && (cpu_off[14:10] == 5'b01101 || cpu_off[14:10] == 5'b01110);   // 0x3400 .. 0x3BFF
    tp_we = 1'b0; tp_waddr = 7'(in.twa + mdec[6:0]); tp_wdata = shifted; tp_lane = 2'b11;
    ms_we = 1'b0; ms_waddr = in.iwa; ms_lane = 2'b11;
    ms_wdata = nofl_pipe[1] ? {memval, 8'd0} : dsp_unpack(memval);
    if (ce && run && t == 2'd3) begin tp_we = in.twt; ms_we = in.iwt; end
    if (cpu_we && cpu_temp) begin
      tp_we = 1'b1; tp_waddr = cpu_off[9:3]; tp_wdata = {cpu_wdata, cpu_wdata[7:0]};
      tp_lane = cpu_off[2] ? 2'b10 : 2'b01;
    end
    if (cpu_we && cpu_mems && cpu_off[2]) begin             // +0 (bits 7:0) is not CPU-writable (tests/probe)
      ms_we = 1'b1; ms_waddr = cpu_off[7:3]; ms_wdata = {cpu_wdata, 8'd0}; ms_lane = 2'b10;
    end
  end

  // CPU reads (data the next clock)
  logic [14:0] roff_q;
  always_ff @(posedge clk) roff_q <= cpu_off;
  always_comb begin
    cpu_rdata = 16'd0;
    if (roff_q[14:9] == 6'b011000) cpu_rdata = {coef_q, 3'd0};
    else if (roff_q[14:9] == 6'b011001) cpu_rdata = ma_q;
    else if (roff_q[14:10] == 5'b01101 || roff_q[14:10] == 5'b01110) cpu_rdata = mpro_q[roff_q[3:2]*16 +: 16];
    else if (roff_q[14:10] == 5'b10000) cpu_rdata = roff_q[2] ? tp_q[23:8] : {8'd0, tp_q[7:0]};
    else if (roff_q[14:8] == 7'b1000100) cpu_rdata = roff_q[2] ? ms_q[23:8] : {8'd0, ms_q[7:0]};
    else if (roff_q >= 15'h4580 && roff_q < 15'h45C0) cpu_rdata = efreg[roff_q[5:2]];
  end

  always_ff @(posedge clk) begin
    if (cpu_ramrd) memval <= cpu_ramrd_hi;
    if (cpu_we && cpu_off >= 15'h4580 && cpu_off < 15'h45C0) efreg[cpu_off[5:2]] <= cpu_wdata;
    if (ce) begin
      if (ph == 9'(DSP_OFFSET)) run <= 1'b1;
      if (run || ph == 9'(DSP_OFFSET)) begin
        if (c == 3'd1 && rd_post) begin rd_fly <= 1'b1; rd_post <= 1'b0; end
        if (c == 3'd2 && rd_fly) begin mem_pend <= rd_coll ? rd_word : ram_rdata[15:0]; mem_land <= 1'b1; rd_fly <= 1'b0; end
        case (t)
          2'd0: begin
            if (step[0] && mem_land) begin memval <= mem_pend; mem_land <= 1'b0; end   // lands at odd steps
          end
          2'd1: begin
            coll_q <= !step[0] && slot_sgc;
            in <= in_c; coef <= coef_q;
            shifted <= sh_c;
            ma_r <= ma_q;                                            // MADRS[ev_in.masa] (t0 read)
          end
          2'd2: begin
            inputs <= inp_c;
            acc <= 26'((mul_p >>> 12) + 38'(b));
            if (in.yrl) yreg <= inp_c;
            ma_w <= ma_q;                                            // MADRS[in.masa] (t1 read)
          end
          default: begin                                             // t3
            if (in.frcl) frc <= (in.shift == 2'd3) ? {1'b0, shifted[11:0]} : shifted[23:11];
            if (in.ewt) efreg[in.ewa] <= shifted[23:8];
            if (step[0]) begin
              if ((in.mrd && !in.mwt) || deferred) begin
                rd_post <= 1'b1; rd_addr <= mr_addr_c;
                rd_coll <= deferred && !in.mrd && ev_coll; rd_word <= ev_word;
              end
              ev_valid <= 1'b0;
            end else if (in.mrd && !in.mwt) begin ev_valid <= 1'b1; ev_in <= in; ev_coll <= coll_q; ev_word <= sgc_lw; end
            if (in.adrl) adrs <= (in.shift == 2'd3) ? shifted[23:12] : 12'(inputs >>> 16);
            nofl_pipe <= {nofl_pipe[0], in.nofl};
          end
        endcase
      end
    end
  end
  initial begin
    in = '0; coef = '0; acc = '0; shifted = '0; inputs = '0; frc = '0; yreg = '0; adrs = '0; memval = '0;
    mem_pend = '0; mem_land = 1'b0; rd_fly = 1'b0; nofl_pipe = '0; ev_valid = 1'b0; ev_in = '0; rd_post = 1'b0;
    rd_addr = '0; run = 1'b0; ma_w = '0; ma_r = '0; fix_d = '0;
    coll_q = 1'b0; ev_coll = 1'b0; rd_coll = 1'b0; ev_word = '0; rd_word = '0;
    for (int i = 0; i < 16; i++) efreg[i] = '0;
  end
endmodule
