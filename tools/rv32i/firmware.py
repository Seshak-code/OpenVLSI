"""The RV32I chip's firmware: the reference SoC's hello (soc/soc.cpp, helloFirmware) as words, a hex file and a two-port ROM in Verilog."""
import sys
# RV32I encodings (word accesses only, as the reference SoC's firmware)
R = dict(zero=0, t0=5, t1=6, a0=10, a1=11, a2=12, a3=13)
def i_type(op, f3, rd, rs1, imm): return ((imm & 0xfff) << 20) | (R[rs1] << 15) | (f3 << 12) | (R[rd] << 7) | op
def addi(rd, rs1, imm): return i_type(0x13, 0, rd, rs1, imm)
def lw(rd, rs1, imm): return i_type(0x03, 2, rd, rs1, imm)
def lui(rd, imm20): return ((imm20 & 0xfffff) << 12) | (R[rd] << 7) | 0x37
def sw(rs2, rs1, imm): return (((imm >> 5) & 0x7f) << 25) | (R[rs2] << 20) | (R[rs1] << 15) | (2 << 12) | ((imm & 0x1f) << 7) | 0x23
def b_type(f3, rs1, rs2, off):
    return (((off >> 12) & 1) << 31) | (((off >> 5) & 0x3f) << 25) | (R[rs2] << 20) | (R[rs1] << 15) | (f3 << 12) | (((off >> 1) & 0xf) << 8) | (((off >> 11) & 1) << 7) | 0x63
def beq(a, b, off): return b_type(0, a, b, off)
def bne(a, b, off): return b_type(1, a, b, off)
def jal(rd, off): return (((off >> 20) & 1) << 31) | (((off >> 1) & 0x3ff) << 21) | (((off >> 11) & 1) << 20) | (((off >> 12) & 0xff) << 12) | (R[rd] << 7) | 0x6f
ECALL = 0x73
SRAM, UART, TEXT = 0x10000000, 0x20000000, 0x200
code = [addi("a0", "zero", TEXT), lui("a1", SRAM >> 12), addi("a2", "a1", 0), lw("t0", "a0", 0), sw("t0", "a2", 0), addi("a0", "a0", 4), addi("a2", "a2", 4),
        bne("t0", "zero", (3 - 7) * 4), lui("a3", UART >> 12), addi("a2", "a1", 0), lw("t0", "a2", 0), beq("t0", "zero", (15 - 11) * 4), sw("t0", "a3", 0),
        addi("a2", "a2", 4), jal("zero", (10 - 14) * 4), ECALL]
words = code + [0] * (TEXT // 4 - len(code)) + [ord(c) for c in "Hello, world!\n"] + [0]
if __name__ == "__main__":
    out = sys.argv[1]
    with open(out + "/firmware.hex", "w") as f:
        f.write("".join("%08x\n" % w for w in words))
    with open(out + "/firmware_rom.v", "w") as f:
        f.write("// The hello firmware, word-addressed, with a port for fetch and one for loads\nmodule firmware_rom(input [7:0] a, input [7:0] b, output reg [31:0] y, output reg [31:0] z);\n")
        for port, addr in (("y", "a"), ("z", "b")):
            f.write("  always @* case (%s)\n" % addr)
            for k, w in enumerate(words):
                if w: f.write("    8'd%d: %s = 32'h%08x;\n" % (k, port, w))
            f.write("    default: %s = 32'h0;\n  endcase\n" % port)
        f.write("endmodule\n")
