// aica_sgc.sv -- caique AICA v1: the sound generator.  ONE slot engine, time-multiplexed over the 64 slots: frame k
// (8 clocks, ph[8:3] == k) is slot k's.  The pipeline (model/cycle-model/aica_model.cpp; the console's register-write
// thresholds of tests/sub_frame: 0x00-0x1C in frame k, 0x28 in frame k + 4, 0x20 in frame k + 7):
//
//   stage A, frame k   : reads slot k's registers and state (a write that lands before frame k is used in this sample),
//                        advances the phase and the loop, fetches the wave words, advances the LFO, commits a pending
//                        stop, and processes the key events latched at this sample's boundary.
//   stage B, frame k+1 : decodes (PCM / ADPCM / noise) and interpolates -> FIFO 1.
//   level,   frame k+4 : filter (Q, LPOFF) and level (TL, VOFF, AEG, ALFO), 0x28 read at c7 of frame k+3 -> FIFO 2.
//   envelope, frame k+5: the envelopes of the NEXT sample (tests/sub_env), 0x10 / 0x14 / 0x18 / 0x30 .. 0x44 read at c7
//                        of frame k+4; state and EG monitor written at c7.
//   send,    frame k+7 : the send (ISEL, IMXL) and the direct outputs (DISDL, DIPAN), 0x20 / 0x24 read at c7 of frame
//                        k+6; the MIXS write at c7 of frame k+8 (the bank the DSP reads during the next sample).
//
// Clock use within a frame (c = ph[2:0]); B = stage B's slot (frame - 1), L = the level stage's (frame - 4):
//   c0  row 1 read                          | B: the slot's own wave RAM step (its last word), LFSR step, decode | L: damping
//   c1  row 0 read, pitch-LFO multiply      | B: noise override
//   c2  (CPU window A: register port)       |                          (multiplier: DSP even step)
//   c3  row 4 read, stepping (latched)      | B: interpolation multiply
//   c4  row 2 read, fetch 1                 | L: k * H
//   c5  row 3 read, fetch 2                 | L: k * B
//   c6  key events, state + CA monitor write |                         (CPU window B; multiplier: DSP odd step)
//   c7  state reads for the next frame      | B: state write, FIFO 1 | L: level, filter state write, FIFO 2 | the send,
//                                           | the MIXS write of the previous frame's send; the 0x28 / 0x20 / 0x24 /
//                                           | envelope copies | E (slot frame - 5): envelope pass, state + EG monitor
// Every rule is a transcription of model/cycle-model/aica_model.cpp (stream_step, stage_key, stage_eg, aeg_clock,
// feg_clock, stage_interp, level_of, stage_send, lpf_step); comments name the case that measured it.
module aica_sgc import aica_pkg::*; (
  input  logic         clk,
  input  logic         ce,           // engine clock enable (0: frozen between samples, the CPU owns the ports)
  input  logic [8:0]   ph,           // sample phase
  input  logic [15:0]  mdec,         // MDEC_CT (the DSP's; it decrements at the end of ph 63)
  input  logic [13:0]  eg_k,         // the boot constant K of the envelope counter
  input  logic         eg_par,       // the MDEC_CT parity its clock ticks on
  input  logic         ld,           // replay parameters (aica_core): load the noise LFSR
  input  logic [16:0]  ld_lfsr,
  // CPU side (aica_bus)
  input  logic         cpu_cw,       // channel register write
  input  logic [5:0]   cpu_cw_slot,
  input  logic [4:0]   cpu_cw_reg,   // offset[6:2]
  input  logic [15:0]  cpu_cw_data,  // already masked
  input  logic         cpu_kyonex,   // a KYONEX write (latched at the next sample boundary)
  input  logic         cpu_cr,       // channel register read (window cycle or frozen): data the next clock
  input  logic [5:0]   cpu_cr_slot,
  input  logic [4:0]   cpu_cr_reg,
  output logic [15:0]  cpu_cr_data,
  input  logic [5:0]   mon_slot,     // MSLC
  input  logic         mon_afsel,
  input  logic         mon_rd,       // an EG-monitor read (clears LP): data the next clock
  output logic [15:0]  mon_eg,       // 0x2810
  output logic [15:0]  mon_ca,       // 0x2814
  input  logic         cpu_mw,       // MIXS write (bank of this sample)
  input  logic [3:0]   cpu_mw_bus,
  input  logic         cpu_mw_hi,    // +4: bits 19:4, else bits 3:0
  input  logic [15:0]  cpu_mw_data,
  input  logic [3:0]   cpu_mr_bus,
  output logic [19:0]  cpu_mr_data,  // MIXS of this sample's bank
  // DSP side
  input  logic [3:0]   dsp_mixs_bus,
  output logic [19:0]  dsp_mixs,     // the bank the slots filled in the previous sample
  // wave RAM (one access per clock; data the clock after the request)
  output logic         ram_req,
  output logic [19:0]  ram_waddr,    // word address: returns {word(a+1), word(a)}
  input  logic [31:0]  ram_rdata,
  output logic         slot_sgc,     // the slot of the stage B frame holds this even step's wave RAM slot
  output logic [15:0]  sgc_lw,       // ... and the word its access leaves (from c2): what a DSP read there returns
  // shared multiplier
  output logic signed [24:0] mul_a,
  output logic signed [12:0] mul_b,
  input  logic signed [37:0] mul_p,
  // direct outputs of the last complete sweep (latched at ph 56, after slot 63's send)
  output logic signed [31:0] dsum_l,
  output logic signed [31:0] dsum_r,
  // simulation flags: a stepping case outside the per-frame hardware (see the stepping block)
  output logic         warn_step
);

  // =====================================================================================================
  // storage
  // =====================================================================================================
  wire [5:0] frame = ph[8:3];
  wire [2:0] c = ph[2:0];
  typedef struct packed {
    logic [9:0]  a;
    logic [1:0]  aeg_st;
    logic        off;
    logic        stop_in;
    logic [12:0] v;
    logic [1:0]  feg_st;
    logic        dir_nz;
    logic        dir_neg;
    logic        passed;
    logic [15:0] ca;
    logic [13:0] step;
    logic        in_loop;
    logic        enabled;
    logic        keyed_fetch;
    logic        ca_clr;             // a one-shot end left CA = LEA: CA reads 0 from the next sample (tests/oneshot)
    logic [9:0]  lfo_cnt;
    logic [7:0]  lfo_st;
    // the key stage (frame k) -> the envelope pass (frame k + 5): the sample's clock and what the key events changed
    logic        pk_clk;
    logic [13:0] pk_cnt;
    logic        pk_on, pk_off;
    logic [1:0]  pk_aeg, pk_feg;
    logic        pk_nz, pk_neg, pk_passed;
  } sta_t;
  localparam sta_t STA_INIT = '{a: 10'h3FF, aeg_st: 2'd3, off: 1'b1, lfo_cnt: 10'd1020, default: '0};

  typedef struct packed {               // stage B: the decoder
    logic signed [15:0] s0;
    logic signed [15:0] s1;
    logic [14:0]        quant;
  } stb_t;
  typedef struct packed {               // the level stage: the filter
    logic signed [24:0] low;
    logic signed [24:0] band;
  } stf_t;

  // channel registers: 64 slots x 8 rows x 4 registers (row r = offsets 16r .. 16r+12; rows 5-7 unused)
  logic [8:0]  cr_raddr;
  logic [63:0] cr_rdata;
  aica_ram #(.W(64), .D(512), .LANES(4)) u_creg (
    .clk, .re(1'b1), .raddr(cr_raddr), .rdata(cr_rdata),
    .we(cpu_cw && cpu_cw_reg < 5'd18), .waddr({cpu_cw_slot, cpu_cw_reg[4:2]}), .wlane(4'b1 << cpu_cw_reg[1:0]),
    .wdata({4{cpu_cw_data}}));

  logic [5:0] sa_raddr; sta_t sa_rdata; logic sa_we; logic [5:0] sa_waddr; sta_t sa_wdata;
  aica_ram #(.W($bits(sta_t)), .D(64), .INIT(STA_INIT)) u_sta (
    .clk, .re(ce), .raddr(sa_raddr), .rdata(sa_rdata), .we(sa_we), .waddr(sa_waddr), .wlane(1'b1), .wdata(sa_wdata));

  logic [5:0] sb_raddr; stb_t sb_rdata; logic sb_we; logic [5:0] sb_waddr; stb_t sb_wdata;
  aica_ram #(.W($bits(stb_t)), .D(64)) u_stb (
    .clk, .re(1'b1), .raddr(sb_raddr), .rdata(sb_rdata), .we(sb_we), .waddr(sb_waddr), .wlane(1'b1), .wdata(sb_wdata));
  logic [5:0] sf_raddr; stf_t sf_rdata; logic sf_we; logic [5:0] sf_waddr; stf_t sf_wdata;
  aica_ram #(.W($bits(stf_t)), .D(64)) u_stf (
    .clk, .re(1'b1), .raddr(sf_raddr), .rdata(sf_rdata), .we(sf_we), .waddr(sf_waddr), .wlane(1'b1), .wdata(sf_wdata));
  // copies of 0x28 and 0x20 / 0x24 for the level and send stages (their own read ports), read at c7 of the frame
  // before the stage's: a write whose X0 is at c6 there is seen, one at c2 of the stage's frame is not (tests/sub_frame)
  logic [15:0] r28c_q; logic [31:0] r2024c_q;
  aica_ram #(.W(16), .D(64)) u_r28c (
    .clk, .re(ce && c == 3'd7), .raddr(frame - 6'd3), .rdata(r28c_q),
    .we(cpu_cw && cpu_cw_reg == 5'd10), .waddr(cpu_cw_slot), .wlane(1'b1), .wdata(cpu_cw_data));
  aica_ram #(.W(32), .D(64), .LANES(2)) u_r2024c (
    .clk, .re(ce && c == 3'd7), .raddr(frame - 6'd6), .rdata(r2024c_q),
    .we(cpu_cw && (cpu_cw_reg == 5'd8 || cpu_cw_reg == 5'd9)), .waddr(cpu_cw_slot), .wlane(cpu_cw_reg == 5'd8 ? 2'b01 : 2'b10),
    .wdata({2{cpu_cw_data}}));

  // copy of the envelope registers 0x10 0x14 0x18 0x30 0x34 0x38 0x3C 0x40 0x44 for the envelope pass (frame k + 5),
  // read at c7 of the frame before (tests/sub_env: an RR write acts in this sample's pass when its X0 is before frame
  // k + 5)
  logic [143:0] ec_q;
  logic [8:0]   ec_lane;
  always_comb case (cpu_cw_reg)
    5'd4: ec_lane = 9'd1 << 0;   5'd5: ec_lane = 9'd1 << 1;   5'd6: ec_lane = 9'd1 << 2;
    5'd12: ec_lane = 9'd1 << 3;  5'd13: ec_lane = 9'd1 << 4;  5'd14: ec_lane = 9'd1 << 5;  5'd15: ec_lane = 9'd1 << 6;
    5'd16: ec_lane = 9'd1 << 7;  5'd17: ec_lane = 9'd1 << 8;
    default: ec_lane = 9'd0;
  endcase
  aica_ram #(.W(144), .D(64), .LANES(9)) u_regec (
    .clk, .re(ce && c == 3'd7), .raddr(frame - 6'd4), .rdata(ec_q),
    .we(cpu_cw && ec_lane != 9'd0), .waddr(cpu_cw_slot), .wlane(ec_lane), .wdata({9{cpu_cw_data}}));

  // monitor copies (the CPU's EG / CA monitors read the selected slot at any time): CA from the key stage, the
  // envelopes from the envelope pass
  logic [27:0] mon_eg_rdata, mon_eg_wdata; logic mon_eg_we; logic [5:0] mon_eg_waddr;
  aica_ram #(.W(28), .D(64), .INIT({10'h3FF, 2'd3, 1'b1, 13'd0, 2'd0})) u_mon_eg (
    .clk, .re(1'b1), .raddr(mon_slot), .rdata(mon_eg_rdata), .we(mon_eg_we), .waddr(mon_eg_waddr), .wlane(1'b1),
    .wdata(mon_eg_wdata));
  logic [15:0] mon_ca_rdata, mon_ca_wdata; logic mon_ca_we; logic [5:0] mon_ca_waddr;
  aica_ram #(.W(16), .D(64)) u_mon_ca (
    .clk, .re(1'b1), .raddr(mon_slot), .rdata(mon_ca_rdata), .we(mon_ca_we), .waddr(mon_ca_waddr), .wlane(1'b1),
    .wdata(mon_ca_wdata));

  logic [63:0] looped;               // monitor LP bits
  logic [63:0] lfo_reload;           // 0x1C written: counter reload (and LFORE) before the slot's next LFO use
  logic        kx_pend, kx_cur;      // KYONEX: written / latched at this sample's boundary
  logic [16:0] lfsr;                 // noise generator, one step per slot (tests/sgc_formats)
  wire  [16:0] lfsr_alfo = lfsr_fwd(lfsr, 2);    // the ALFO noise byte (at the stage B slot's FIFO 1 write)
  wire  [16:0] lfsr_plfo = lfsr_back(lfsr, 67);  // the PLFO noise byte (at the stage A slot's pitch LFO multiply)
  logic        eg_clk_q;
  logic [13:0] eg_cnt_q;
  logic        sbank;                // the MIXS bank the slots fill in this sample
  logic [19:0] mixs [2][16];
  logic [15:0] mixs_w [2];           // bus written this sample (the first writer overwrites: tests/mixs_write)
  logic        dsp_bank;             // the bank the DSP reads (and the CPU accesses)
  // The send of slot k (frame k + 7) reaches its bus at c7 of frame k + 8, so the writes of a sweep fill exactly the
  // DSP's sample window (slot 0 at ph 71 .. slot 63 at ph 63 of the next sample): the bank the DSP reads is never
  // written while it reads it, and the CPU (which reads the DSP's bank) never sees a partial sum -- the console's
  // readout of a bus written by channel 40, ~1 sample apart at every phase, shows its whole value 16/16 (wren7
  // tests/hw/collide2).  One entry: written at c7 of the send's frame, drained at c7 of the next.
  logic        mq_v;
  logic        mq_bank;
  logic [3:0]  mq_isel;
  logic [19:0] mq_val;
  // the output pipeline (model/cycle-model/aica_model.cpp "output pipeline", console tests/sub_frame): stage B (frame
  // k + 1) -> FIFO 1 -> the level stage (filter, level; frame k + 4) -> FIFO 2 -> the send stage (frame k + 7).  Entries
  // indexed by frame[1:0], written at c7 of frame f, read during frame f + 3.
  typedef struct packed {
    logic               valid;
    logic [5:0]         slot;
    logic               bank;
    logic signed [19:0] s16;
    logic [9:0]         a;
    logic [12:0]        v;
    logic [7:0]         alfo_w, lfsr_b;
    logic [2:0]         alfos;
    logic [1:0]         alfows;
  } f1_t;
  typedef struct packed {
    logic               valid;
    logic [5:0]         slot;
    logic               bank;
    logic signed [19:0] v16;
  } f2_t;
  f1_t fifo1 [4];
  f2_t fifo2 [4];


  // =====================================================================================================
  // stage A
  // =====================================================================================================
  sta_t        s0q;                 // slot state read at the frame start
  logic [15:0] r00, r04, r08, r0c, r10, r14, r18, r1c, r20, r24, r28, r2c, r40, r44;
  logic        reload_q;
  logic signed [37:0] plfo_p;

  // register / state read addresses: engine at c0 c1 c3 c4 c5 (rows 1 0 4 2 3), the CPU at c2 c6 c7 or when frozen
  always_comb begin
    cr_raddr = {frame, 3'd0};
    if (!ce || c == 3'd2 || c == 3'd6 || c == 3'd7) cr_raddr = {cpu_cr_slot, cpu_cr_reg[4:2]};
    else case (c)
      3'd0: cr_raddr = {frame, 3'd1};
      3'd1: cr_raddr = {frame, 3'd0};
      3'd3: cr_raddr = {frame, 3'd4};
      3'd4: cr_raddr = {frame, 3'd2};
      3'd5: cr_raddr = {frame, 3'd3};
      default: ;
    endcase
    // issued at c7 for the next frame's stage A; before, the envelope pass's slot (frame - 5: its data at c7)
    sa_raddr = (c == 3'd7) ? frame + 6'd1 : frame - 6'd5;
    sb_raddr = frame;                 // issued at c7: stage B of this frame's slot runs in the next frame
  end
  logic [4:0] cpu_cr_reg_q;
  always_ff @(posedge clk) cpu_cr_reg_q <= cpu_cr_reg;
  assign cpu_cr_data = cr_rdata[cpu_cr_reg_q[1:0]*16 +: 16];

  // ---- c1: pitch LFO multiply operands ----------------------------------------------------------------------
  logic [9:0] lfo_cnt_eff; logic [7:0] lfo_st_eff;
  always_comb begin
    lfo_cnt_eff = s0q.lfo_cnt; lfo_st_eff = s0q.lfo_st;
    if (reload_q && r1c[15]) lfo_st_eff = 8'd0;       // a 0x1C write setting LFORE: state 0 from this sample's use
  end
  // at c1 r18 / r1c are the RAM outputs (row 1 read at c0): use them combinationally
  wire [15:0] r18_c2 = cr_rdata[47:32], r1c_c2 = cr_rdata[63:48];
  logic [7:0] lfo_st_c2;
  logic signed [7:0] plfo_w;
  logic signed [8:0] plfo_mod;
  always_comb begin
    lfo_st_c2 = s0q.lfo_st;
    if (reload_q && r1c_c2[15]) lfo_st_c2 = 8'd0;
    plfo_w = (r1c_c2[9:8] == 2'd3) ? $signed(lfsr_plfo[7:0] ^ 8'h80) : plfo_wave(r1c_c2[9:8], lfo_st_c2);
    plfo_mod = 9'($signed({plfo_w[7:1], 1'b0}) >>> (7 - int'(r1c_c2[7:5])));
  end

  // ---- c3: phase, loop, fetch ---------------------------------------------------------------------------------
  typedef struct packed {
    sta_t        s;
    logic        looped_set;
    logic        en_out;
    logic [2:0]  dkind;
    logic [19:0] w1, w2;             // fetch words (region 1 / region 2)
    logic        need1, need2;
    // ADPCM: up to 8 steps, each a nibble of {word w2 .. w2+1, word w1 .. w1+1} (index 0..15), decoded 3 per clock
    // in stage B; rstm: the decoder resets before that step (a PCMS 2 loop wrap, the key-on sample)
    logic [3:0]  nsteps;
    logic [7:0][3:0] nidx;
    logic [7:0]  rstm;
    logic [3:0]  nsx;                // the s1 nibble (CA + 1 of the last step)
    logic        boff;
    logic [7:0]  alfo_w;
    logic        warn;
    logic        claim;              // this sample's fetch holds the slot's wave RAM step (aica_bus)
    logic [19:0] lw;                 // the word the fetch leaves on the bus: the one holding sample CA + 1
  } stp_t;
  stp_t stp, stp_q;

  function automatic logic [20:0] sa_addr(input logic [15:0] r0, input logic [15:0] r4);
    logic [22:0] a;
    a = {r0[6:0], r4};
    if (r0[8:7] == 2'd0) a[0] = 1'b0;
    return a[20:0];
  endfunction

  always_comb begin
    sta_t s;
    logic [2:0]  fmt;
    logic        fetch, lpctl, lpslnk, adp;
    logic [15:0] lsa, lea, dl;
    logic [20:0] sa;
    logic [3:0]  oct;
    logic signed [31:0] m, incr;
    logic [23:0] t;
    logic [8:0]  ip;
    logic [15:0] cas [9];
    logic        wr [9];
    int          sh;
    logic [15:0] cat; logic armed;
    logic [16:0] e, j1, js;
    logic armed_any, wrap;
    logic [20:0] b1, b2, bs, bn;
    logic [19:0] wo, wn, wl;          // ADPCM claim: 16-bit word of the old CA, the new CA, the look-ahead nibble
    logic        kf;                  // the key-on sample: the first fetch (CA 0, no advance)
    int          jw, nw;              // ADPCM: the step of a loop wrap, the number of wraps
    logic [20:0] bb; logic [4:0] ix, mx;
    logic        done;                // a one-shot end: the steps after it do not run
    int          ipr;                 // the steps that ran
    logic [15:0] ca_old;
    stp = '0;
    oct = '0; m = '0; incr = '0; t = '0; sh = 0; cat = '0; armed = 1'b0; e = '0; j1 = '0; js = '0;
    armed_any = 1'b0; wrap = 1'b0; b1 = '0; b2 = '0; bs = '0; bn = '0; wo = '0; wn = '0; wl = '0;
    jw = 0; nw = 0; bb = '0; ix = '0; mx = '0; done = 1'b0; ipr = 0;
    s = s0q;
    s.lfo_cnt = lfo_cnt_eff; s.lfo_st = lfo_st_eff;
    if (s.ca_clr) begin s.ca_clr = 1'b0; s.ca = 16'd0; end   // the sample after a one-shot end (it left CA = LEA)
    ca_old = s.ca;
    fmt = r00[10] ? 3'd4 : {1'b0, r00[8:7]};
    adp = (fmt == 3'd2 || fmt == 3'd3);
    lpctl = r00[9]; lpslnk = r14[14];
    lsa = r08; lea = r0c;
    sa = sa_addr(r00, r04);
    fetch = s.enabled && !s.keyed_fetch;             // the key-on sample outputs CA 0 without advancing
    kf = s.enabled && s.keyed_fetch;
    s.keyed_fetch = 1'b0;
    stp.dkind = 3'd0;
    ip = 9'd0;
    for (int j = 0; j <= 8; j++) begin cas[j] = s.ca; wr[j] = 1'b0; end
    if (fetch) begin
      // pitch: (1024 + FNS [+ PLFO]) << (OCT + 4), 14 fraction bits (tests/sgc_pitch, sgc_lfo)
      oct = r18[14:11];
      m = 32'sd1024 + 32'(r18[9:0]);
      if (r1c[7:5] != 3'd0) m = m + 32'(plfo_p >>> 10);
      sh = (oct[3] ? int'(oct) - 16 : int'(oct)) + 4;
      incr = (sh >= 0) ? (m <<< sh) : (m >>> (-sh));
      t = 24'(s.step) + 24'(incr);
      ip = t[22:14]; ipr = int'(ip);
      s.step = t[13:0];
      if (ip <= 9'd2 || (adp && ip <= 9'd8)) begin
        // step by step, as the model's loop (tests/sgc_loop): arming, loop end, LPSLNK, one-shot end (ADPCM: up to
        // 8 steps, OCT +2; the console stops an ADPCM channel at OCT +3, wren7 tests/hw/sgcadp)
        for (int j = 1; j <= 8; j++) if (j <= int'(ip) && !done) begin
          s.ca = s.ca + 16'd1;
          cat = (fmt == 3'd3) ? {s.ca[15:2], 2'b00} : s.ca;
          if (lpslnk && s.aeg_st == EG_ATTACK && s.ca >= lsa) s.aeg_st = EG_DECAY1;
          armed = s.in_loop;
          if (s.ca >= lsa) s.in_loop = 1'b1;
          if (cat >= lea && armed) begin
            stp.looped_set = 1'b1;
            if (lpctl) begin
              s.ca = s.ca - (lea - lsa);
              wr[j] = 1'b1;
            end else begin
              // one-shot end (tests/oneshot): only the fetch stops -- CA reads the stepped value (LEA) on this sample
              // and 0 from the next; the envelope keeps its state and keeps stepping
              s.enabled = 1'b0; s.ca_clr = 1'b1; done = 1'b1; ipr = j - 1;
            end
          end
          cas[j] = s.ca;
        end
      end else begin
        // closed form (PCM / noise at pitch > 2): at most one loop wrap per sample -- exact for every loop longer
        // than the step (tests/sgc_pitch, sgc_loop); two wraps or a 16-bit wrap inside the step raise warn_step
        e = 17'(s.ca) + 17'(ip);
        if (lpslnk && s.aeg_st == EG_ATTACK && e >= 17'(lsa)) s.aeg_st = EG_DECAY1;
        j1 = s.in_loop ? 17'd0 : ((17'(s.ca) + 17'd1 >= 17'(lsa)) ? 17'd1 : 17'(lsa) - 17'(s.ca));
        armed_any = j1 < 17'(ip);
        if (j1 <= 17'(ip)) s.in_loop = 1'b1;
        js = (17'(lea) > 17'(s.ca)) ? 17'(lea) - 17'(s.ca) : 17'd0;
        if (js < j1 + 17'd1) js = j1 + 17'd1;
        wrap = armed_any && js <= 17'(ip);
        s.ca = e[15:0];
        if (wrap) begin
          stp.looped_set = 1'b1;
          if (lpctl) begin
            s.ca = e[15:0] - (lea - lsa);
            if (17'(e) - 17'(lea - lsa) >= 17'(lea) && lea > lsa) stp.warn = 1'b1;
          end else begin
            s.ca = ca_old + js[15:0];                 // one-shot end at step js: the stepped CA
            s.enabled = 1'b0; s.ca_clr = 1'b1;
          end
        end
        if (e[16]) stp.warn = 1'b1;
        if (adp) stp.warn = 1'b1;                   // ADPCM beyond eight nibbles per sample (OCT >= 3)
      end
      if (ipr != 0) begin                     // the steps that ran (a one-shot end stops them)
        stp.dkind = (fmt == 3'd4) ? 3'd4 : (fmt == 3'd0) ? 3'd1 : (fmt == 3'd1) ? 3'd2 : 3'd3;
        stp.need1 = (fmt != 3'd4);
        if (fmt == 3'd0) stp.w1 = 20'((sa + {4'd0, s.ca, 1'b0}) >> 1);
        else if (fmt == 3'd1) begin
          b1 = sa + {5'd0, s.ca}; stp.w1 = b1[20:1]; stp.boff = b1[0];
        end else if (adp) begin
          // nibble n: byte sa + (n >> 1), shift 4 * (n & 1).  Region 1 starts at step 1's nibble (w1, 16 nibbles with
          // w2 = w1 + 2); a loop wrap at a later step starts region 2 there (w2, indices 8..15).  One wrap per sample;
          // a region longer than its words raises warn_step (tests/sgc_formats: ADPCM loops at pitch 1.0 / 1.37)
          jw = 0; nw = 0; mx = '0;
          for (int j = 2; j <= 8; j++) if (j <= ipr && wr[j]) begin nw++; if (jw == 0) jw = j; end
          if (nw > 1) stp.warn = 1'b1;
          stp.nsteps = 4'(ipr);
          b1 = sa + {6'd0, cas[1][15:1]};
          stp.w1 = b1[20:1];
          if (jw != 0) begin
            b2 = sa + {6'd0, cas[jw][15:1]};
            stp.w2 = b2[20:1]; stp.need2 = 1'b1;
          end else stp.w2 = stp.w1 + 20'd2;
          for (int j = 1; j <= 8; j++) if (j <= ipr) begin
            bb = sa + {6'd0, cas[j][15:1]};
            if (jw != 0 && j >= jw) ix = 5'd8 + 5'(((bb - {stp.w2, 1'b0}) << 1) + {20'd0, cas[j][0]});
            else ix = 5'(((bb - {stp.w1, 1'b0}) << 1) + {20'd0, cas[j][0]});
            stp.nidx[j - 1] = ix[3:0];
            if (ix > mx) mx = ix;
            stp.rstm[j - 1] = wr[j] && fmt == 3'd2 && lpctl;
            if (jw != 0 && j < jw && ix > 5'd7) stp.warn = 1'b1;
          end
          // the s1 nibble: CA + 1 of the last step (no wrap for it)
          bs = sa + 21'(({1'b0, s.ca} + 17'd1) >> 1);
          if (jw != 0) ix = 5'd8 + 5'(((bs - {stp.w2, 1'b0}) << 1) + {20'd0, ~s.ca[0]});
          else ix = 5'(((bs - {stp.w1, 1'b0}) << 1) + {20'd0, ~s.ca[0]});
          stp.nsx = ix[3:0];
          if (ix > mx) mx = ix;
          if (mx > 5'd15) stp.warn = 1'b1;
          if (jw == 0 && mx > 5'd7) stp.need2 = 1'b1;
        end
      end
    end else if (kf) begin
      // the key-on sample (tests/dsp_coll kon runs: its fetch is the first, in the sample that outputs CA 0): the
      // words at CA 0, decoded as one step from a reset decoder (the model's decode_initial)
      stp.dkind = (fmt == 3'd4) ? 3'd4 : (fmt == 3'd0) ? 3'd1 : (fmt == 3'd1) ? 3'd2 : 3'd3;
      stp.need1 = (fmt != 3'd4);
      stp.w1 = sa[20:1]; stp.boff = sa[0];
      stp.nsteps = 4'd1; stp.rstm = 8'd1; stp.w2 = sa[20:1] + 20'd2;
      stp.nidx[0] = {2'b00, sa[0], 1'b0}; stp.nsx = {2'b00, sa[0], 1'b1};   // nibbles 0 and 1 of byte SA
    end
    // the step's wave RAM slot (wren7 tests/hw/sgc, sgcadp): PCM16 / PCM8 fetch once per sample at every pitch while
    // the slot plays; noise never.  ADPCM holds one 16-bit word: it fetches when the new CA's word differs from the
    // old one's, or when the look-ahead nibble (CA + 1) lies in the next word -- one fetch per sample at most.  This
    // gives the console's shares exactly (OCT -2 5/16, -1 3/8, 0 and +1 1/2, +2 1); at OCT >= 3 the console fetches
    // nothing.
    wo = 20'((sa + 21'(ca_old >> 1)) >> 1);
    wn = 20'((sa + 21'(s.ca >> 1)) >> 1);
    // the word the fetch leaves (tests/dsp_coll: a DSP read in the slot returns it): the one holding sample CA + 1
    if (fmt == 3'd0) stp.lw = 20'((sa + {3'd0, 17'({1'b0, s.ca} + 17'd1), 1'b0}) >> 1);
    else if (fmt == 3'd1) stp.lw = 20'((sa + 21'({1'b0, s.ca} + 17'd1)) >> 1);
    else stp.lw = 20'((sa + 21'(({1'b0, s.ca} + 17'd1) >> 1)) >> 1);
    wl = 20'((sa + 21'((17'(s.ca) + 17'd1) >> 1)) >> 1);
    if (fmt <= 3'd1) stp.claim = s0q.enabled;
    else if (adp) stp.claim = !(r18[14:11] >= 4'd3 && r18[14:11] <= 4'd7) && (kf || (fetch && (wn != wo || wl != wn)));
    else stp.claim = 1'b0;
    stp.en_out = s.enabled;
    // LFO (slot_output: advances after this sample's output, only while the slot plays; LFORE holds it at 0)
    stp.alfo_w = alfo_wave(r1c[4:3], s.lfo_st);
    // the LFO runs every sample, playing or not; no write reloads its counter; a counter above a shortened period is cut
    // to it; LFORE holds the state at 0, not the counter (model/cycle-model stage_a, tests/sgc_lfo hw9)
    if (s.lfo_cnt > lfo_period(r1c[14:10])) s.lfo_cnt = lfo_period(r1c[14:10]);
    s.lfo_cnt = s.lfo_cnt - 10'd1;
    if (s.lfo_cnt == 10'd0) begin
      if (!r1c[15]) s.lfo_st = s.lfo_st + 8'd1;
      s.lfo_cnt = lfo_period(r1c[14:10]);
    end
    if (r1c[15]) s.lfo_st = 8'd0;
    // a stop armed by the previous sample's envelope pass takes effect after this sample's output (tests/slot_tail,
    // ca_stop): the fetch stops, the monitor reads 0x1FFF, CA reads 0
    if (s.stop_in) begin s.stop_in = 1'b0; s.enabled = 1'b0; s.off = 1'b1; s.ca = 16'd0; end
    stp.s = s;
  end

  // ---- c6: key events (the model's stage_key) -------------------------------------------------------------------
  // KYONEX latched at the sample boundary, KYONB as sampled in stage A (tests/sub_sched exp 0); the key event acts on
  // the state after this sample's fetch.  What the envelope pass of frame k + 5 needs is recorded in the pk_ fields.
  sta_t key_s; logic eg_keyon;
  always_comb begin
    sta_t s;
    s = stp_q.s;
    s.pk_on = 1'b0; s.pk_off = 1'b0; s.pk_aeg = s.aeg_st; s.pk_feg = s.feg_st;
    s.pk_nz = s.dir_nz; s.pk_neg = s.dir_neg; s.pk_passed = s.passed;
    s.pk_clk = eg_clk_q; s.pk_cnt = eg_cnt_q;
    eg_keyon = 1'b0;
    if (kx_cur) begin
      // FLV0 = r2c; FLV1 / FLV4 = row 3, whose read (issued at c5) is on the RAM output exactly at c6
      if (!r00[14] && s.aeg_st != EG_RELEASE) begin              // key_off: a clock on this sample steps the OLD segment
        s.aeg_st = EG_RELEASE; s.feg_st = EG_RELEASE;
        s.dir_nz = 1'b1; s.dir_neg = s.v >= cr_rdata[60:48]; s.passed = 1'b0;
        s.pk_off = 1'b1;
      end
      if (r00[14] && s.aeg_st == EG_RELEASE) begin               // key_on (tests/sgc_aeg: a = 0x280); not from a
        s.enabled = 1'b1; s.aeg_st = EG_ATTACK; s.a = 10'h280;   // one-shot end, whose envelope keeps running
        s.off = 1'b0; s.stop_in = 1'b0; s.ca_clr = 1'b0;         // (tests/oneshot)
        if (eff_rate(r10[4:0], r14, r18) >= 6'd63) s.a = 10'd0;  // R 63: instant (tests/sgc_krs)
        s.feg_st = EG_ATTACK; s.v = r2c[12:0];
        s.dir_nz = 1'b1; s.dir_neg = r2c[12:0] >= cr_rdata[12:0]; s.passed = 1'b0;
        s.ca = 16'd0; s.step = 14'd0; s.in_loop = 1'b0;
        s.keyed_fetch = 1'b1; s.pk_on = 1'b1; eg_keyon = 1'b1;
      end
    end
    key_s = s;
  end

  // ---- c7: the envelope pass of slot frame - 5 (the model's stage_eg: aeg_clock / feg_clock) --------------------
  // The envelopes of that slot's NEXT sample, with the clock of its own sample (pk_clk / pk_cnt, recorded by its key
  // stage) and the registers of the copy ec_q (read at c7 of the frame before: tests/sub_env); state read from u_sta
  // (issued at c6), written back with the EG monitor at c7.
  wire [15:0] e10 = ec_q[15:0], e14 = ec_q[31:16], e18 = ec_q[47:32], e40 = ec_q[127:112], e44 = ec_q[143:128];
  sta_t eg_s;
  always_comb begin
    sta_t s;
    logic keyed, keyed_off;
    logic [1:0] aeg_prev, feg_prev, seg;
    logic [12:0] flv [5];
    logic [12:0] target;
    logic [4:0] rate;
    logic [3:0] inc;
    logic signed [15:0] a;
    logic dnz, dneg, cmp, was_d1;
    int nv;
    s = sa_rdata;
    flv[0] = '0; flv[1] = ec_q[60:48]; flv[2] = ec_q[76:64]; flv[3] = ec_q[92:80]; flv[4] = ec_q[108:96];
    keyed = s.pk_on; keyed_off = s.pk_off; aeg_prev = s.pk_aeg; feg_prev = s.pk_feg;
    if (s.pk_clk) begin
      // ---- amplitude envelope (aeg_clock) ----
      if (!(s.off && s.a == 10'h3FF)) begin
        if (keyed) begin
          if (s.aeg_st == EG_ATTACK && s.a == 10'd0 && !e14[14]) s.aeg_st = EG_DECAY1;   // tests/eg_kprobe
        end else if (!(keyed_off && aeg_prev == EG_ATTACK)) begin                    // tests/aeg_koff koff_att
          was_d1 = s.aeg_st == EG_DECAY1;
          case (keyed_off ? aeg_prev : s.aeg_st)
            EG_ATTACK: rate = e10[4:0];
            EG_DECAY1: rate = e10[10:6];
            EG_DECAY2: rate = e10[15:11];
            default:   rate = e14[4:0];
          endcase
          inc = eg_increment(eff_rate(rate, e14, e18), s.pk_cnt);
          if (inc == 4'd0) begin
            if (was_d1 && s.a[9:5] == e14[9:5]) s.aeg_st = EG_DECAY2;
          end else begin
            a = 16'(s.a);
            if (s.aeg_st == EG_ATTACK) begin
              a = a + 16'((32'(~a) * 32'(inc)) >>> 4);
              if (a <= 0) begin a = 0; if (!e14[14]) s.aeg_st = EG_DECAY1; end
            end else begin
              a = a + 16'(inc);
              // tests/slot_tail; also after a one-shot end, whose slot is not off (tests/oneshot A)
              if (a >= 16'sh3C0 && !s.off && !s.stop_in) s.stop_in = 1'b1;
              if (a > 16'sh3FF) a = 16'sh3FF;
            end
            s.a = a[9:0];
            if (was_d1 && s.a[9:5] == e14[9:5]) s.aeg_st = EG_DECAY2;          // equality (tests/sgc_loop lo_2)
          end
        end
      end
      // ---- filter envelope (feg_clock) ----
      if (!keyed) begin
        if (s.passed && s.feg_st < EG_DECAY2) begin
          s.feg_st = s.feg_st + 2'd1;
          s.dir_nz = 1'b1; s.dir_neg = s.v >= flv[{1'b0, s.feg_st} + 3'd1];
          s.passed = 1'b0;
        end
        seg = keyed_off ? feg_prev : s.feg_st;
        dnz = keyed_off ? s.pk_nz : s.dir_nz;
        dneg = keyed_off ? s.pk_neg : s.dir_neg;
        if (keyed_off && s.pk_passed && seg < EG_DECAY2) begin              // tests/feg_koffpass
          seg = seg + 2'd1;
          dnz = 1'b1; dneg = s.v >= flv[{1'b0, seg} + 3'd1];
        end
        case (seg)
          EG_ATTACK: rate = e40[12:8];
          EG_DECAY1: rate = e40[4:0];
          EG_DECAY2: rate = e44[12:8];
          default:   rate = e44[4:0];
        endcase
        target = flv[{1'b0, s.feg_st} + 3'd1];
        inc = eg_increment(eff_rate(rate, e14, e18), s.pk_cnt);
        if (inc != 4'd0 && !s.passed) begin
          cmp = s.v >= target;
          nv = int'(s.v) + (dnz ? (dneg ? -int'(inc) : int'(inc)) : 0);
          if (nv < 0) nv = 0;
          if (nv > 8191) nv = 8191;
          if (s.feg_st >= EG_DECAY2) begin
            if ((13'(nv) >= target) == cmp) s.v = 13'(nv);                 // hold short of the target
          end else begin
            s.v = 13'(nv);
            if ((13'(nv) >= target) != cmp) s.passed = 1'b1;
          end
        end
      end
    end
    eg_s = s;
  end

  // ---- stage A sequencing --------------------------------------------------------------------------------------
  typedef struct packed {
    logic        valid;
    logic [5:0]  slot;
    logic        bank;
    logic        en_out;
    logic [5:0]  frac6;
    logic [2:0]  dkind;
    logic [3:0]  nsteps;
    logic [7:0][3:0] nidx;
    logic [7:0]  rstm;
    logic [3:0]  nsx;
    logic        boff;
    logic        ssctl;
    logic [2:0]  alfos;
    logic [1:0]  alfows;
    logic [7:0]  alfo_w;
    logic [9:0]  a;
    logic [12:0] v;
    logic        claim;
    logic [19:0] lw;
  } bq_t;
  bq_t bq;                              // stage A -> stage B, latched at the end of stage A's frame
  logic [31:0] rd1, rd2;                // fetched words
  logic [31:0] b_rd1, b_rd2;
  logic [15:0] lw_q;                    // the word stage B's slot access read (the fetch's last word)
  logic        a_valid;                 // stage A has run at least once (the first stage B is real)

  always_comb begin
    ram_req = 1'b0; ram_waddr = stp_q.w1;
    if (ce) case (c)
      3'd4: begin ram_req = stp_q.need1; ram_waddr = stp_q.w1; end
      3'd5: begin ram_req = stp_q.need2; ram_waddr = stp_q.w2; end
      3'd0: begin ram_req = bq.valid && bq.claim; ram_waddr = bq.lw; end   // the slot's own step (DSP 2K - 14): its last word
      default: ;
    endcase
  end

  always_ff @(posedge clk) if (ce) begin
    case (c)
      3'd0: begin
        s0q <= a_valid ? sa_rdata : STA_INIT;          // the very first frame's read (issued in frame 63) never ran
        reload_q <= lfo_reload[frame];
      end
      3'd1: begin
        r10 <= cr_rdata[15:0]; r14 <= cr_rdata[31:16]; r18 <= cr_rdata[47:32]; r1c <= cr_rdata[63:48];
        plfo_p <= mul_p;
        lw_q <= ram_rdata[15:0];                                  // stage B: the word of the slot's own step
      end
      3'd2: begin r00 <= cr_rdata[15:0]; r04 <= cr_rdata[31:16]; r08 <= cr_rdata[47:32]; r0c <= cr_rdata[63:48]; end
      3'd3: stp_q <= stp;
      3'd4: begin r40 <= cr_rdata[15:0]; r44 <= cr_rdata[31:16]; end
      3'd5: begin r20 <= cr_rdata[15:0]; r24 <= cr_rdata[31:16]; r28 <= cr_rdata[47:32]; r2c <= cr_rdata[63:48];
                  rd1 <= ram_rdata; end
      3'd6: rd2 <= ram_rdata;
      3'd7: begin
        // hand over to stage B
        bq.valid <= 1'b1; bq.slot <= frame; bq.bank <= sbank;
        bq.en_out <= stp_q.en_out; bq.frac6 <= stp_q.s.step[13:8];
        bq.dkind <= stp_q.dkind; bq.nsteps <= stp_q.nsteps; bq.nidx <= stp_q.nidx; bq.rstm <= stp_q.rstm;
        bq.nsx <= stp_q.nsx;
        bq.boff <= stp_q.boff;
        bq.ssctl <= r00[10];
        bq.alfos <= r1c[2:0]; bq.alfows <= r1c[4:3]; bq.alfo_w <= stp_q.alfo_w;
        bq.a <= stp_q.s.a; bq.v <= stp_q.s.v;
        bq.claim <= stp_q.claim; bq.lw <= stp_q.lw;
        b_rd1 <= rd1; b_rd2 <= rd2;
      end
      default: ;
    endcase
  end

  // state writes: c6 the key stage (slot `frame`, with the CA monitor), c7 the envelope pass (slot frame - 5, with the
  // EG monitor)
  always_comb begin
    sa_we = ce && (c == 3'd6 || c == 3'd7) && a_valid_frame;
    sa_waddr = (c == 3'd7) ? frame - 6'd5 : frame;
    sa_wdata = (c == 3'd7) ? eg_s : key_s;
    mon_ca_we = ce && c == 3'd6;
    mon_ca_waddr = frame;
    mon_ca_wdata = key_s.ca;
    mon_eg_we = ce && c == 3'd7;
    mon_eg_waddr = frame - 6'd5;
    mon_eg_wdata = {eg_s.a, eg_s.aeg_st, eg_s.off, eg_s.v, eg_s.feg_st};
  end
  wire a_valid_frame = 1'b1;
  // slot bq.slot's wave RAM step is c0-c3 of its stage B frame (DSP step 2 K - 14, aica_pkg)
  assign slot_sgc = bq.valid && bq.claim;
  assign sgc_lw = lw_q;

  // =====================================================================================================
  // stage B (slot bq.slot, frame k + 1): decode, noise, interpolation -> FIFO 1
  // =====================================================================================================
  stb_t        sbq;                              // state B read at c7 of stage A's frame
  logic signed [15:0] ds0, ds1;                  // decoded samples of this sample (PCM; ADPCM: see below)
  logic signed [19:0] s16;

  // decode (combinational from the fetched words and the state; used from c0)
  function automatic logic [3:0] nib64(input logic [63:0] w, input logic [3:0] i);
    return w[i*4 +: 4];
  endfunction
  function automatic logic signed [15:0] byte16(input logic [31:0] w, input logic [1:0] i);
    return {w[i*8 +: 8], 8'd0};
  endfunction
  always_comb begin
    ds0 = sbq.s0; ds1 = sbq.s1;
    case (bq.dkind)
      3'd1: begin ds0 = b_rd1[15:0]; ds1 = b_rd1[31:16]; end
      3'd2: begin ds0 = byte16(b_rd1, {1'b0, bq.boff}); ds1 = byte16(b_rd1, {1'b0, bq.boff} + 2'd1); end
      3'd4: begin ds0 = 16'sd0; ds1 = 16'sd0; end
      default: ;
    endcase
  end
  // ADPCM (the model's decode_sample loop): up to 8 steps, three per clock at c0 / c1 / c2 from the state of the
  // previous sample (quant, s0), then the s1 nibble from the state after the last step (its quant is not kept)
  logic signed [15:0] ap, ap2, ap3, as1;
  logic [14:0]        aq, aq2, aq3;
  always_comb begin
    logic signed [15:0] p; logic [14:0] q; logic [30:0] r; int k0;
    p = (c == 3'd0) ? sbq.s0 : ap; q = (c == 3'd0) ? sbq.quant : aq;
    k0 = (c == 3'd1) ? 3 : (c == 3'd2) ? 6 : 0;
    ap2 = p; aq2 = q; ap3 = p; aq3 = q; as1 = '0; r = '0;
    for (int k = 0; k < 3; k++) begin
      if (k0 + k < int'(bq.nsteps)) begin
        if (bq.rstm[k0 + k]) begin p = 16'sd0; q = 15'd127; end
        r = adpcm_dec(nib64({b_rd2, b_rd1}, bq.nidx[k0 + k]), p, q); p = r[15:0]; q = r[30:16];
      end
      if (k == 1) begin ap2 = p; aq2 = q; end
    end
    ap3 = p; aq3 = q;
    r = adpcm_dec(nib64({b_rd2, b_rd1}, bq.nsx), ap2, aq2); as1 = r[15:0];
  end

  logic signed [15:0] bs0, bs1;
  logic [14:0]        adq;               // ADPCM quant after this sample's steps

  // =====================================================================================================
  // the level stage (FIFO 1 entry of 3 frames ago: slot frame - 4, its frame k + 4): filter (Q, LPOFF), level (TL,
  // VOFF, AEG, ALFO) with 0x28 read at c7 of the frame before -> FIFO 2
  // the send stage (FIFO 2 entry of 3 frames ago: slot frame - 7, its frame k + 7): ISEL / IMXL / DISDL / DIPAN
  // (0x20 / 0x24 read at c7 of the frame before) -> the MIXS write at c7 of the next frame, the direct sums
  // =====================================================================================================
  wire f1_t lq = fifo1[frame[1:0] - 2'd3];         // this frame's level-stage input
  wire f2_t sq = fifo2[frame[1:0] - 2'd3];         // this frame's send-stage input
  wire [15:0] l28 = r28c_q;                        // 0x28 of slot lq.slot
  wire [15:0] s20 = r2024c_q[15:0], s24 = r2024c_q[31:16];
  stf_t        sfq;                              // filter state of slot lq.slot, read at c7 of the frame before
  logic signed [25:0] damp;
  logic signed [24:0] band_n, low_n;
  logic signed [19:0] filt;
  logic signed [19:0] v16_c;             // level result (combinational at c7)
  logic signed [31:0] snd_d, dir_c, side_c;
  wire  [12:0] fv = lq.v;
  wire  [9:0]  fk = (fv >= 13'h1FFE) ? 10'd512 : {2'b01, fv[8:1]};   // 256 + FLV[8:1], unity at 0x1FFE/F
  wire  [4:0]  fsh = 5'd24 - {1'b0, fv[12:9]};                       // 24 - FLV >> 9

  // level: attenuation TL*4 + AEG + ALFO (tests/sgc_level, sgc_lfo), saturated at 0x3FF
  logic [11:0] att; logic [6:0] att_m; logic [3:0] att_e; logic [7:0] alfo_src; logic [8:0] alfo;
  always_comb begin
    alfo_src = (lq.alfows == 2'd3) ? lq.lfsr_b : lq.alfo_w;
    alfo = (lq.alfos != 3'd0) ? 9'({alfo_src[7:1], 1'b0} >> (7 - int'(lq.alfos))) : 9'd0;
    att = {l28[15:8], 2'b00} + {2'b00, lq.a} + {3'b000, alfo};
    if (att > 12'h3FF) att = 12'h3FF;
    att_m = 7'd127 - {1'b0, att[5:0]};
    att_e = att[9:6];
  end

  // multiplier operands of the SGC cycles (c0 damping (L), c1 PLFO (A), c3 interpolation (B), c4 k*H (L), c5 k*B (L),
  // c7 level (L))
  logic signed [26:0] h27;
  logic signed [23:0] h24;
  logic signed [19:0] lvl_src;
  always_comb begin
    h27 = 27'(lq.s16 >>> 1) - 27'(sfq.low) - 27'(damp);
    h24 = (h27 > 27'sd8388607) ? 24'sd8388607 : (h27 < -27'sd8388608) ? -24'sd8388608 : 24'(h27);
    lvl_src = l28[5] ? lq.s16 : filt;
    mul_a = '0; mul_b = '0;
    case (c)
      3'd0: begin mul_a = sfq.band; mul_b = 13'(q128(l28[4:0])); end
      3'd1: begin mul_a = 25'sd1024 + 25'(r18_c2[9:0]); mul_b = 13'(plfo_mod); end
      3'd3: begin mul_a = 25'(bs1) - 25'(bs0); mul_b = 13'(bq.frac6); end
      3'd4: begin mul_a = 25'(h24); mul_b = 13'(fk); end
      3'd5: begin mul_a = band_n; mul_b = 13'(fk); end
      3'd7: begin mul_a = 25'(lvl_src); mul_b = 13'(att_m); end
      default: ;
    endcase
  end

  // c7: V16 = level (whole samples) or the fractional signal with VOFF; the send of the send stage's slot
  always_comb begin
    logic signed [37:0] lv;
    lv = mul_p >>> (7 + int'(att_e));
    if (l28[6]) v16_c = lvl_src;
    else v16_c = 20'((lv >>> 4) <<< 4);
    snd_d = send_level(32'(sq.v16), s20[7:4]);
    dir_c = send_level(32'(sq.v16 >>> 4), s24[11:8]);
    side_c = (s24[3:0] == 4'hF) ? 32'sd0 : send_level(dir_c, 4'hF - s24[3:0]);
  end

  always_ff @(posedge clk) if (ce) begin
    case (c)
      3'd0: begin
        damp <= 26'(-((-mul_p) >>> 8)) <<< 1;                 // 2 * ceil(q * B / 256)  (1/4-sample damping)
        bs0 <= ds0; bs1 <= ds1;
        ap <= ap3; aq <= aq3;
      end
      3'd1: begin
        ap <= ap3; aq <= aq3;
        if (bq.ssctl) begin                                   // noise: the slot's LFSR byte << 8, not interpolated, playing or not
          bs0 <= {lfsr[7:0], 8'd0}; bs1 <= {lfsr[7:0], 8'd0};
        end
      end
      3'd2: if (bq.dkind == 3'd3) begin bs0 <= ap2; bs1 <= as1; adq <= aq2; end
      3'd3: s16 <= (bq.en_out || bq.ssctl) ? 20'(32'(bs0) * 16 + 32'(mul_p >>> 2)) : 20'sd0;
      3'd4: band_n <= sfq.band + 25'(mul_p >>> fsh);
      3'd5: begin low_n <= low_c6; filt <= filt_c6; end
      default: ;
    endcase
    if (c == 3'd7) begin
      sbq <= sb_rdata;                                          // stage B's state for the next frame
      sfq <= sf_rdata;                                          // the level stage's filter state for the next frame
      // stage B -> FIFO 1 (the level stage of this slot runs 3 frames later)
      fifo1[frame[1:0]] <= '{valid: bq.valid, slot: bq.slot, bank: bq.bank, s16: s16, a: bq.a, v: bq.v,
                             alfo_w: bq.alfo_w, lfsr_b: lfsr_alfo[7:0], alfos: bq.alfos, alfows: bq.alfows};
      // the level stage -> FIFO 2 (the send stage of this slot runs 3 frames later)
      fifo2[frame[1:0]] <= '{valid: lq.valid, slot: lq.slot, bank: lq.bank, v16: v16_c};
    end
  end

  // c5: low += ceil(k * band' >> sh); the output is -2 low, saturated to signed 20 bits (tests/filt_id2)
  logic signed [24:0] low_c6; logic signed [31:0] fo; logic signed [19:0] filt_c6;
  always_comb begin
    low_c6 = sfq.low + 25'(-((-mul_p) >>> fsh));
    fo = -(32'(low_c6) <<< 1);
    filt_c6 = (fo > 32'sd524287) ? 20'sd524287 : (fo < -32'sd524288) ? -20'sd524288 : 20'(fo);
  end

  // state writes at c7: the decoder's (stage B slot), the filter's (level-stage slot; LPOFF freezes it)
  logic signed [31:0] dl_acc, dr_acc;
  always_comb begin
    sb_we = ce && c == 3'd7 && bq.valid;
    sb_waddr = bq.slot;
    sb_wdata.s0 = bs0;
    sb_wdata.s1 = bs1;
    sb_wdata.quant = (bq.dkind == 3'd3) ? adq : sbq.quant;
    sf_we = ce && c == 3'd7 && lq.valid && !l28[5];
    sf_waddr = lq.slot;
    sf_wdata.low = low_n;
    sf_wdata.band = band_n;
    sf_raddr = frame - 6'd3;          // issued at c7: the next frame's level-stage slot
  end

  // =====================================================================================================
  // sample boundary bookkeeping, LFSR, MIXS banks, flags
  // =====================================================================================================
  always_ff @(posedge clk) begin
    if (cpu_kyonex) kx_pend <= 1'b1;
    if (cpu_cw && cpu_cw_reg == 5'd7) lfo_reload[cpu_cw_slot] <= 1'b1;
    if (mon_rd) looped[mon_slot] <= 1'b0;
    if (ce) begin
      if (ph == 9'd0) begin
        kx_cur <= kx_pend || cpu_kyonex;
        kx_pend <= 1'b0;
        // the envelope clock / counter of the NEXT sample: MDEC_CT(n+1) - 1 = MDEC_CT(n) - 2; at ph 0 the DSP's
        // counter still holds the previous sample's value (it decrements at ph 63)
        eg_clk_q <= ((mdec - 16'd3) & 16'd1) == 16'(eg_par);
        eg_cnt_q <= 14'(eg_k - 14'((mdec - 16'd3) >> 1));
      end
      if (ph == 9'(DSP_OFFSET - 1)) mixs_w[sbank] <= 16'd0;          // this sweep's bank: its write window opens
      if (ph == 9'(SAMPLE_CLKS - 1)) sbank <= ~sbank;
      if (c == 3'd0 && bq.valid) lfsr <= {lfsr[0] ^ lfsr[5], lfsr[16:1]};
      if (ld) lfsr <= ld_lfsr;
      // the LFO reload flag is sampled (and cleared) at c0 of the slot's frame (a write at c2 / c6 waits for the next)
      if (c == 3'd0 && !(cpu_cw && cpu_cw_reg == 5'd7 && cpu_cw_slot == frame)) lfo_reload[frame] <= 1'b0;
      if (c == 3'd4 && stp_q.looped_set) looped[frame] <= 1'b1;
      if (c == 3'd6 && eg_keyon) looped[frame] <= 1'b0;
      if (c == 3'd7) begin
        a_valid <= 1'b1;
        if (mq_v) begin
          // every slot writes its ISEL bus; the first writer of the sample overwrites (tests/mixs_write)
          mixs[mq_bank][mq_isel] <= (mixs_w[mq_bank][mq_isel] ? mixs[mq_bank][mq_isel] : 20'd0) + mq_val;
          mixs_w[mq_bank][mq_isel] <= 1'b1;
        end
        mq_v <= sq.valid; mq_bank <= sq.bank; mq_isel <= s20[3:0]; mq_val <= 20'(snd_d);
        if (sq.valid) begin
          dl_acc <= ((sq.slot == 6'd0) ? 32'sd0 : dl_acc) + ((s24[4]) ? dir_c : side_c);
          dr_acc <= ((sq.slot == 6'd0) ? 32'sd0 : dr_acc) + ((s24[4]) ? side_c : dir_c);
        end
      end
      if (ph == 9'd56) begin dsum_l <= dl_acc; dsum_r <= dr_acc; end    // slot 63's send was at c7 of frame 6 (ph 55)
    end
    // CPU MIXS write: the DSP's bank (at the sample boundary, ph 0, that is the bank this sweep fills: tests/mixs_rd)
    if (cpu_mw) begin
      if (cpu_mw_hi) mixs[dsp_bank][cpu_mw_bus][19:4] <= cpu_mw_data;
      else mixs[dsp_bank][cpu_mw_bus][3:0] <= cpu_mw_data[3:0];
    end
  end
  initial begin
    kx_pend = 1'b0; kx_cur = 1'b0; lfsr = 17'd1; sbank = 1'b0; a_valid = 1'b0; bq = '0;
    looped = '0; lfo_reload = '0; mixs_w[0] = '0; mixs_w[1] = '0;
    for (int b = 0; b < 2; b++) for (int i = 0; i < 16; i++) mixs[b][i] = '0;
    mq_v = 1'b0; mq_bank = 1'b0; mq_isel = '0; mq_val = '0;
    for (int i = 0; i < 4; i++) begin fifo1[i] = '0; fifo2[i] = '0; end
    eg_clk_q = 1'b0; eg_cnt_q = '0; dl_acc = '0; dr_acc = '0; dsum_l = '0; dsum_r = '0;
  end

  assign cpu_mr_data = mixs[dsp_bank][cpu_mr_bus];     // the DSP's bank: always complete
  assign dsp_mixs = mixs[dsp_bank][dsp_mixs_bus];
  // the DSP reads the bank filled in the previous sample, from its boundary (ph DSP_OFFSET) to the next one
  always_ff @(posedge clk) if (ce && ph == 9'(DSP_OFFSET - 1)) dsp_bank <= ~sbank;  // sbank toggles at ph 511
  initial dsp_bank = 1'b1;

  // monitors
  logic mon_afsel_q; logic [5:0] mon_slot_q; logic mon_lp_q;
  always_ff @(posedge clk) begin mon_afsel_q <= mon_afsel; mon_slot_q <= mon_slot; mon_lp_q <= looped[mon_slot]; end
  wire [9:0]  m_a = mon_eg_rdata[27:18];
  wire [1:0]  m_ast = mon_eg_rdata[17:16];
  wire        m_off = mon_eg_rdata[15];
  wire [12:0] m_v = mon_eg_rdata[14:2];
  wire [1:0]  m_fst = mon_eg_rdata[1:0];
  // LP as it was when the read issued (the read clears it: tests/probe)
  assign mon_eg = mon_afsel_q ? {mon_lp_q, m_fst, m_v} : {mon_lp_q, m_ast, m_off ? 13'h1FFF : {3'd0, m_a}};
  assign mon_ca = mon_ca_rdata;

  assign warn_step = ce && c == 3'd3 && stp.warn;
endmodule
