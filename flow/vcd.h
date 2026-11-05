#pragma once
// A Value Change Dump: what Verilog simulators record. Fail-closed on a malformed file.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace flow {

struct VcdSignal {
    std::string name;   // hierarchical: tb.dut.count
    int width = 1;
    std::string code;   // the dump's short identifier; signals may share one
};

struct VcdChange { int64_t time; std::string value; };  // a bit string, MSB first, of 0 1 x z; a real as "r3.14"

struct Vcd {
    double secondsPerUnit = 1e-9;
    int64_t endTime = 0;
    std::vector<VcdSignal> vars;                       // sorted by name
    std::map<std::string, std::vector<VcdChange>> changes;  // by code; only what was asked for
};

// Throws std::runtime_error with the reason. With a header-only read, vars and the timescale come back without changes;
// wanted names which signals' changes to keep (empty: all).
Vcd readVcd(const std::string& path, bool headerOnly = false, const std::set<std::string>& wanted = {});

// A value at a time; "x" before its first change
std::string valueAt(const std::vector<VcdChange>& changes, int64_t time);
// A bit string as hex, with x or z where a nibble holds one
std::string toHex(const std::string& bits);

}  // namespace flow
