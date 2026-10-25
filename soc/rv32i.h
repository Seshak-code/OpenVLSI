#pragma once
// RV32I instruction-set simulator (Roadmap Phase 5: "firmware on CPU model", the top rung of the
// abstraction ladder). One hart, the full RV32I base integer ISA, no CSRs or interrupts; ECALL and
// EBREAK halt it. Memory goes through a Bus that charges wait cycles (the SoC's SRAM timing comes from
// SPICE characterisation), so cycles() is the program's run time at the SoC clock.
#include <cstdint>

namespace soc {

struct Bus {
    virtual ~Bus() = default;
    // bytes = 1, 2 or 4 (little-endian); wait: extra cycles the access costs
    virtual uint32_t load(uint32_t addr, int bytes, int &wait) = 0;
    virtual void store(uint32_t addr, int bytes, uint32_t value, int &wait) = 0;
};

class Rv32i {
public:
    explicit Rv32i(Bus &bus, uint32_t resetPc = 0) : bus_(bus), pc_(resetPc) {}
    // Executes one instruction; false once halted (ECALL / EBREAK) or on an illegal instruction.
    bool step();
    // Runs until halted or maxInstructions; returns the instructions executed.
    uint64_t run(uint64_t maxInstructions);

    bool halted() const { return halted_; }
    bool illegal() const { return illegal_; }  // halted on an instruction it does not implement
    uint32_t pc() const { return pc_; }
    uint32_t reg(int i) const { return i ? x_[i] : 0; }
    uint64_t cycles() const { return cycles_; }  // one per instruction plus the bus's wait cycles

private:
    Bus &bus_;
    uint32_t x_[32] = {};
    uint32_t pc_;
    uint64_t cycles_ = 0;
    bool halted_ = false, illegal_ = false;
};

}  // namespace soc
