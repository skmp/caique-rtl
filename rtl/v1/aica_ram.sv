// aica_ram.sv -- simple dual-port RAM (one read port, one write port, registered read: data the clock after the
// address), with write lanes.  Every AICA buffer is one of these; synthesis maps them to M10K.  INIT fills the array.
module aica_ram #(
  parameter int W = 16,              // word width
  parameter int D = 64,              // depth
  parameter int LANES = 1,           // write lanes (W must be a multiple)
  parameter logic [W-1:0] INIT = '0
) (
  input  logic                   clk,
  input  logic                   re,       // read enable (the registered output holds while 0)
  input  logic [$clog2(D)-1:0]   raddr,
  output logic [W-1:0]           rdata,
  input  logic                   we,
  input  logic [$clog2(D)-1:0]   waddr,
  input  logic [LANES-1:0]       wlane,
  input  logic [W-1:0]           wdata
);
  localparam int LW = W / LANES;
  logic [W-1:0] mem [D];
  initial for (int i = 0; i < D; i++) mem[i] = INIT;
  always_ff @(posedge clk) begin
    if (re) rdata <= mem[raddr];
    if (we) for (int l = 0; l < LANES; l++) if (wlane[l]) mem[waddr][l*LW +: LW] <= wdata[l*LW +: LW];
  end
endmodule
