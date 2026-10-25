#include "sim/run.h"

#include "flow/subprocess.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sim {

namespace {

std::runtime_error error(const std::string& path, const std::string& what) { return std::runtime_error("raw file " + path + ": " + what); }

std::string tail(const std::string& log, size_t lines) {  // the last non-empty lines, each on its own line
    std::vector<std::string> all;
    std::istringstream in(log);
    for (std::string l; std::getline(in, l);) if (!l.empty()) all.push_back(l);
    std::string out;
    for (size_t k = all.size() > lines ? all.size() - lines : 0; k < all.size(); ++k) out += "\n" + all[k];
    return out;
}

}  // namespace

std::vector<Plot> readRaw(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw error(path, "cannot be read");
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<Plot> plots;
    size_t at = 0;
    while (at < bytes.size()) {
        Plot p;
        long vars = -1, points = -1;
        bool inVars = false, binary = false;
        while (!binary) {  // the header, line by line, up to Binary:
            const size_t nl = bytes.find('\n', at);
            if (nl == std::string::npos) throw error(path, "a header ends without Binary:");
            const std::string line = bytes.substr(at, nl - at);
            at = nl + 1;
            auto value = [&](const char* key) { return line.substr(std::strlen(key)); };
            if (line.rfind("Plotname: ", 0) == 0) p.name = value("Plotname: ");
            else if (line.rfind("Flags: ", 0) == 0) p.complex = value("Flags: ").find("complex") != std::string::npos;
            else if (line.rfind("No. Variables: ", 0) == 0) vars = std::stol(value("No. Variables: "));
            else if (line.rfind("No. Points: ", 0) == 0) points = std::stol(value("No. Points: "));
            else if (line == "Variables:") inVars = true;
            else if (line == "Binary:") binary = true;
            else if (line == "Values:") throw error(path, "ASCII raw files are not read (treespice writes binary without --ascii)");
            else if (inVars && !line.empty() && line[0] == '\t') {
                std::istringstream ls(line);
                std::string idx, name;
                ls >> idx >> name;
                p.vectors.push_back(name);
            }
        }
        if (vars < 0 || points < 0 || (long)p.vectors.size() != vars) throw error(path, "plot '" + p.name + "' has a malformed header");
        const size_t width = p.complex ? 16 : 8, need = (size_t)vars * (size_t)points * width;
        if (bytes.size() - at < need) throw error(path, "plot '" + p.name + "' is truncated");
        p.data.assign((size_t)vars, std::vector<std::complex<double>>((size_t)points));
        for (long k = 0; k < points; ++k)
            for (long v = 0; v < vars; ++v) {
                double d[2] = {0, 0};
                std::memcpy(d, bytes.data() + at, width);  // native little-endian doubles, as written
                at += width;
                p.data[(size_t)v][(size_t)k] = {d[0], d[1]};
            }
        plots.push_back(std::move(p));
        while (at < bytes.size() && bytes[at] == '\n') ++at;
    }
    return plots;
}

Run simulate(const std::string& simulator, const std::string& dir, const std::string& netlist, std::chrono::seconds timeout,
             const std::atomic<bool>* cancel) {
    namespace fs = std::filesystem;
    if (simulator.empty() || !fs::exists(simulator)) throw std::runtime_error("no simulator: treespice is not at '" + simulator + "'");
    const fs::path d(dir);
    {
        std::ofstream f(d / "input.cir");
        if (!(f << netlist) || !f.flush()) throw std::runtime_error("cannot write " + (d / "input.cir").string());
    }
    // ponytail: double precision on the CPU, the results ngspice is compared with; the GPU fronts' mixed precision
    // once its accuracy against ngspice is shown
    acutesim::orchestration::Subprocess p(simulator, {"-b", "--cpu", "-r", (d / "out.raw").string(), (d / "input.cir").string()});
    p.setWorkingDirectory(dir);
    p.setTimeout(timeout);
    p.setCancelFlag(cancel);
    const auto r = p.run();
    Run run{dir, r.stdout_output + r.stderr_output, {}};
    std::ofstream(d / "log.txt") << run.log;
    if (r.cancelled) throw std::runtime_error("the simulation was stopped (" + dir + ")");
    if (r.timed_out) throw std::runtime_error("the simulation timed out after " + std::to_string(timeout.count()) + " s (" + dir + ")" + tail(run.log, 5));
    if (r.exit_code != 0)
        throw std::runtime_error("the simulation failed (treespice exit " + std::to_string(r.exit_code) + ", " + dir + "):" + tail(run.log, 8));
    run.plots = readRaw((d / "out.raw").string());
    if (run.plots.empty())
        throw std::runtime_error("the simulation wrote no results (treespice exit " + std::to_string(r.exit_code) + ", " + dir + "):" + tail(run.log, 8));
    return run;
}

}  // namespace sim
