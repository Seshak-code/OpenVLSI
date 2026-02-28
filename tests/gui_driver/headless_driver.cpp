// ============================================================================
// headless_driver.cpp — Pure-C++ HeadlessDriver (no GridScene, no Qt::Widgets)
// ============================================================================
// Uses TensorNetlist + VerilogAGenerator + LocalWorkerDriver directly.
// Connectivity is resolved via explicit n+/n- net-name params on each component.
// This avoids the full SchematicEditor dependency chain.
// ============================================================================
#include "headless_driver.h"

#include "local_worker_driver.h"

#include "acutesim_engine/netlist/circuit.h"          // TensorNetlist
#include "components/veriloga_generator.h"      // VerilogAGenerator

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <vector>

using namespace acutesim::compute::orchestration;

namespace acutesim::testing {

// ── Pimpl ─────────────────────────────────────────────────────────────────────

struct HeadlessDriver::Impl {
    struct PlacedComponent {
        std::string id;
        std::string type;
        double cx = 0, cy = 0, rot = 0;
        std::unordered_map<std::string, std::string> params;
    };

    std::unordered_map<std::string, PlacedComponent> components;
    std::vector<std::pair<std::pair<int,int>, std::pair<int,int>>> wires;
    int nextId = 1;

    LocalWorkerDriver                driver;
    SimulationResponseDTO            lastResult;
    std::mutex                       resultMu;
    std::condition_variable          resultCv;
    std::atomic<bool>                resultReady{false};

    // ── Build TensorNetlist from placed components ─────────────────────────
    // Uses explicit n+/n- params from component params when present.
    // Falls back to position-based union-find only for components without net names.
    TensorNetlist buildNetlist() const {
        // ── Collect all named nets, assign indices ─────────────────────────
        // "gnd" (and the net of any Ground component) → node 0.
        std::unordered_map<std::string, int> netIndex;
        netIndex["gnd"] = 0;
        netIndex["GND"] = 0;
        netIndex["0"]   = 0;
        int nextNode = 1;

        // Scan Ground components: their "net" param (or id) also maps to 0.
        for (const auto& [id, comp] : components) {
            if (comp.type == "Ground") {
                auto it = comp.params.find("net");
                std::string netName = (it != comp.params.end() && !it->second.empty())
                                      ? it->second : id;
                netIndex[netName] = 0;
            }
        }

        auto getNetNode = [&](const std::string& netName) -> int {
            if (netName.empty()) {
                // unnamed net: assign new node
                return nextNode++;
            }
            auto it = netIndex.find(netName);
            if (it != netIndex.end()) return it->second;
            int n = nextNode++;
            netIndex[netName] = n;
            return n;
        };

        auto getParam = [](const std::unordered_map<std::string, std::string>& params,
                           const std::string& key, double def) -> double {
            auto it = params.find(key);
            if (it == params.end()) return def;
            try { return std::stod(it->second); } catch (...) { return def; }
        };

        auto getStrParam = [](const std::unordered_map<std::string, std::string>& params,
                              const std::string& key) -> std::string {
            auto it = params.find(key);
            return (it != params.end()) ? it->second : "";
        };

        // ── Build TensorNetlist ────────────────────────────────────────────
        TensorNetlist nl;

        for (const auto& [id, comp] : components) {
            if (comp.type == "Ground") continue; // no electrical element

            // Prefer explicit n+/n- net names; fall back to unnamed nodes.
            std::string netPos = getStrParam(comp.params, "n+");
            std::string netNeg = getStrParam(comp.params, "n-");

            int nodeP = getNetNode(netPos);
            int nodeN = getNetNode(netNeg);

            if (comp.type == "Resistor") {
                double r = getParam(comp.params, "resistance", 1000.0);
                nl.addResistor(id, nodeP, nodeN, r);
            } else if (comp.type == "Capacitor") {
                double c = getParam(comp.params, "capacitance", 1e-6);
                nl.addCapacitor(id, nodeP, nodeN, c);
            } else if (comp.type == "Inductor") {
                double l = getParam(comp.params, "inductance", 1e-3);
                nl.addInductor(id, nodeP, nodeN, l);
            } else if (comp.type == "VoltageSource") {
                double v = getParam(comp.params, "voltage", 0.0);
                nl.addVoltageSource(nodeP, nodeN, v);
            } else if (comp.type == "CurrentSource") {
                double i = getParam(comp.params, "current", 0.0);
                nl.addCurrentSource(nodeP, nodeN, i);
            } else if (comp.type == "Diode" || comp.type == "SchottkyDiode") {
                double Is = getParam(comp.params, "Is", 1e-14);
                nl.addDiode(id, nodeP, nodeN, Is);
            } else if (comp.type == "Probe") {
                // Probe measures n+ vs GND (node 0)
                nl.addProbe(id, nodeP, 0);
            }
        }

        nl.numGlobalNodes = nextNode > 1 ? nextNode : 2;
        return nl;
    }
};

// ── Constructor / Destructor ──────────────────────────────────────────────────

HeadlessDriver::HeadlessDriver()
    : impl_(std::make_unique<Impl>())
{
    impl_->driver.onJobCompleted =
        [this](JobID /*jobId*/, const SimulationResponseDTO& r)
    {
        std::lock_guard<std::mutex> lk(impl_->resultMu);
        impl_->lastResult  = r;
        impl_->resultReady = true;
        impl_->resultCv.notify_one();
    };
}

HeadlessDriver::~HeadlessDriver() = default;

// ── IGuiDriver implementation ─────────────────────────────────────────────────

ComponentHandle HeadlessDriver::placeComponent(
    const std::string& typeStr,
    Point pos,
    double rotDeg,
    const std::unordered_map<std::string, std::string>& params)
{
    // Prefer the scenario-supplied id so probe names match in the simulation result.
    std::string id;
    auto it = params.find("_scenarioId");
    if (it != params.end() && !it->second.empty()) {
        id = it->second;
    } else {
        id = typeStr + "_" + std::to_string(impl_->nextId++);
    }
    ++impl_->nextId;
    impl_->components[id] = { id, typeStr, pos.x, pos.y, rotDeg, params };
    return ComponentHandle{id};
}

bool HeadlessDriver::moveComponent(ComponentHandle h, Point newPos)
{
    auto it = impl_->components.find(h.id);
    if (it == impl_->components.end()) return false;
    it->second.cx = newPos.x;
    it->second.cy = newPos.y;
    return true;
}

bool HeadlessDriver::rotateComponent(ComponentHandle h, double deltaDeg)
{
    auto it = impl_->components.find(h.id);
    if (it == impl_->components.end()) return false;
    it->second.rot += deltaDeg;
    return true;
}

bool HeadlessDriver::deleteComponent(ComponentHandle h)
{
    return impl_->components.erase(h.id) > 0;
}

WireHandle HeadlessDriver::drawWire(Point from, Point to)
{
    impl_->wires.push_back({ {(int)from.x, (int)from.y}, {(int)to.x, (int)to.y} });
    return WireHandle{std::to_string(impl_->wires.size())};
}

bool HeadlessDriver::runSimulation(
    const std::string& analysisType,
    const std::unordered_map<std::string, std::string>& params)
{
    std::lock_guard<std::mutex> lk(impl_->resultMu);
    impl_->resultReady = false;

    // Build netlist from current schematic state
    TensorNetlist nl = impl_->buildNetlist();

    SimulationRequestDTO req;
    req.source.format  = NetlistFormatDTO::VERILOG_A;
    req.source.payload = VerilogAGenerator::generate(nl);

    if      (analysisType == "DC")        req.analysis = AnalysisTypeDTO::DC;
    else if (analysisType == "TRANSIENT") req.analysis = AnalysisTypeDTO::TRANSIENT;
    else if (analysisType == "AC")        req.analysis = AnalysisTypeDTO::AC;
    else if (analysisType == "NOISE")     req.analysis = AnalysisTypeDTO::NOISE;
    else                                  req.analysis = AnalysisTypeDTO::DC;

    req.parameters = params;

    impl_->driver.submitJob(req);
    return true;
}

bool HeadlessDriver::waitForSimulationComplete(int timeoutMs)
{
    std::unique_lock<std::mutex> lk(impl_->resultMu);
    impl_->resultCv.wait_for(lk,
        std::chrono::milliseconds(timeoutMs),
        [this] { return impl_->resultReady.load(); });
    return impl_->lastResult.success;
}

std::vector<double> HeadlessDriver::readProbe(const std::string& probeName)
{
    auto it = impl_->lastResult.probeValues.find(probeName);
    if (it == impl_->lastResult.probeValues.end()) return {};
    return it->second;
}

bool HeadlessDriver::ercClean()
{
    // Simple ERC: check there's at least one voltage source and one ground
    bool hasVsrc = false, hasGnd = false;
    for (const auto& [id, comp] : impl_->components) {
        if (comp.type == "VoltageSource" || comp.type == "CurrentSource") hasVsrc = true;
        if (comp.type == "Ground") hasGnd = true;
    }
    return hasVsrc && hasGnd;
}

int HeadlessDriver::componentCount()
{
    return static_cast<int>(impl_->components.size());
}

std::string HeadlessDriver::schematicHash()
{
    // Simple hash: XOR of component positions and types
    uint64_t h = 0xcbf29ce484222325ULL; // FNV offset
    for (const auto& [id, comp] : impl_->components) {
        for (char c : comp.type) h = (h ^ c) * 0x100000001b3ULL;
        h ^= static_cast<uint64_t>(comp.cx * 1000);
        h ^= static_cast<uint64_t>(comp.cy * 1000);
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << h;
    return oss.str();
}

void HeadlessDriver::reset()
{
    impl_->components.clear();
    impl_->wires.clear();
    impl_->nextId = 1;
    std::lock_guard<std::mutex> lk(impl_->resultMu);
    impl_->lastResult  = {};
    impl_->resultReady = false;
}

} // namespace acutesim::testing
