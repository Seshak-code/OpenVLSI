#pragma once
// ============================================================================
// scenario_runner.h — JSON DSL scenario runner for IGuiDriver
// ============================================================================
// Reads JSON scenario files and dispatches operations to an IGuiDriver.
// Scenario format documented in tests/scenarios/*.json.
// ============================================================================
#include "iguidriver.h"
#include <string>
#include <vector>

// Forward-declare Qt type so the header stays slim; QJsonObject used in .cpp only.
class QJsonObject;

namespace acutesim::testing {

struct StepResult {
    int         stepIndex  = 0;
    std::string op;
    bool        passed     = true;
    std::string failReason;
};

struct ScenarioResult {
    std::string             scenarioName;
    bool                    passed = true;
    std::vector<StepResult> steps;

    int passCount() const {
        int n = 0;
        for (const auto& s : steps) n += s.passed ? 1 : 0;
        return n;
    }
    int failCount() const {
        int n = 0;
        for (const auto& s : steps) n += s.passed ? 0 : 1;
        return n;
    }
};

class ScenarioRunner {
public:
    explicit ScenarioRunner(IGuiDriver& driver);

    // Run a scenario from a JSON file path.
    ScenarioResult runFile(const std::string& jsonPath);

    // Run a scenario from a JSON string.
    ScenarioResult runJson(const std::string& jsonText);

private:
    IGuiDriver& driver_;

    StepResult executeStep(const QJsonObject& step,
                           std::unordered_map<std::string, ComponentHandle>& components,
                           int stepIndex);
};

} // namespace acutesim::testing
