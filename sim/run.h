#pragma once
// A simulation run: a netlist given to TreeSPICE's `treespice` in a worker process (a crash or a hang never takes the
// caller down), its ngspice raw file read back. Each run keeps its files in its own directory: input.cir, out.raw,
// log.txt.
#include <atomic>
#include <chrono>
#include <complex>
#include <string>
#include <vector>

namespace sim {

struct Plot {  // one analysis' results
    std::string name;  // the raw file's Plotname: Operating Point, Transient Analysis, AC Analysis, ...
    bool complex = false;
    std::vector<std::string> vectors;                      // names; the scale (time, frequency, ...) first, if any
    std::vector<std::vector<std::complex<double>>> data;  // data[vector][point]
};

// An ngspice binary raw file, every plot in it; anything else (ASCII, a truncated file) is an error
std::vector<Plot> readRaw(const std::string& path);

struct Run {
    std::string dir, log;
    std::vector<Plot> plots;
};

// Writes dir/input.cir (dir must exist) and runs `simulator -b --cpu -r out.raw input.cir` there. A simulator that
// fails, times out, is cancelled (a set `cancel` kills it) or writes no results is an error whose text ends with the
// log's last lines.
Run simulate(const std::string& simulator, const std::string& dir, const std::string& netlist, std::chrono::seconds timeout,
             const std::atomic<bool>* cancel = nullptr);

}  // namespace sim
