#pragma once
// ADE Explorer's model (docs/rebuild.md, M1): a setup (a maestro view's `setup` property) expands into points (each
// enabled test × the nominal corner and each enabled corner × each combination of swept variables), each netlisted,
// run on TreeSPICE, and its outputs evaluated by the calculator against their specs. A run is a history
// (Interactive.N) on disk: history.json and a directory per point.
//
// The setup:
//   tests        [{name, design: "lib/cell/view" (a schematic or config), enabled (true), analyses: [{type, ...}],
//                  models: [{file, section ("": .include)}], options: {reltol: "1e-4", ...}}]
//     analyses   op; dc {source, start, stop, step}; ac {sweep (dec|oct|lin), points, start, stop};
//                tran {step, stop [, start, maxstep]}; noise {output, source, sweep, points, start, stop};
//                tf {output, source}; sens {output}; pz {in1, in2, out1, out2, transfer (vol|cur), kind (pol|zer|pz)}
//   variables    {name: "1k"}; a space-separated list ("1k 2k 5k") is a parametric sweep. They are .params, so an
//                instance parameter "{name}" uses one.
//   temperature  "27" (the nominal corner's)
//   nominal      true: run the nominal corner too
//   corners      [{name, enabled (true), temperature, variables: {name: "value"}, models: [{file, section}]}]: a
//                corner's model file replaces the test's of the same file
//   outputs      [{name, test, expression (sim/calc.h), min, max}]: a spec passes when min <= value <= max
//
// history.json: {name, status (running, done, stopped), points: [...]}. A point records every parameter it ran with
// (test, design, corner, variables, temperature, models, options, analyses; its deck is dir/input.cir), its status
// (pending, running, done, error, stopped) and error, and its outputs: {name: {value | error, equation, steps, pass,
// spec}}. An output's value is a number, {re, im}, or {wave: points}; its equation is the expression = the value, its
// steps the calculator's derivation (sim/calc.h), its spec the comparison that passed or failed.
#include "oadb/oadb.h"

#include <QJsonObject>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sim {

void validateSetup(const QJsonObject& setup);  // throws, naming what is wrong

struct Point {
    QJsonObject info;     // index, test, corner, variables, temperature, dir (as history.json lists it)
    std::string netlist;  // the complete deck
};

struct Job {
    std::string dir;  // the history's directory
    QString name;     // Interactive.N
    QJsonObject setup;
    std::vector<Point> points;
};

// Netlists every point of lib/cell/view's setup (this reads the database: call it on the database's thread) into a
// new history under root, and writes history.json (status running, every point pending)
Job prepare(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view, const std::string& root);

// Runs the points, several at once (no database: any thread), evaluates their outputs, and rewrites history.json as
// each finishes. A set `cancel` kills the runs in flight; what did not finish is "stopped". Returns the history.
QJsonObject execute(const Job& job, const std::string& simulator, std::chrono::seconds timeout, const std::atomic<bool>* cancel);

QJsonObject readHistory(const std::string& dir);

// Background runs, one thread each, by history directory; destroying it stops every run and waits for it
class Jobs {
public:
    ~Jobs();
    void start(const std::string& id, std::function<void(const std::atomic<bool>& cancel)> work);
    bool cancel(const std::string& id);  // false: no such run (finished runs ignore it)

private:
    struct Run {
        std::atomic<bool> cancel{false};
        std::thread thread;
    };
    std::mutex m_;
    std::map<std::string, std::unique_ptr<Run>> runs_;
};

}  // namespace sim
