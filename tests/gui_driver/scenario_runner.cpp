// ============================================================================
// scenario_runner.cpp — JSON DSL → IGuiDriver dispatcher
// ============================================================================
#include "scenario_runner.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cmath>
#include <stdexcept>

namespace acutesim::testing {

// ── Helpers ──────────────────────────────────────────────────────────────────

static Point pointFromArray(const QJsonArray& arr)
{
    Point p;
    if (arr.size() >= 2) {
        p.x = arr[0].toDouble();
        p.y = arr[1].toDouble();
    }
    return p;
}

static std::unordered_map<std::string, std::string> paramsFromObject(const QJsonObject& obj)
{
    std::unordered_map<std::string, std::string> out;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        out[it.key().toStdString()] = it.value().toVariant().toString().toStdString();
    }
    return out;
}

// ── ScenarioRunner ────────────────────────────────────────────────────────────

ScenarioRunner::ScenarioRunner(IGuiDriver& driver)
    : driver_(driver)
{}

ScenarioResult ScenarioRunner::runJson(const std::string& jsonText)
{
    ScenarioResult result;

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(jsonText), &err);
    if (doc.isNull() || !doc.isObject()) {
        result.passed = false;
        StepResult s;
        s.op = "parse";
        s.passed = false;
        s.failReason = "JSON parse error: " + err.errorString().toStdString();
        result.steps.push_back(s);
        return result;
    }

    QJsonObject root = doc.object();
    result.scenarioName = root["scenario"].toString("unnamed").toStdString();

    QJsonArray steps = root["steps"].toArray();
    std::unordered_map<std::string, ComponentHandle> components;

    for (int i = 0; i < steps.size(); ++i) {
        QJsonObject step = steps[i].toObject();
        StepResult sr = executeStep(step, components, i);
        if (!sr.passed) result.passed = false;
        result.steps.push_back(sr);
    }

    return result;
}

ScenarioResult ScenarioRunner::runFile(const std::string& jsonPath)
{
    ScenarioResult result;

    QFile f(QString::fromStdString(jsonPath));
    if (!f.open(QIODevice::ReadOnly)) {
        result.passed = false;
        StepResult s;
        s.op = "open_file";
        s.passed = false;
        s.failReason = "Cannot open file: " + jsonPath;
        result.steps.push_back(s);
        return result;
    }

    return runJson(f.readAll().toStdString());
}

StepResult ScenarioRunner::executeStep(
    const QJsonObject& step,
    std::unordered_map<std::string, ComponentHandle>& components,
    int stepIndex)
{
    StepResult sr;
    sr.stepIndex = stepIndex;
    sr.op        = step["op"].toString("unknown").toStdString();

    // ── placeComponent ──────────────────────────────────────────────────────
    if (sr.op == "placeComponent") {
        std::string typeStr = step["type"].toString().toStdString();
        Point pos = pointFromArray(step["pos"].toArray());
        double rot = step["rot"].toDouble(0.0);
        auto params = paramsFromObject(step["params"].toObject());

        // Pass the scenario "id" as _scenarioId param so the driver can use it
        // as the component/probe name in the simulation netlist.
        std::string scenarioId = step["id"].toString().toStdString();
        if (!scenarioId.empty()) params["_scenarioId"] = scenarioId;

        auto handle = driver_.placeComponent(typeStr, pos, rot, params);
        sr.passed = !handle.id.empty();
        if (!sr.passed) {
            sr.failReason = "placeComponent returned empty id for type: " + typeStr;
        } else {
            // Store under the scenario-level id key (for later ref/move/delete)
            if (!scenarioId.empty()) {
                components[scenarioId] = handle;
            }
        }
        return sr;
    }

    // ── moveComponent ───────────────────────────────────────────────────────
    if (sr.op == "moveComponent") {
        std::string ref = step["ref"].toString().toStdString();
        auto it = components.find(ref);
        if (it == components.end()) {
            sr.passed = false;
            sr.failReason = "moveComponent: unknown component ref '" + ref + "'";
            return sr;
        }
        Point pos = pointFromArray(step["pos"].toArray());
        sr.passed = driver_.moveComponent(it->second, pos);
        if (!sr.passed) sr.failReason = "moveComponent returned false";
        return sr;
    }

    // ── rotateComponent ─────────────────────────────────────────────────────
    if (sr.op == "rotateComponent") {
        std::string ref = step["ref"].toString().toStdString();
        auto it = components.find(ref);
        if (it == components.end()) {
            sr.passed = false;
            sr.failReason = "rotateComponent: unknown component ref '" + ref + "'";
            return sr;
        }
        double deg = step["deg"].toDouble(90.0);
        sr.passed = driver_.rotateComponent(it->second, deg);
        if (!sr.passed) sr.failReason = "rotateComponent returned false";
        return sr;
    }

    // ── deleteComponent ─────────────────────────────────────────────────────
    if (sr.op == "deleteComponent") {
        std::string ref = step["ref"].toString().toStdString();
        auto it = components.find(ref);
        if (it == components.end()) {
            sr.passed = false;
            sr.failReason = "deleteComponent: unknown component ref '" + ref + "'";
            return sr;
        }
        sr.passed = driver_.deleteComponent(it->second);
        if (!sr.passed) sr.failReason = "deleteComponent returned false";
        else components.erase(it);
        return sr;
    }

    // ── drawWire ────────────────────────────────────────────────────────────
    if (sr.op == "drawWire") {
        Point from = pointFromArray(step["from"].toArray());
        Point to   = pointFromArray(step["to"].toArray());
        auto wire = driver_.drawWire(from, to);
        // drawWire always "succeeds" (no error path in GridScene::placeWirePath)
        sr.passed = true;
        (void)wire;
        return sr;
    }

    // ── runSimulation ───────────────────────────────────────────────────────
    if (sr.op == "runSimulation") {
        std::string analysis = step["analysis"].toString("DC").toStdString();
        auto params = paramsFromObject(step["params"].toObject());

        bool submitted = driver_.runSimulation(analysis, params);
        if (!submitted) {
            sr.passed = false;
            sr.failReason = "runSimulation: submitJob failed";
            return sr;
        }
        int timeout = step["timeoutMs"].toInt(10000);
        bool ok = driver_.waitForSimulationComplete(timeout);
        sr.passed = ok;
        if (!ok) sr.failReason = "runSimulation: timed out or simulation failed";
        return sr;
    }

    // ── assertErcClean ──────────────────────────────────────────────────────
    if (sr.op == "assertErcClean") {
        sr.passed = driver_.ercClean();
        if (!sr.passed) sr.failReason = "ERC returned errors";
        return sr;
    }

    // ── assertComponentCount ────────────────────────────────────────────────
    if (sr.op == "assertComponentCount") {
        int expected = step["expected"].toInt(0);
        int actual   = driver_.componentCount();
        sr.passed = (actual == expected);
        if (!sr.passed) {
            sr.failReason = "componentCount: expected " + std::to_string(expected)
                          + ", got " + std::to_string(actual);
        }
        return sr;
    }

    // ── assertProbe ─────────────────────────────────────────────────────────
    if (sr.op == "assertProbe") {
        std::string probe = step["probe"].toString().toStdString();
        double expected   = step["expected"].toDouble(0.0);
        double tolerance  = step["tolerance"].toDouble(0.01);

        auto values = driver_.readProbe(probe);
        if (values.empty()) {
            sr.passed = false;
            sr.failReason = "assertProbe: no data for probe '" + probe + "'";
            return sr;
        }
        double actual = values[0];
        sr.passed = std::abs(actual - expected) <= tolerance;
        if (!sr.passed) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "assertProbe '%s': expected %.6g ± %.6g, got %.6g",
                probe.c_str(), expected, tolerance, actual);
            sr.failReason = buf;
        }
        return sr;
    }

    // ── assertProbeEmpty ────────────────────────────────────────────────────
    if (sr.op == "assertProbeEmpty") {
        std::string probe = step["probe"].toString().toStdString();
        auto values = driver_.readProbe(probe);
        sr.passed = values.empty();
        if (!sr.passed) {
            sr.failReason = "assertProbeEmpty: probe '" + probe + "' had "
                          + std::to_string(values.size()) + " values";
        }
        return sr;
    }

    // ── reset ───────────────────────────────────────────────────────────────
    if (sr.op == "reset") {
        driver_.reset();
        components.clear();
        sr.passed = true;
        return sr;
    }

    // ── Unknown op ──────────────────────────────────────────────────────────
    sr.passed = false;
    sr.failReason = "Unknown op: " + sr.op;
    return sr;
}

} // namespace acutesim::testing
