#pragma once
// RV32I instruction encoders: the reference SoC's firmware is built with these (no RISC-V
// toolchain needed). Each returns one 32-bit instruction; branch/jump offsets are in bytes.
#include <cstdint>

namespace soc::rv {

enum Reg : uint32_t { zero, ra, sp, gp, tp, t0, t1, t2, s0, s1, a0, a1, a2, a3, a4, a5 };

constexpr uint32_t R(uint32_t f7, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op) {
    return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
constexpr uint32_t I(int32_t imm, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op) {
    return ((uint32_t)imm & 0xfff) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
constexpr uint32_t S(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3) {
    const uint32_t u = (uint32_t)imm;
    return ((u >> 5) & 0x7f) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (u & 31) << 7 | 0x23;
}
constexpr uint32_t B(int32_t off, uint32_t rs2, uint32_t rs1, uint32_t f3) {
    const uint32_t u = (uint32_t)off;
    return ((u >> 12) & 1) << 31 | ((u >> 5) & 0x3f) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 |
           ((u >> 1) & 15) << 8 | ((u >> 11) & 1) << 7 | 0x63;
}
constexpr uint32_t lui(uint32_t rd, uint32_t imm20) { return imm20 << 12 | rd << 7 | 0x37; }
constexpr uint32_t jal(uint32_t rd, int32_t off) {
    const uint32_t u = (uint32_t)off;
    return ((u >> 20) & 1) << 31 | ((u >> 1) & 0x3ff) << 21 | ((u >> 11) & 1) << 20 | ((u >> 12) & 0xff) << 12 |
           rd << 7 | 0x6f;
}
constexpr uint32_t addi(uint32_t rd, uint32_t rs1, int32_t imm) { return I(imm, rs1, 0, rd, 0x13); }
constexpr uint32_t andi(uint32_t rd, uint32_t rs1, int32_t imm) { return I(imm, rs1, 7, rd, 0x13); }
constexpr uint32_t add(uint32_t rd, uint32_t rs1, uint32_t rs2) { return R(0, rs2, rs1, 0, rd, 0x33); }
constexpr uint32_t sub(uint32_t rd, uint32_t rs1, uint32_t rs2) { return R(0x20, rs2, rs1, 0, rd, 0x33); }
constexpr uint32_t lw(uint32_t rd, uint32_t rs1, int32_t imm) { return I(imm, rs1, 2, rd, 0x03); }
constexpr uint32_t lbu(uint32_t rd, uint32_t rs1, int32_t imm) { return I(imm, rs1, 4, rd, 0x03); }
constexpr uint32_t sw(uint32_t rs2, uint32_t rs1, int32_t imm) { return S(imm, rs2, rs1, 2); }
constexpr uint32_t sb(uint32_t rs2, uint32_t rs1, int32_t imm) { return S(imm, rs2, rs1, 0); }
constexpr uint32_t beq(uint32_t rs1, uint32_t rs2, int32_t off) { return B(off, rs2, rs1, 0); }
constexpr uint32_t bne(uint32_t rs1, uint32_t rs2, int32_t off) { return B(off, rs2, rs1, 1); }
constexpr uint32_t blt(uint32_t rs1, uint32_t rs2, int32_t off) { return B(off, rs2, rs1, 4); }
constexpr uint32_t ecall() { return 0x73; }

}  // namespace soc::rv
