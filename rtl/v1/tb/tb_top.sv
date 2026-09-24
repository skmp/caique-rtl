// tb_top.sv -- aica_core with a behavioural wave RAM (2 MB as two 16-bit banks, even / odd words, so that any two
// consecutive words are one access).  Driven by tb/cosim.cpp.
module tb_top (
  input  logic         clk,
  input  logic         ce,
  input  logic [13:0]  eg_k,
  input  logic         eg_par,
  input  logic [6:0]   tim_phase, output logic sh4_irq,
  input  logic         ld, input logic [15:0] ld_mdec, input logic [16:0] ld_lfsr,
  input  logic         dbg_req, input logic dbg_we, input logic dbg_ram, input logic [20:0] dbg_addr,
  input  logic [31:0]  dbg_wdata, output logic [31:0] dbg_rdata, output logic dbg_ack,
  input  logic         sh_req, input logic sh_we, input logic sh_ram, input logic [20:0] sh_addr,
  input  logic [31:0]  sh_wdata, input logic [3:0] sh_be, output logic [31:0] sh_rdata, output logic sh_ack,
  input  logic         arm_req, input logic arm_we, input logic arm_ram, input logic arm_lock,
  input  logic [20:0]  arm_addr, input logic [31:0] arm_wdata, input logic [3:0] arm_be,
  output logic [31:0]  arm_rdata, output logic arm_ack,
  output logic signed [15:0] out_l, output logic signed [15:0] out_r, output logic out_valid,
  output logic [8:0]   ph, output logic [15:0] mdec, output logic warn_step,
  output logic         ram_req_o, output logic [2:0] ram_c_o
);
  logic ram_req, ram_we; logic [19:0] ram_waddr; logic [31:0] ram_wdata, ram_rdata; logic [3:0] ram_be;
  aica_core u_core (.clk, .ce, .eg_k, .eg_par, .tim_phase, .sh4_irq, .ld, .ld_mdec, .ld_lfsr, .ph_o(ph),
    .dbg_req, .dbg_we, .dbg_ram, .dbg_addr, .dbg_wdata, .dbg_rdata, .dbg_ack,
    .sh_req, .sh_we, .sh_ram, .sh_addr, .sh_wdata, .sh_be, .sh_rdata, .sh_ack,
    .arm_req, .arm_we, .arm_ram, .arm_lock, .arm_addr, .arm_wdata, .arm_be, .arm_rdata, .arm_ack,
    .ram_req, .ram_we, .ram_waddr, .ram_wdata, .ram_be, .ram_rdata,
    .out_l, .out_r, .out_valid, .mdec_o(mdec), .warn_step);
  assign ram_req_o = ram_req;
  assign ram_c_o = ph[2:0];

  logic [15:0] bank0 [524288];   // even words
  logic [15:0] bank1 [524288];   // odd words
  initial for (int i = 0; i < 524288; i++) begin bank0[i] = '0; bank1[i] = '0; end
  wire [19:0] a0 = ram_waddr, a1 = ram_waddr + 20'd1;
  always_ff @(posedge clk) if (ram_req) begin
    logic [15:0] w0, w1;
    w0 = a0[0] ? bank1[a0[19:1]] : bank0[a0[19:1]];
    w1 = a1[0] ? bank1[a1[19:1]] : bank0[a1[19:1]];
    ram_rdata <= {w1, w0};
    if (ram_we) begin
      if (ram_be[1:0] != 2'b00) begin
        logic [15:0] n0;
        n0 = {ram_be[1] ? ram_wdata[15:8] : w0[15:8], ram_be[0] ? ram_wdata[7:0] : w0[7:0]};
        if (a0[0]) bank1[a0[19:1]] <= n0; else bank0[a0[19:1]] <= n0;
      end
      if (ram_be[3:2] != 2'b00) begin
        logic [15:0] n1;
        n1 = {ram_be[3] ? ram_wdata[31:24] : w1[15:8], ram_be[2] ? ram_wdata[23:16] : w1[7:0]};
        if (a1[0]) bank1[a1[19:1]] <= n1; else bank0[a1[19:1]] <= n1;
      end
    end
  end
endmodule
