// The SRAM macro as synthesis sees it: its ports only; the flow places IHP's macro (its LEF, Liberty and GDS)
(* blackbox *)
module RM_IHPSG13_1P_64x64_c2_bm_bist (
  input A_CLK, input A_MEN, input A_WEN, input A_REN, input [5:0] A_ADDR, input [63:0] A_DIN, input A_DLY,
  output [63:0] A_DOUT, input [63:0] A_BM, input A_BIST_CLK, input A_BIST_EN, input A_BIST_MEN, input A_BIST_WEN,
  input A_BIST_REN, input [5:0] A_BIST_ADDR, input [63:0] A_BIST_DIN, input [63:0] A_BIST_BM);
endmodule
