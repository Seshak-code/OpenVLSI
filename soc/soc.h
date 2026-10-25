#pragma once
// Reference SoC (Roadmap Phase 5 gate): an RV32I core, ROM, an SRAM whose access time comes from a
// SPICE characterisation of a real-PDK 6T cell (TreeSPICE), and a UART whose TX pin is co-simulated in
// SPICE (pad driver, line, receiver) and decoded back from the analog waveform.
//
//   ROM   0x0000_0000  4 KiB   firmware and its string, 0 wait cycles
//   SRAM  0x1000_0000  4 KiB   ceil(t_read / t_clk) - 1 wait cycles per access, from characterizeSram()
//   UART  0x2000_0000          write: transmit a byte (the store waits while the line is busy)
//         0x2000_0004          read: status, bit 0 RX byte available, bit 1 TX busy, bit 2 framing error seen
//         0x2000_0008          read: RX byte (clears bit 0)
//         0x2000_000C          write: debug port, a byte collected as the run's output (not sent on TX)
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace soc {

struct SramTiming {
    double readDelay = 0.0;  // wordline 50 % -> bitline split of senseMargin [s]
    double senseMargin = 0.1;
    int waitCycles = 0;      // per access at the SoC clock
    std::string error;       // empty when characterised
};

// Read of one IHP SG13G2 6T cell (PSP103 through OSDI, mos_tt) onto a bitline pair loaded like
// a 64-row column, simulated with TreeSPICE. datasets: the directory holding pdk/ihp and osdi/.
SramTiming characterizeSram(const std::string &datasets, double clockHz);

struct SocRun {
    bool ok = false;
    std::string error;
    std::string uart;             // bytes written to the UART register
    uint64_t instructions = 0, cycles = 0;
    double seconds = 0.0;         // cycles at the SoC clock
    std::vector<std::pair<double, double>> tx;  // the UART TX pin, (time, volts) corners
};

struct SocConfig {
    double clockHz = 100e6;
    int uartDivider = 10;   // clock cycles per UART bit
    double vdd = 1.2;
    double edge = 0.2e-9;   // TX pin rise/fall [s]
};

// The firmware: copies "Hello, world!\n" from ROM into SRAM, then sends it from SRAM to the UART.
std::vector<uint32_t> helloFirmware();
const char *helloText();

// The firmware for cosimLockstep: sends each character of helloText() on TX, waits for it to come back
// on RX (a loopback cable), and writes the received byte to the debug port. Word accesses only.
std::vector<uint32_t> echoFirmware();

SocRun runSoc(const std::vector<uint32_t> &rom, const SramTiming &sram, const SocConfig &cfg = {});

struct CosimResult {
    bool ok = false;
    std::string error;
    std::string text;   // decoded from the receiver output's analog waveform
    double seconds = 0.0;
};

// Mixed-signal co-simulation of the UART line: the TX corners drive an IHP sg13g2_inv_4 pad driver
// through a 100 ohm / 1 pF line into an sg13g2_inv_1 receiver (TreeSPICE transient); the receiver
// output is thresholded at VDD/2 (A2D) and decoded as 8N1 UART frames.
CosimResult cosimUart(const SocRun &run, const std::string &datasets, const SocConfig &cfg = {});

struct LockstepResult {
    bool ok = false;
    std::string error;
    std::string debug;               // bytes the firmware wrote to the debug port
    std::string rx;                  // bytes the UART receiver completed, in order
    uint64_t instructions = 0, cycles = 0;
    int exchanges = 0;               // CPU <-> SPICE hand-overs, one per RX oversampling tick
    int framingErrors = 0;
    std::vector<double> latency;     // per RX byte: stop-bit sample time minus the ideal one [s]
    double seconds = 0.0;            // simulated time
    double wallSeconds = 0.0;        // run time of the co-simulation itself
};

// Lockstep mixed-signal co-simulation: the TX pin is wired through SPICE (the cosimUart pad driver, line
// and receiver, one live TreeSPICE transient) back to RX. Every uartDivider/16 cycles (one RX oversampling
// tick) the pin level re-drives the SPICE source, SPICE advances to now, and the receiver output (A2D at
// VDD/2) is the level the UART RX sees, so the CPU's next step depends on the analog result.
// uartDivider must be a multiple of 16.
LockstepResult cosimLockstep(const std::vector<uint32_t> &rom, const SramTiming &sram, const std::string &datasets,
                             const SocConfig &cfg = {});

}  // namespace soc
