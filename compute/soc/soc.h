#pragma once
// Reference SoC (Roadmap Phase 5 gate): an RV32I core, ROM, an SRAM whose access time comes from a
// SPICE characterisation of a real-PDK 6T cell (TreeSPICE), and a UART whose TX pin is co-simulated in
// SPICE (pad driver, line, receiver) and decoded back from the analog waveform.
//
//   ROM   0x0000_0000  4 KiB   firmware and its string, 0 wait cycles
//   SRAM  0x1000_0000  4 KiB   ceil(t_read / t_clk) - 1 wait cycles per access, from characterizeSram()
//   UART  0x2000_0000          write: transmit a byte (the store waits while the line is busy)
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

}  // namespace soc
