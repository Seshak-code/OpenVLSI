#pragma once
// ============================================================================
// iguidriver.h — Pure C++ abstract interface for GUI automation
// ============================================================================
// ARCHITECTURAL RULE: No Qt headers. No project headers. Only stdlib.
// This interface compiles against both Qt-desktop and WASM/web drivers.
// ============================================================================
#include <string>
#include <vector>
#include <unordered_map>

namespace acutesim::testing {

struct ComponentHandle { std::string id; };
struct WireHandle     { std::string id; };
struct Point          { double x = 0; double y = 0; };

class IGuiDriver {
public:
    virtual ~IGuiDriver() = default;

    // ── Schematic Authoring ─────────────────────────────────────────────────
    // typeStr must match componentTypeToString() values, e.g. "Resistor", "VoltageSource"
    virtual ComponentHandle placeComponent(
        const std::string& typeStr,
        Point pos,
        double rotDeg,
        const std::unordered_map<std::string, std::string>& params) = 0;

    virtual bool moveComponent(ComponentHandle h, Point newPos) = 0;
    virtual bool rotateComponent(ComponentHandle h, double deltaDeg) = 0;
    virtual bool deleteComponent(ComponentHandle h) = 0;

    // Connect two points with a wire
    virtual WireHandle drawWire(Point from, Point to) = 0;

    // ── Simulation ──────────────────────────────────────────────────────────
    // analysisType: "DC", "TRANSIENT", "AC", etc.
    // params: e.g. {"stopTime":"1e-6","stepTime":"1e-9"}
    virtual bool runSimulation(
        const std::string& analysisType,
        const std::unordered_map<std::string, std::string>& params) = 0;

    // Blocks until simulation completes or timeoutMs elapses.
    // Returns true if simulation completed successfully.
    virtual bool waitForSimulationComplete(int timeoutMs) = 0;

    // Read probe values after simulation. Returns empty if probe not found.
    virtual std::vector<double> readProbe(const std::string& probeName) = 0;

    // ── Assertions ──────────────────────────────────────────────────────────
    virtual bool ercClean() = 0;
    virtual int componentCount() = 0;
    virtual std::string schematicHash() = 0;   // hex string of topology hash
    virtual void reset() = 0;                   // clear schematic + results
};

} // namespace acutesim::testing
