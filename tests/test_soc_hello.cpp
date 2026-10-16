// test_soc_hello — Roadmap Phase 5 gate: "hello world" firmware on the reference SoC, with the SRAM's
// timing from a SPICE characterisation (TreeSPICE, IHP SG13G2) and the UART line co-simulated in SPICE.
// Needs datasets/pdk/ihp and datasets/osdi/psp103.osdi (acutesim_engine/tests/build_osdi.sh); without
// them the SPICE parts are skipped and only the ISA checks run.
#include "compute/soc/rv32i.h"
#include "compute/soc/rv32i_asm.h"
#include "compute/soc/soc.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

// Flat memory for ISA checks: program at 0, data anywhere below 64 KiB
struct FlatBus : soc::Bus {
    std::vector<uint8_t> mem = std::vector<uint8_t>(65536, 0);
    uint32_t load(uint32_t a, int bytes, int&) override { uint32_t v = 0; std::memcpy(&v, &mem[a & 0xffff], bytes); return v; }
    void store(uint32_t a, int bytes, uint32_t v, int&) override { std::memcpy(&mem[a & 0xffff], &v, bytes); }
};

static soc::Rv32i runProgram(FlatBus& bus, const std::vector<uint32_t>& prog) {
    std::memcpy(bus.mem.data(), prog.data(), prog.size() * 4);
    soc::Rv32i cpu(bus);
    cpu.run(10000);
    return cpu;
}

int main() {
    using namespace soc::rv;
    std::printf("== RV32I ==\n");
    {
        FlatBus bus;
        auto cpu = runProgram(bus, {
            addi(t0, zero, -5),          // t0 = -5
            addi(t1, zero, 7),           // t1 = 7
            add(t2, t0, t1),             // 2
            sub(s0, t0, t1),             // -12
            lui(s1, 0x12345),            // 0x12345000
            addi(a0, zero, 0x100),
            sw(t0, a0, 0),               // mem[0x100] = -5
            lbu(a1, a0, 0),              // 0xfb (zero-extended)
            lw(a2, a0, 0),               // -5
            blt(t0, t1, 8),              // taken: skip the next
            addi(a3, zero, 99),
            addi(a4, zero, 42),
            jal(ra, 8),                  // ra = pc + 4, skip the next
            addi(a5, zero, 1),
            ecall(),
        });
        check(cpu.halted() && !cpu.illegal(), "program halts on ECALL");
        check(cpu.reg(t2) == 2 && (int32_t)cpu.reg(s0) == -12, "add / sub with negative immediates");
        check(cpu.reg(s1) == 0x12345000u, "lui");
        check(cpu.reg(a1) == 0xfbu && (int32_t)cpu.reg(a2) == -5, "sw, then lbu zero-extends and lw reads back");
        check(cpu.reg(a3) == 0 && cpu.reg(a4) == 42, "blt taken skips one instruction");
        check(cpu.reg(a5) == 0 && cpu.reg(ra) == 13 * 4, "jal links and jumps");
        check(cpu.reg(zero) == 0, "x0 stays zero");
    }

    std::printf("== reference SoC ==\n");
    const auto rom = soc::helloFirmware();
    const soc::SocRun fast = soc::runSoc(rom, soc::SramTiming{});
    check(fast.ok && fast.uart == soc::helloText(), "hello world on the SoC, zero-wait SRAM (\"" + fast.uart + "\")");
    {
        soc::SramTiming slow;
        slow.waitCycles = 3;
        const soc::SocRun r = soc::runSoc(rom, slow);
        // The copy loop's 15 SRAM stores (14 bytes + the 0) and the print loop's first load cost 3 cycles
        // each; the print loop's later loads overlap the UART's back-pressure (the CPU waits for the line)
        const long extra = (long)r.cycles - (long)fast.cycles;
        check(r.ok && r.uart == soc::helloText() && extra == 3 * 16,
              "SRAM wait cycles: +" + std::to_string(extra) + " cycles (3 x 16 exposed accesses)");
    }

    const std::string datasets = SOC_DATASETS;
    if (!std::filesystem::exists(datasets + "/osdi/psp103.osdi") || !std::filesystem::exists(datasets + "/pdk/ihp")) {
        std::printf("  (no datasets/osdi/psp103.osdi or datasets/pdk/ihp: SPICE parts skipped)\n");
    } else {
        std::printf("== SPICE (TreeSPICE, IHP SG13G2) ==\n");
        const double clk = 2e9;  // a 2 GHz core makes the bitline's read time cost cycles
        const soc::SramTiming sram = soc::characterizeSram(datasets, clk);
        check(sram.error.empty() && sram.readDelay > 5e-12 && sram.readDelay < 1e-9,
              "6T SRAM read characterised: " + std::to_string(sram.readDelay * 1e12) + " ps to a " +
                  std::to_string((int)(sram.senseMargin * 1e3)) + " mV split, " + std::to_string(sram.waitCycles) +
                  " wait cycle(s) at 2 GHz" + (sram.error.empty() ? "" : " (" + sram.error + ")"));
        soc::SocConfig cfg;
        cfg.clockHz = clk;
        cfg.uartDivider = 200;  // 10 Mbaud at 2 GHz
        const soc::SocRun run = soc::runSoc(rom, sram, cfg);
        check(run.ok && run.uart == soc::helloText(), "hello world with the characterised SRAM timing: " +
                                                          std::to_string(run.cycles) + " cycles");
        const soc::CosimResult co = soc::cosimUart(run, datasets, cfg);
        check(co.ok && co.text == soc::helloText(),
              "UART co-simulated (pad driver, line, receiver in SPICE) decodes \"" + co.text + "\"" +
                  (co.error.empty() ? "" : " (" + co.error + ")"));
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
