// The RV32I chip's core: ORFS's single-cycle riscv, the firmware in a ROM, 64 words of data in an IHP SRAM macro, and a
// UART byte port to the pads. Memory map as the reference SoC's: ROM at 0, SRAM at 0x1000_0000, UART at 0x2000_0000.
// The core reads memory in the cycle it asks, so the SRAM takes the falling edge: half a clock for the address, half for the data.
module soc_core (
  input            clk,
  input            reset,
  output reg [7:0] uart_data,
  output reg       uart_valid,
  output           halted
);
  wire [31:0] pc, instr, aluout, writedata, readdata, rom_data;
  wire        memwrite, memread;
  riscv cpu (.clk(clk), .reset(reset), .pc(pc), .instr(instr), .memwrite(memwrite), .aluout(aluout),
             .writedata(writedata), .readdata(readdata), .memread(memread), .suspend(halted));
  firmware_rom rom (.a(pc[9:2]), .b(aluout[9:2]), .y(instr), .z(rom_data));

  wire        sram_sel = aluout[31:28] == 4'h1;
  wire [63:0] sram_q;
  RM_IHPSG13_1P_64x64_c2_bm_bist sram (
    .A_CLK(~clk), .A_MEN(sram_sel), .A_WEN(sram_sel & memwrite), .A_REN(sram_sel & ~memwrite), .A_ADDR(aluout[7:2]),
    .A_DIN({32'b0, writedata}), .A_DLY(1'b1), .A_DOUT(sram_q), .A_BM({32'b0, {32{1'b1}}}),
    .A_BIST_CLK(1'b0), .A_BIST_EN(1'b0), .A_BIST_MEN(1'b0), .A_BIST_WEN(1'b0), .A_BIST_REN(1'b0),
    .A_BIST_ADDR(6'b0), .A_BIST_DIN(64'b0), .A_BIST_BM(64'b0));
  assign readdata = sram_sel ? sram_q[31:0] : rom_data;

  always @(posedge clk)
    if (reset) begin
      uart_valid <= 1'b0;
      uart_data  <= 8'h00;
    end else begin
      uart_valid <= memwrite && aluout[31:28] == 4'h2;
      if (memwrite && aluout[31:28] == 4'h2) uart_data <= writedata[7:0];
    end
endmodule
