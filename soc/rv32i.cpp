#include <cstdint>
#include "rv32i.h"

namespace soc {

namespace {
int32_t sext(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }
}  // namespace

bool Rv32i::step() {
    if (halted_) return false;
    int wait = 0;
    const uint32_t in = bus_.load(pc_, 4, wait);
    cycles_ += 1 + wait;
    const uint32_t op = in & 0x7f, rd = (in >> 7) & 31, f3 = (in >> 12) & 7, rs1 = (in >> 15) & 31,
                   rs2 = (in >> 20) & 31, f7 = in >> 25;
    const uint32_t a = reg(rs1), b = reg(rs2);
    const int32_t immI = (int32_t)in >> 20;
    const int32_t immS = sext(((in >> 25) << 5) | ((in >> 7) & 31), 12);
    const int32_t immB = sext(((in >> 31) << 12) | (((in >> 7) & 1) << 11) | (((in >> 25) & 63) << 5) |
                                  (((in >> 8) & 15) << 1), 13);
    const int32_t immJ = sext(((in >> 31) << 20) | (((in >> 12) & 255) << 12) | (((in >> 20) & 1) << 11) |
                                  (((in >> 21) & 1023) << 1), 21);
    uint32_t next = pc_ + 4, val = 0;
    bool write = true;
    switch (op) {
        case 0x37: val = in & 0xfffff000u; break;                     // LUI
        case 0x17: val = pc_ + (in & 0xfffff000u); break;              // AUIPC
        case 0x6f: val = pc_ + 4; next = pc_ + immJ; break;            // JAL
        case 0x67: val = pc_ + 4; next = (a + immI) & ~1u; break;      // JALR
        case 0x63: {                                                   // branches
            write = false;
            bool t = false;
            switch (f3) {
                case 0: t = a == b; break;
                case 1: t = a != b; break;
                case 4: t = (int32_t)a < (int32_t)b; break;
                case 5: t = (int32_t)a >= (int32_t)b; break;
                case 6: t = a < b; break;
                case 7: t = a >= b; break;
                default: illegal_ = true;
            }
            if (t) next = pc_ + immB;
            break;
        }
        case 0x03: {                                                   // loads
            const int bytes = 1 << (f3 & 3);
            const uint32_t v = bus_.load(a + immI, bytes, wait);
            cycles_ += wait;
            if (f3 == 0) val = (uint32_t)sext(v, 8);
            else if (f3 == 1) val = (uint32_t)sext(v, 16);
            else if (f3 == 2 || f3 == 4 || f3 == 5) val = v;
            else illegal_ = true;
            break;
        }
        case 0x23:                                                     // stores
            write = false;
            if (f3 > 2) { illegal_ = true; break; }
            bus_.store(a + immS, 1 << f3, b, wait);
            cycles_ += wait;
            break;
        case 0x13:                                                     // ALU with immediate
        case 0x33: {                                                   // ALU register
            const bool imm = op == 0x13;
            const uint32_t y = imm ? (uint32_t)immI : b, sh = y & 31;
            switch (f3) {
                case 0: val = (!imm && f7 == 0x20) ? a - y : a + y; break;
                case 1: val = a << sh; break;
                case 2: val = (int32_t)a < (int32_t)y; break;
                case 3: val = a < y; break;
                case 4: val = a ^ y; break;
                case 5: val = (f7 & 0x20) ? (uint32_t)((int32_t)a >> sh) : a >> sh; break;
                case 6: val = a | y; break;
                case 7: val = a & y; break;
            }
            break;
        }
        case 0x0f: write = false; break;                               // FENCE: nothing to order
        case 0x73: write = false; halted_ = true; break;               // ECALL / EBREAK
        default: write = false; illegal_ = true;
    }
    if (illegal_) { halted_ = true; return false; }
    if (write && rd) x_[rd] = val;
    pc_ = next;
    return !halted_;
}

uint64_t Rv32i::run(uint64_t maxInstructions) {
    uint64_t n = 0;
    while (n < maxInstructions && step()) ++n;
    return n + (halted_ && !illegal_ ? 1 : 0);  // the halting ECALL counts
}

}  // namespace soc
