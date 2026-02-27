#include "sim_auto_detector.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace acutesim::orchestration {

SimulationContext SimAutoDetector::detectFromActiveFile(const std::string& filePath) {
    SimulationContext ctx;
    ctx.contextId = fs::path(filePath).filename().string();

    FileRef mainRef;
    mainRef.path  = filePath;
    mainRef.isTop = true;

    SimulationType type = guessTypeFromExtension(filePath);

    // Heuristic check: deep inspection for .v files that might be Verilog-A or structural
    if (type == SimulationType::Digital) {
        if (isAnalogContent(filePath)) {
            type = SimulationType::Analog; // Verilog-A or mapped structural
        }
    }

    ctx.type = type;
    const std::string ext4 = filePath.size() >= 4 ? filePath.substr(filePath.size() - 4) : "";
    mainRef.isSchematic = (type == SimulationType::Analog && ext4 == ".sch");

    ctx.sources.push_back(mainRef);
    ctx.entryPoint = filePath;

    return ctx;
}

SimulationContext SimAutoDetector::detectFromProject(
    const std::vector<std::string>& files,
    const std::string& explicitTop)
{
    SimulationContext ctx;
    ctx.entryPoint = explicitTop;

    for (const auto& f : files) {
        FileRef ref;
        ref.path  = f;
        ref.isTop = (!explicitTop.empty() && fs::path(f).stem().string() == explicitTop);
        ctx.sources.push_back(ref);
    }

    // Pick type from the first file that isn't None
    for (const auto& f : files) {
        SimulationType t = guessTypeFromExtension(f);
        if (t != SimulationType::None) {
            ctx.type = t;
            break;
        }
    }

    if (ctx.entryPoint.empty() && !ctx.sources.empty()) {
        ctx.entryPoint = ctx.sources.front().path;
        ctx.contextId  = fs::path(ctx.entryPoint).filename().string();
    }

    return ctx;
}

SimulationType SimAutoDetector::guessTypeFromExtension(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    // normalise to lower-case (ASCII-safe)
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    if (ext == ".sch")  return SimulationType::Analog;
    if (ext == ".v" || ext == ".sv") return SimulationType::Digital;
    if (ext == ".va" || ext == ".vams") return SimulationType::Analog;
    if (ext == ".py" || ext == ".tcl")  return SimulationType::Script;

    return SimulationType::None;
}

bool SimAutoDetector::isAnalogContent(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Keywords for Verilog-A or analog behavior
    if (content.find("analog begin")  != std::string::npos) return true;
    if (content.find("electrical ")   != std::string::npos) return true;
    if (content.find("ground ")       != std::string::npos) return true;

    return false;
}

} // namespace acutesim::orchestration
