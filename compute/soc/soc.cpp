#include "soc.h"
#include "rv32i.h"
#include "rv32i_asm.h"

#include "engine_api/engine_callbacks.h"
#include "engine_api/isimulation_engine.h"
#include "engine_api/simulation_dto.h"
#include "engine_api/simulation_session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iterator>
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

// 16x-oversampling 8N1 receiver: tick() once per oversampling period with the A2D'd rx level
struct UartRx {
    bool busy = false, avail = false;
    int framingErrors = 0;
    int count = 0, bit = 0;   // ticks since the start edge was seen (reset each bit); bit -1 = the start bit
    uint8_t shift = 0, data = 0;
    // returns true when a byte completes
    bool tick(bool rx) {
        if (!busy) {
            if (!rx) { busy = true; count = 1; bit = -1; shift = 0; }
            return false;
        }
        if (++count < (bit < 0 ? 8 : 16)) return false;  // 8: the middle of the start bit, then of every bit
        count = 0;
        if (bit < 0) { if (rx) busy = false; else bit = 0; return false; }  // a high at mid-start is a glitch
        if (bit < 8) { shift |= (uint8_t)(rx << bit++); return false; }
        busy = false;
        framingErrors += !rx;  // the stop bit must be high; the byte is delivered either way
        data = shift;
        avail = true;
        return true;
    }
};

class SocBus : public Bus {
public:
    // The TX pin at a cycle: frames never overlap (a store waits for the line), so the latest start decides
    bool txLevel(uint64_t cycle) const {
        const auto f = std::upper_bound(frames.begin(), frames.end(), cycle, [](uint64_t c, const auto &p) { return c < p.first; });
        if (f == frames.begin()) return true;
        const uint64_t k = (cycle - std::prev(f)->first) / cfg_.uartDivider;
        return k >= 9 || (k > 0 && (std::prev(f)->second >> (k - 1) & 1));  // start bit low, 8 data bits, stop high
    }
    SocBus(const std::vector<uint32_t> &romWords, const SramTiming &sram, const SocConfig &cfg)
        : rom_(kMemSize, 0), sram_(kMemSize, 0), wait_(sram.waitCycles), cfg_(cfg) {
        std::memcpy(rom_.data(), romWords.data(), std::min<size_t>(romWords.size() * 4, kMemSize));
        tx_.push_back({0.0, cfg.vdd});  // idle high
    }
    const Rv32i *cpu = nullptr;
    std::string uart, debug;
    std::vector<std::pair<double, double>> tx_;
    std::vector<std::pair<uint64_t, uint8_t>> frames;  // (start cycle, byte) of every frame sent
    UartRx rx;
    uint64_t lineFree = 0;  // cycle the UART line is idle again
    bool fault = false;

    uint32_t load(uint32_t addr, int bytes, int &wait) override {
        if (addr >= kUartBase && addr < kUartBase + 16) {
            if (addr == kUartBase + 4) return rx.avail | (cpu->cycles() < lineFree) << 1 | (rx.framingErrors > 0) << 2;
            if (addr != kUartBase + 8) return 0;
            rx.avail = false;
            return rx.data;
        }
        uint8_t *mem = region(addr, bytes, wait);
        if (!mem) return 0;
        uint32_t v = 0;
        std::memcpy(&v, mem, bytes);
        return v;
    }
    void store(uint32_t addr, int bytes, uint32_t value, int &wait) override {
        if (addr == kUartBase) { transmit((uint8_t)value, wait); return; }
        if (addr == kUartBase + 12) { debug.push_back((char)value); return; }
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
        frames.push_back({start, byte});
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

namespace {
// The text after the code, one character per word, 0-terminated
std::vector<uint32_t> withText(std::vector<uint32_t> w) {
    w.resize(kStringAt / 4, 0);
    for (const char c : std::string(helloText())) w.push_back((uint8_t)c);
    w.push_back(0);
    return w;
}
}  // namespace

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
    return withText(std::move(w));
}

std::vector<uint32_t> echoFirmware() {
    using namespace rv;
    return withText({
        addi(a0, zero, (int32_t)kStringAt),        // 0  src: the string in ROM
        lui(a3, kUartBase >> 12),                  // 1  UART
        lw(t0, a0, 0),                             // 2  t0 = next character
        beq(t0, zero, (12 - 3) * 4),               // 3  0: done
        sw(t0, a3, 0),                             // 4  transmit
        lw(t1, a3, 4),                             // 5  poll the status
        andi(t1, t1, 1),                           // 6  RX byte available?
        beq(t1, zero, (5 - 7) * 4),                // 7  no: poll again
        lw(t1, a3, 8),                             // 8  the byte that came back through SPICE
        sw(t1, a3, 12),                            // 9  to the debug port
        addi(a0, a0, 4),                           // 10
        jal(zero, (2 - 11) * 4),                   // 11
        ecall(),                                   // 12 done
    });
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

// TX -> pad driver -> line -> receiver, the TX source given as its SPICE spec. Two inverters, so rx follows tx.
static std::string uartLine(const std::string &txSource, const SocConfig &cfg) {
    std::ostringstream n;
    n << "* UART line co-simulation: TX -> pad driver -> line -> receiver\n"
      << ".osdi osdi/psp103.osdi\n.lib " << kIhp << "/libs.tech/ngspice/models/cornerMOSlv.lib mos_tt\n"
      << ".include " << kIhp << "/libs.ref/sg13g2_stdcell/spice/sg13g2_stdcell.spice\n"
      << "VTX tx 0 " << txSource << "\nVDD vdd 0 DC " << cfg.vdd << "\nX1 pad tx vdd 0 sg13g2_inv_4\nR1 pad far 100\nC1 far 0 1p\n"
      << "X2 rx far vdd 0 sg13g2_inv_1\nCL rx 0 5f\n";
    return n.str();
}

CosimResult cosimUart(const SocRun &run, const std::string &datasets, const SocConfig &cfg) {
    CosimResult out;
    if (run.tx.size() < 2) { out.error = "no TX activity"; return out; }
    const double bit = (double)cfg.uartDivider / cfg.clockHz, stop = run.tx.back().first;
    std::ostringstream n, pwl;
    pwl.precision(12);
    pwl << "PWL(";
    for (const auto &[t, v] : run.tx) pwl << ' ' << t << ' ' << v;
    pwl << ')';
    n.precision(12);
    n << uartLine(pwl.str(), cfg) << ".tran " << bit / 50 << ' ' << stop << "\n.end\n";
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

LockstepResult cosimLockstep(const std::vector<uint32_t> &rom, const SramTiming &sram, const std::string &datasets,
                             const SocConfig &cfg) {
    using namespace acutesim::compute::orchestration;
    LockstepResult out;
    if (cfg.uartDivider % 16) { out.error = "uartDivider must be a multiple of 16"; return out; }
    const auto t0 = std::chrono::steady_clock::now();
    SocBus bus(rom, sram, cfg);
    Rv32i cpu(bus, kRomBase);
    bus.cpu = &cpu;
    auto engine = acutesim::ISimulationEngine::create();
    engine->setPrecisionMode(acutesim::PrecisionMode::Native_F64);
    auto session = engine->createSession();
    SimulationRequestDTO req;
    std::ostringstream idle;
    idle << "DC " << cfg.vdd;
    req.source.payload = uartLine(idle.str(), cfg) + ".end\n";
    req.source.baseDir = datasets;
    req.transient.stepSizeS = (double)cfg.uartDivider / cfg.clockHz / 50;
    const auto spice = session->beginTransient(req);
    out.error = spice->error();

    const uint64_t period = cfg.uartDivider / 16, kMaxInstructions = 1000000;
    uint64_t next = period;
    bool pin = true;
    while (out.error.empty() && !cpu.halted() && out.instructions < kMaxInstructions) {
        cpu.step();
        ++out.instructions;
        for (; next <= cpu.cycles() && out.error.empty(); next += period) {  // the ticks this instruction spanned
            // the source is re-driven at the SPICE time now (the tick before), so it takes the pin level of then
            const bool level = bus.txLevel(next - period);
            const double t = (double)next / cfg.clockHz;
            double v = 0.0;
            // re-driving a level that has not changed would only add SPICE breakpoints
            if ((level == pin || spice->setSource("VTX", level ? cfg.vdd : 0.0, cfg.edge)) && spice->advanceTo(t) &&
                spice->voltage("rx", v)) {
                pin = level;
                ++out.exchanges;
                if (bus.rx.tick(v > cfg.vdd / 2)) {
                    out.rx.push_back((char)bus.rx.data);
                    const size_t i = out.rx.size() - 1;
                    if (i < bus.frames.size())
                        out.latency.push_back(t - ((double)bus.frames[i].first + 9.5 * cfg.uartDivider) / cfg.clockHz);
                }
            }
            out.error = spice->error();
        }
    }
    out.cycles = cpu.cycles();
    out.seconds = (double)out.cycles / cfg.clockHz;
    out.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    out.debug = bus.debug;
    out.framingErrors = bus.rx.framingErrors;
    if (out.error.empty()) {
        if (cpu.illegal()) out.error = "illegal instruction at pc " + std::to_string(cpu.pc());
        else if (!cpu.halted()) out.error = "did not halt within 1e6 instructions";
        else if (bus.fault) out.error = "bus fault";
    }
    out.ok = out.error.empty();
    return out;
}

}  // namespace soc
