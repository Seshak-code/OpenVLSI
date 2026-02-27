#pragma once
// ============================================================================
// headless_driver.h — HeadlessDriver: GridScene(Headless) + LocalWorkerDriver
// ============================================================================
// Qt headers are in headless_driver.cpp only. This header stays Qt-free so
// that test entry points can include it without pulling in Qt dependencies.
// ============================================================================
#include "iguidriver.h"
#include <memory>

namespace acutesim::testing {

// HeadlessDriver — GridScene(Mode::Headless) + LocalWorkerDriver.
// No display required. Safe to use in CI.
class HeadlessDriver : public IGuiDriver {
public:
    HeadlessDriver();
    ~HeadlessDriver() override;

    ComponentHandle placeComponent(const std::string& typeStr, Point pos,
        double rotDeg,
        const std::unordered_map<std::string, std::string>& params) override;
    bool moveComponent(ComponentHandle h, Point newPos) override;
    bool rotateComponent(ComponentHandle h, double deltaDeg) override;
    bool deleteComponent(ComponentHandle h) override;
    WireHandle drawWire(Point from, Point to) override;

    bool runSimulation(const std::string& analysisType,
                       const std::unordered_map<std::string, std::string>& params) override;
    bool waitForSimulationComplete(int timeoutMs) override;
    std::vector<double> readProbe(const std::string& probeName) override;

    bool ercClean() override;
    int componentCount() override;
    std::string schematicHash() override;
    void reset() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace acutesim::testing
