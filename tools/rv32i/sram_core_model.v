// A functional model of IHP's SRAM macro core (SRAM_1P_behavioral_bm_bist), written to the ports its wrappers (verilog/RM_*.v) give it:
// a clocked RAM with a bit mask, no BIST. Not IHP's model: tools/m7_gate.py uses IHP's RM_IHPSG13_1P_core_behavioral_bm_bist.v from
// ~/eda/ihp-verilog when it is there, and this one otherwise.
module SRAM_1P_behavioral_bm_bist #(parameter P_DATA_WIDTH = 64, parameter P_ADDR_WIDTH = 6) (
  input A_CLK, input A_MEN, input A_WEN, input A_REN, input [P_ADDR_WIDTH-1:0] A_ADDR, input A_DLY,
  input [P_DATA_WIDTH-1:0] A_DIN, output reg [P_DATA_WIDTH-1:0] A_DOUT, input [P_DATA_WIDTH-1:0] A_BM,
  input A_BIST_CLK, input A_BIST_EN, input A_BIST_MEN, input A_BIST_WEN, input A_BIST_REN,
  input [P_ADDR_WIDTH-1:0] A_BIST_ADDR, input [P_DATA_WIDTH-1:0] A_BIST_DIN, input [P_DATA_WIDTH-1:0] A_BIST_BM);
  reg [P_DATA_WIDTH-1:0] mem [0:(1<<P_ADDR_WIDTH)-1];
  always @(posedge A_CLK) if (A_MEN) begin
    if (A_WEN) mem[A_ADDR] <= (mem[A_ADDR] & ~A_BM) | (A_DIN & A_BM);
    if (A_REN) A_DOUT <= mem[A_ADDR];
  end
endmodule
