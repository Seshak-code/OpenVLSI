`timescale 1ns/1ps
// The RV32I chip at its pads: a 25 MHz clock and reset in, the firmware's UART bytes out
module tb;
  reg clk = 0, reset = 1;
  wire [7:0] d;
  wire valid, halted;
  rv32i_chip dut (.clk(clk), .reset(reset), .uart_d0(d[0]), .uart_d1(d[1]), .uart_d2(d[2]), .uart_d3(d[3]), .uart_d4(d[4]),
                  .uart_d5(d[5]), .uart_d6(d[6]), .uart_d7(d[7]), .uart_valid(valid), .halted(halted),
                  .vdd(), .vss(), .iovdd(), .iovss());
  always #20 clk = ~clk;
  reg [8*16-1:0] got = 0;
  integer n = 0, cycles = 0;
  initial #100 reset = 0;
  always @(posedge clk) begin
    if (valid) begin got = {got[8*15-1:0], d}; n = n + 1; $write("%c", d); end
    cycles = cycles + 1;
    if (halted && !reset) begin
      if (n == 14 && got[8*14-1:0] == "Hello, world!\n") $display("\nPASS: Hello, world! after %0d cycles", cycles);
      else $display("\nFAIL: %0d bytes before the halt", n);
      $finish;
    end
    if (cycles > 5000) begin $display("FAIL: no halt in 5000 cycles"); $finish; end
  end
endmodule
