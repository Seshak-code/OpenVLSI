#include "soc.h"
#include "rv32i.h"
#include "rv32i_asm.h"

#include "engine_api/engine_callbacks.h"
#include "engine_api/isimulation_engine.h"
#include "engine_api/simulation_dto.h"
#include "engine_api/simulation_session.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <sstream>

namespace soc {

namespace {

constexpr uint32_t kRomBase = 0x00000000, kSramBase = 0x10000000, kUartBase = 0x20000000, kMemSize = 4096;
constexpr uint32_t kStringAt = 0x200;
const char *kIhp = "pdk/ihp/ihp-sg13g2";

// A transient through the engine API: the waveform rows (time, then nodes by first appearance)
std::vector<acutesim::compute::orchestration::WaveformDataPoint>
transient(const std::string &netlist, const std::string &baseDir, double step, double stop, std::string &error) {
    using namespace acutesim::compute::orchestration;
    auto engine = acutesim::ISimulationEngine::create();
    engine->setPrecisionMode(acutesim::PrecisionMode::Native_F64);
    auto session = engine->createSession();
    SimulationRequestDTO req;
    req.source.payload = netlist;
    req.source.baseDir = baseDir;
    req.transient.stepSizeS = step;
    req.transient.stopTimeS = stop;
    acutesim::EngineCallbacks cb;
    SimulationResponseDTO r = session->runTransient(req, cb, nullptr);
    if (!r.converged || r.waveformPoints.empty()) error = r.errorDetail.empty() ? "transient failed" : r.errorDetail;
    return r.waveformPoints;
}

// First time v (node column) crosses level after t0, by linear interpolation; -1 if never
double crossing(const std::vector<acutesim::compute::orchestration::WaveformDataPoint> &w,
                const std::function<double(const std::vector<double> &)> &v, double level, double t0) {
    for (size_t i = 1; i < w.size(); ++i) {
        if (w[i].timeS < t0) continue;
        const double a = v(w[i - 1].nodeVoltages) - level, b = v(w[i].nodeVoltages) - level;
        if (a == 0.0) return w[i - 1].timeS;
        if (a * b < 0.0) return w[i - 1].timeS + (w[i].timeS - w[i - 1].timeS) * (-a) / (b - a);
    }
    return -1.0;
}

class SocBus : public Bus {
public:
    SocBus(const std::vector<uint32_t> &romWords, const SramTiming &sram, const SocConfig &cfg)
        : rom_(kMemSize, 0), sram_(kMemSize, 0), wait_(sram.waitCycles), cfg_(cfg) {
        std::memcpy(rom_.data(), romWords.data(), std::min<size_t>(romWords.size() * 4, kMemSize));
        tx_.push_back({0.0, cfg.vdd});  // idle high
    }
    const Rv32i *cpu = nullptr;
    std::string uart;
    std::vector<std::pair<double, double>> tx_;
    uint64_t lineFree = 0;  // cycle the UART line is idle again
    bool fault = false;

    uint32_t load(uint32_t addr, int bytes, int &wait) override {
        uint8_t *mem = region(addr, bytes, wait);
        if (!mem) return 0;
        uint32_t v = 0;
        std::memcpy(&v, mem, bytes);
        return v;
    }
    void store(uint32_t addr, int bytes, uint32_t value, int &wait) override {
        if (addr == kUartBase) { transmit((uint8_t)value, wait); return; }
        if (addr < kSramBase + kMemSize && addr >= kSramBase) {
            uint8_t *mem = region(addr, bytes, wait);
            if (mem) std::memcpy(mem, &value, bytes);
            return;
        }
        fault = true;  // a store to ROM or unmapped space
    }

private:
    std::vector<uint8_t> rom_, sram_;
    int wait_;
    SocConfig cfg_;

    uint8_t *region(uint32_t addr, int bytes, int &wait) {
        if (addr + bytes <= kRomBase + kMemSize) return &rom_[addr - kRomBase];
        if (addr >= kSramBase && addr + bytes <= kSramBase + kMemSize) { wait = wait_; return &sram_[addr - kSramBase]; }
        fault = true;
        return nullptr;
    }
    // 8N1 frame: the store waits until the line is free, then the bits are scheduled as TX corners
    void transmit(uint8_t byte, int &wait) {
        uart.push_back((char)byte);
        const uint64_t now = cpu->cycles(), start = std::max(now, lineFree);
        wait = (int)(start - now);
        int bits[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        for (int k = 0; k < 8; ++k) bits[1 + k] = (byte >> k) & 1;
        for (int k = 0; k < 10; ++k) level(start + (uint64_t)k * cfg_.uartDivider, bits[k] ? cfg_.vdd : 0.0);
        lineFree = start + 10ull * cfg_.uartDivider;
    }
    void level(uint64_t cycle, double v) {
        if (v == tx_.back().second) return;
        const double t = (double)cycle / cfg_.clockHz;
        tx_.push_back({t, tx_.back().second});
        tx_.push_back({t + cfg_.edge, v});
    }
};

}  // namespace

const char *helloText() { return "Hello, world!\n"; }

std::vector<uint32_t> helloFirmware() {
    // Word accesses only, one character per word: the RTL core (ORFS riscv32i, Phase 7) has no
    // byte enables (SB rewrites the whole word, LBU ignores the address's low bits)
    using namespace rv;
    std::vector<uint32_t> w = {
        addi(a0, zero, (int32_t)kStringAt),        // 0  src: the string in ROM
        lui(a1, kSramBase >> 12),                  // 1  SRAM base
        addi(a2, a1, 0),                           // 2  dst
        lw(t0, a0, 0),                             // 3  copy: t0 = *src
        sw(t0, a2, 0),                             // 4  *dst = t0
        addi(a0, a0, 4),                           // 5
        addi(a2, a2, 4),                           // 6
        bne(t0, zero, (3 - 7) * 4),                // 7  until the terminating 0 is copied
        lui(a3, kUartBase >> 12),                  // 8  UART
        addi(a2, a1, 0),                           // 9  from the SRAM copy
        lw(t0, a2, 0),                             // 10 print: t0 = *p
        beq(t0, zero, (15 - 11) * 4),              // 11 0: done
        sw(t0, a3, 0),                             // 12 transmit
        addi(a2, a2, 4),                           // 13
        jal(zero, (10 - 14) * 4),                  // 14
        ecall(),                                   // 15 done (the core raises `suspend`)
    };
    w.resize(kStringAt / 4, 0);
    for (const char c : std::string(helloText())) w.push_back((uint8_t)c);
    w.push_back(0);
    return w;
}

SramTiming characterizeSram(const std::string &datasets, double clockHz) {
    SramTiming out;
    // Read of a cell storing 0 (Q = 0): BL discharges through the access and pull-down devices.
    // Bitlines precharged to VDD (.ic + UIC), 20 fF each (a 64-row column).
    std::ostringstream n;
    n << "* 6T SRAM read, IHP SG13G2 lv devices\n"
      << ".osdi osdi/psp103.osdi\n.lib " << kIhp << "/libs.tech/ngspice/models/cornerMOSlv.lib mos_tt\n"
      << "VWL wl 0 PULSE(0 1.2 100p 20p 20p 2n 4n)\nCBL bl 0 20f\nCBLB blb 0 20f\nVDD vdd 0 DC 1.2\n"
      << "XPU1 q qb vdd vdd sg13_lv_pmos w=0.15u l=0.13u\nXPD1 q qb 0 0 sg13_lv_nmos w=0.30u l=0.13u\n"
      << "XPU2 qb q vdd vdd sg13_lv_pmos w=0.15u l=0.13u\nXPD2 qb q 0 0 sg13_lv_nmos w=0.30u l=0.13u\n"
      << "XPG1 bl wl q 0 sg13_lv_nmos w=0.20u l=0.13u\nXPG2 blb wl qb 0 sg13_lv_nmos w=0.20u l=0.13u\n"
      << ".ic v(bl)=1.2 v(blb)=1.2 v(q)=0 v(qb)=1.2\n.tran 1p 1n uic\n.end\n";
    const auto w = transient(n.str(), datasets, 1e-12, 1e-9, out.error);
    if (!out.error.empty()) return out;
    // nodes by first appearance: wl, bl, blb
    const double tWl = crossing(w, [](const std::vector<double> &v) { return v[0]; }, 0.6, 0.0);
    const double tSense = crossing(w, [&](const std::vector<double> &v) { return v[2] - v[1]; }, out.senseMargin, 0.0);
    if (tWl < 0.0 || tSense < 0.0) { out.error = "the bitlines did not split by the sense margin within 1 ns"; return out; }
    out.readDelay = tSense - tWl;
    out.waitCycles = std::max(0, (int)std::ceil(out.readDelay * clockHz - 1e-9) - 1);  // the access cycle itself is free
    return out;
}

SocRun runSoc(const std::vector<uint32_t> &rom, const SramTiming &sram, const SocConfig &cfg) {
    SocRun out;
    SocBus bus(rom, sram, cfg);
    Rv32i cpu(bus, kRomBase);
    bus.cpu = &cpu;
    out.instructions = cpu.run(1000000);
    out.cycles = std::max<uint64_t>(cpu.cycles(), bus.lineFree);  // the last frame finishes on the line
    out.seconds = (double)out.cycles / cfg.clockHz;
    out.uart = bus.uart;
    bus.tx_.push_back({out.seconds + (double)cfg.uartDivider / cfg.clockHz, bus.tx_.back().second});  // one idle bit
    out.tx = bus.tx_;
    if (cpu.illegal()) out.error = "illegal instruction at pc " + std::to_string(cpu.pc());
    else if (!cpu.halted()) out.error = "did not halt within 1e6 instructions";
    else if (bus.fault) out.error = "bus fault";
    out.ok = out.error.empty();
    return out;
}

CosimResult cosimUart(const SocRun &run, const std::string &datasets, const SocConfig &cfg) {
    CosimResult out;
    if (run.tx.size() < 2) { out.error = "no TX activity"; return out; }
    const double bit = (double)cfg.uartDivider / cfg.clockHz, stop = run.tx.back().first;
    std::ostringstream n;
    n.precision(12);
    n << "* UART line co-simulation: TX -> pad driver -> line -> receiver\n"
      << ".osdi osdi/psp103.osdi\n.lib " << kIhp << "/libs.tech/ngspice/models/cornerMOSlv.lib mos_tt\n"
      << ".include " << kIhp << "/libs.ref/sg13g2_stdcell/spice/sg13g2_stdcell.spice\n"
      << "VTX tx 0 PWL(";
    for (const auto &[t, v] : run.tx) n << ' ' << t << ' ' << v;
    n << ")\nVDD vdd 0 DC " << cfg.vdd << "\nX1 pad tx vdd 0 sg13g2_inv_4\nR1 pad far 100\nC1 far 0 1p\n"
      << "X2 rx far vdd 0 sg13g2_inv_1\nCL rx 0 5f\n.tran " << bit / 50 << ' ' << stop << "\n.end\n";
    const auto w = transient(n.str(), datasets, bit / 50, stop, out.error);
    if (!out.error.empty()) return out;
    // nodes by first appearance: tx, vdd, pad, far, rx. A2D at VDD/2, then 8N1 frames sampled mid-bit.
    auto rxHigh = [&](double t) {
        const auto it = std::lower_bound(w.begin(), w.end(), t, [](const auto &p, double x) { return p.timeS < x; });
        const auto &p = it == w.end() ? w.back() : *it;
        return p.nodeVoltages[4] > cfg.vdd / 2;
    };
    double t = 0.0;
    while (true) {
        const double fall = crossing(w, [&](const std::vector<double> &v) { return cfg.vdd / 2 - v[4]; }, 0.0, t);
        if (fall < 0.0 || fall + 10 * bit > stop + 0.5 * bit) break;
        uint8_t byte = 0;
        for (int k = 0; k < 8; ++k) byte |= (uint8_t)rxHigh(fall + (1.5 + k) * bit) << k;
        if (!rxHigh(fall + 9.5 * bit)) { out.error = "framing error"; break; }
        out.text.push_back((char)byte);
        t = fall + 9.6 * bit;  // after the stop bit's middle: the next start bit is a fresh fall
    }
    out.seconds = stop;
    out.ok = out.error.empty();
    return out;
}

}  // namespace soc
