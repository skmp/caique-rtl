// aica_pkg.sv -- caique AICA v1: constants and the combinational laws measured on the console (model/NOTES.md).
// Every function here is a transcription of the matching routine in model/sample-model/aica_model.cpp; the comments name it.
package aica_pkg;

  // ---- frame schedule (22.5792 MHz, 512 clocks per sample, 64 frames of 8) --------------------------------------
  // c = ph[2:0] within a frame; frame k = ph[8:3] is slot k's stage A (and slot k-1's stage B).
  //   shared multiplier:  c0 damping (B)   c1 PLFO (A)     c2 DSP step t2   c3 interpolation (B)
  //                       c4 k*H (B)       c5 k*B (B)      c6 DSP step t2   c7 level (B)
  //   wave RAM clocks:    c0 SGC slot step (B)      c1 DSP read     c2 bus RAM lane   c3 DSP write (even step)
  //                       c4 SGC fetch 1 (A)        c5 SGC fetch 2  c6 bus RAM lane   c7 DSP write (odd step)
  // The DSP's 128 steps of 4 clocks run DSP_OFFSET clocks after the sweep: step s = ph DSP_OFFSET + 4 s (mod 512),
  // even steps on c0-c3, odd steps on c4-c7.  The console's arbitration unit is the step: wave RAM has ONE shared
  // slot per step (wren7-rtl/model NOTES "DSP step clock and wave RAM slots", "Contention"), held by a DSP MRD / MWT
  // of that step, by slot K's fetch at step 2K - 14 (mod 128), or by the fixed pair at steps 109 / 111; the SH4 and
  // the ARM get the free ones (aica_bus).  Slot K's claim is the step of its stage B frame (K + 1, c0-c3), which puts
  // DSP step 0 at ph 64.  The one-sample interval edge (SCIPD bit 10) is 40 clocks before DSP step 0 (ph 24).
  localparam int SAMPLE_CLKS = 512;
  localparam int DSP_OFFSET  = 64;     // the DSP sample (and MDEC_CT) starts 8 frames after the slot sweep
  localparam int EDGE_PH     = DSP_OFFSET - 40;   // the one-sample interval edge (for the v2 interrupt controller)
  localparam int FIX_STEP0   = 108;    // the fixed pair lives in steps 108 .. 112 (nominally 109 / 111)

  typedef enum logic [1:0] {EG_ATTACK = 2'd0, EG_DECAY1 = 2'd1, EG_DECAY2 = 2'd2, EG_RELEASE = 2'd3} eg_state_t;

  // ---- envelope: effective rate and increment (aica_model.cpp eff_rate / eg_increment) -------------------------
  function automatic logic [5:0] eff_rate(input logic [4:0] re, input logic [15:0] r14, input logic [15:0] r18);
    int k, s, r;
    if (re == 5'd0) return 6'd0;
    s = 0;
    if (r14[13:10] != 4'hF) begin
      k = int'(r14[13:10]) + (r18[14] ? int'(r18[14:11]) - 16 : int'(r18[14:11]));
      s = (k < 0) ? 0 : 2 * ((k > 15) ? 15 : k) + int'(r18[9]);
    end
    r = 2 * int'(re) + s;
    return (r > 63) ? 6'd63 : 6'(r);
  endfunction

  // eg_inc rows 0..16 (rows 5 / 9 / 13 = {b,2b,b,b,b,2b,b,b}: tests/eg_lock)
  function automatic logic [3:0] eg_inc_tab(input logic [4:0] row, input logic [2:0] idx);
    logic [3:0] b; logic one;
    if (row < 5'd4) begin
      case (row[1:0])
        2'd0: one = idx[0];
        2'd1: one = (idx == 3'd1 || idx == 3'd3 || idx == 3'd4 || idx == 3'd5 || idx == 3'd7);
        2'd2: one = (idx != 3'd0 && idx != 3'd4);
        default: one = (idx != 3'd0);
      endcase
      return {3'b0, one};
    end
    if (row >= 5'd16) return 4'd8;
    b = 4'd1 << ((row - 5'd4) >> 2);
    case (row[1:0])
      2'd0: return b;
      2'd1: return (idx == 3'd1 || idx == 3'd5) ? (b << 1) : b;
      2'd2: return idx[0] ? (b << 1) : b;
      default: return (idx == 3'd0 || idx == 3'd4) ? b : (b << 1);
    endcase
  endfunction

  function automatic logic [3:0] eg_increment(input logic [5:0] r, input logic [13:0] cnt);
    logic [13:0] c; int shift;
    if (r == 6'd0) return 4'd0;
    if (r < 6'd48) begin
      c = cnt - 14'd1;                                   // the R < 48 rows see the counter one step behind
      shift = 11 - int'(r >> 2);
      if ((c & ((14'd1 << shift) - 14'd1)) != 14'd0) return 4'd0;
      return eg_inc_tab({3'b0, r[1:0]}, 3'((c >> shift) & 14'd7));
    end
    return eg_inc_tab((r >= 6'd60) ? 5'd16 : 5'(4 + int'(r) - 48), cnt[2:0]);
  endfunction

  // ---- levels (att_apply's exponent / mantissa, send_level) -----------------------------------------------------
  function automatic logic signed [31:0] send_level(input logic signed [31:0] v, input logic [3:0] l);
    logic [3:0] n; logic signed [34:0] x;
    if (l == 4'd0) return 32'sd0;
    n = 4'd15 - l;
    x = n[0] ? (35'(v) * 35'sd3) : (35'(v) <<< 2);
    return 32'(x >>> (2 + int'(n >> 1)));
  endfunction

  // ---- ADPCM (decode_adpcm) -------------------------------------------------------------------------------------
  function automatic logic [9:0] adpcm_qs(input logic [2:0] d);
    case (d) 3'd4: return 10'h133; 3'd5: return 10'h199; 3'd6: return 10'h200; 3'd7: return 10'h266; default: return 10'h0E6; endcase
  endfunction
  // returns {quant[14:0], sample[15:0]}
  function automatic logic [30:0] adpcm_dec(input logic [3:0] nib, input logic signed [15:0] prev, input logic [14:0] q);
    int rv, o, qn;
    rv = int'(q >> 3) + (nib[0] ? int'(q >> 2) : 0) + (nib[1] ? int'(q >> 1) : 0) + (nib[2] ? int'(q) : 0);
    if (rv > 32767) rv = 32767;
    o = nib[3] ? int'(prev) - rv : int'(prev) + rv;
    if (o > 32767) o = 32767;
    if (o < -32768) o = -32768;
    qn = (int'(q) * int'(adpcm_qs(nib[2:0]))) >> 8;
    if (qn < 127) qn = 127;
    if (qn > 24576) qn = 24576;
    return {15'(qn), 16'(o)};
  endfunction

  // ---- LFO (update_lfo / lfo_calc) ------------------------------------------------------------------------------
  function automatic logic [9:0] lfo_period(input logic [4:0] n);   // O = L + G * (M + 1)
    int s, m, g;
    s = int'(n >> 2); m = int'(~n & 5'd3); g = 128 >> s;
    return 10'(((g - 1) << 2) + g * (m + 1));
  endfunction
  function automatic logic [7:0] alfo_wave(input logic [1:0] ws, input logic [7:0] st);
    case (ws)
      2'd0: return st;
      2'd1: return st[7] ? 8'hFF : 8'h00;
      2'd2: return {st[6:0] ^ (st[7] ? 7'h7F : 7'h00), 1'b0};
      default: return 8'h00;                     // noise: the LFSR byte, taken per sample in the output stage
    endcase
  endfunction
  // truncated to a signed byte as the model stores it (the triangle's state 64 reads -128)
  function automatic logic signed [7:0] plfo_wave(input logic [1:0] ws, input logic [7:0] st);
    int v;
    case (ws)
      2'd0: v = int'($signed(st));
      2'd1: v = st[7] ? -128 : 127;
      // triangle: 126 again at state 64 (tests/sgc_lfo lf_4 / lf_5; the value's bit 0 is not used)
      2'd2: v = (st < 8'd64) ? 2 * int'(st) : (st < 8'd192) ? 254 - 2 * int'(st) : 2 * int'(st) - 512;
      default: v = 0;                            // noise: the LFSR byte (in the pitch stage)
    endcase
    return 8'(v);
  endfunction

  // ---- noise LFSR jumps (x^17 + x^12 + 1; linear: synthesis flattens these to XOR networks) ------------------------
  // the noise LFO waveforms (tests/sgc_lfo, lfo_noise): the ALFO byte two steps after the slot's own step, the PLFO byte
  // 67 steps before the slot's stage-A point, XOR 0x80 (model/NOTES "The noise LFO bytes")
  function automatic logic [16:0] lfsr_fwd(input logic [16:0] l, input int n);
    for (int i = 0; i < n; i++) l = {l[0] ^ l[5], l[16:1]};
    return l;
  endfunction
  function automatic logic [16:0] lfsr_back(input logic [16:0] l, input int n);
    for (int i = 0; i < n; i++) l = {l[15:0], l[16] ^ l[4]};   // the inverse step: old bit 0 = new bit 16 ^ new bit 4
    return l;
  endfunction

  // ---- filter (lpf_q128) ----------------------------------------------------------------------------------------
  function automatic logic [7:0] q128(input logic [4:0] q);
    case (q)
      5'd0: return 8'd192; 5'd1: return 8'd176; 5'd2: return 8'd160; 5'd3: return 8'd144; 5'd4: return 8'd128;
      5'd5: return 8'd120; 5'd6: return 8'd112; 5'd7: return 8'd104; 5'd8: return 8'd96; 5'd9: return 8'd88;
      5'd10: return 8'd80; 5'd11: return 8'd72; 5'd12: return 8'd64; 5'd13: return 8'd60; 5'd14: return 8'd56;
      5'd15: return 8'd52; 5'd16: return 8'd48; 5'd17: return 8'd44; 5'd18: return 8'd40; 5'd19: return 8'd36;
      5'd20: return 8'd32; 5'd21: return 8'd30; 5'd22: return 8'd28; 5'd23: return 8'd26; 5'd24: return 8'd24;
      5'd25: return 8'd22; 5'd26: return 8'd20; 5'd27: return 8'd18; 5'd28: return 8'd16; 5'd29: return 8'd15;
      5'd30: return 8'd14; default: return 8'd13;
    endcase
  endfunction

  // ---- DSP memory float (dsp_float.h) ---------------------------------------------------------------------------
  function automatic logic [15:0] dsp_pack(input logic [23:0] val);
    logic [23:0] t; int e; logic [23:0] v;
    t = val ^ {val[22:0], 1'b0};
    e = 0;
    for (int k = 0; k < 12; k++) if (e == k && !t[23 - k]) e = k + 1;
    v = (e < 12) ? ((val << e) & 24'h3FFFFF) : (val << 11);
    return {val[23], 4'(e), v[21:11]};
  endfunction
  function automatic logic [23:0] dsp_unpack(input logic [15:0] f);
    logic [23:0] v; int e;
    e = int'(f[14:11]);
    v = {f[15], 1'b0, f[10:0], 11'd0};
    if (e > 11) begin e = 11; v[22] = f[15]; end
    else v[22] = ~f[15];
    return 24'($signed(v) >>> e);
  endfunction

endpackage
