// ============================================================================
// test_gui_headless_scenarios.cpp — Phase 5 GUI testing framework entry point
// ============================================================================
// Discovers JSON scenarios from SCENARIOS_DIR and runs them via HeadlessDriver.
// HeadlessDriver builds TensorNetlist directly — no display, no Qt::Widgets.
// Qt::Core is used only for JSON parsing in ScenarioRunner.
//
// Build: cmake --build build --target test_gui_headless_scenarios
// Run:   cd build && ctest -R test_gui_headless_scenarios --verbose
// ============================================================================

#include <QCoreApplication>

#include "gui_driver/headless_driver.h"
#include "gui_driver/scenario_runner.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef SCENARIOS_DIR
#define SCENARIOS_DIR "tests/scenarios"
#endif

int main(int argc, char* argv[])
{
    // QCoreApplication needed for Qt::Core JSON parsing in ScenarioRunner.
    QCoreApplication app(argc, argv);

    acutesim::testing::HeadlessDriver driver;
    acutesim::testing::ScenarioRunner runner(driver);

    const std::vector<std::string> scenarioFiles = {
        SCENARIOS_DIR "/rc_lowpass_dc.json",
        SCENARIOS_DIR "/voltage_divider_dc.json",
    };

    int totalPass = 0;
    int totalFail = 0;

    for (const auto& file : scenarioFiles) {
        driver.reset();

        auto result = runner.runFile(file);

        std::printf("\n── Scenario: %s ──\n", result.scenarioName.c_str());

        for (const auto& step : result.steps) {
            if (step.passed) {
                std::printf("  PASS [%d] %s\n", step.stepIndex, step.op.c_str());
                ++totalPass;
            } else {
                std::printf("  FAIL [%d] %s — %s\n",
                    step.stepIndex, step.op.c_str(), step.failReason.c_str());
                ++totalFail;
            }
        }

        std::printf("  Result: %s (%d passed, %d failed)\n",
            result.passed ? "PASS" : "FAIL",
            result.passCount(), result.failCount());
    }

    std::printf("\n══════════════════════════════════════\n");
    std::printf("Total: %d passed, %d failed\n", totalPass, totalFail);

    return totalFail > 0 ? 1 : 0;
}
