#pragma once
// The routed chip at transistor level: its layout's netlist as a SPICE deck for TreeSPICE, driven by what a testbench did at its pins, and its outputs compared
// with what the testbench's run recorded. Nothing assumed about the logic: every output, at the end of every clock cycle.
#include "flow/vcd.h"

#include <map>
#include <string>
#include <vector>

namespace sim {

struct ChipDeckConfig {
    std::string cdl;       // OpenROAD's write_cdl: the top and its cells' transistors
    std::string spef;      // "": no parasitics
    std::string verilog;   // the routed netlist, for its ports' directions
    std::string top;
    std::string dut;       // the design's instance in the recording, as tb.dut
    std::string modelCards;  // what the devices need: .osdi, .lib, .param
    double supply = 1.2;
    double edgeSeconds = 50e-12;   // inputs rise and fall in this
    double stopSeconds = 0;        // 0: the recording's end
    double outputLoadFarads = 5e-15;
};

struct ChipPort { std::string name, node; int bit; bool output; };  // name is the bus's; bit -1 for a scalar

struct ChipDeck {
    std::string text;
    std::vector<ChipPort> ports;
    std::string clockPort;
    long transistors = 0, nets = 0;
    double capacitanceFarads = 0;  // from the SPEF, added to the nets
    double stopSeconds = 0;
};

// Throws with the reason: a device it cannot simulate, a port the recording does not hold, a clock it cannot find.
ChipDeck buildChipDeck(const ChipDeckConfig& cfg, const flow::Vcd& recording, const std::string& clockPort);

// A node name SPICE reads as one word
std::string spiceNode(const std::string& net);

struct ChipCompare {
    long samples = 0, mismatches = 0, undecided = 0;  // undecided: mid-rail at a sample, counted as a mismatch too
    std::string first;  // the first difference, in words
};
// At each rising clock edge of the recording, just before it: the voltage (time, volts per output node) against the recorded value. Samples where the recording holds x are skipped.
ChipCompare compareOutputs(const ChipDeck& deck, const flow::Vcd& recording, const std::string& dut, const std::map<std::string, std::vector<std::pair<double, double>>>& voltages, double supply);

}  // namespace sim
