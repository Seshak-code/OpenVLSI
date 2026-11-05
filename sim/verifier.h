#pragma once
// ADE Verifier: requirements traced to Assembler's results. A verifier view holds requirements, in a tree; each leaf maps
// to outputs of ADE views. Checking one reads the mapped view's history and says whether it proves the requirement.
//
// setup: {"requirements": [{"id", "title", "parent"?, "min"?, "max"?, "minYield"?,
//                           "mappings": [{"library", "cell", "view", "test", "output", "history"?}]}]}
// min and max, when given, replace the output's own spec; minYield asks that share of a Monte Carlo history's samples to
// meet the spec. history names one run; without it, the view's latest.
//
// A mapping's status, worst first: fail, error (a point did not simulate, or the output or test is gone), stale (the view's
// setup changed since that history ran), noSpec, notRun; else pass. Nothing counts as verified that was not measured:
// a stopped run, a failed point and a changed setup all withhold pass. A parent takes its worst child's status.
#include <QJsonObject>

#include <functional>
#include <string>

namespace sim {

void validateRequirements(const QJsonObject& setup);  // throws why: a duplicate id, an unknown parent, a cycle, a bad mapping

// historyOf(mapping): that mapping's history as readHistory gives it, or {} when none exists. setupOf(mapping): the view's
// current setup, or {} when the view is gone.
QJsonObject evaluateRequirements(const QJsonObject& setup, const std::function<QJsonObject(const QJsonObject&)>& historyOf,
                                 const std::function<QJsonObject(const QJsonObject&)>& setupOf);

}  // namespace sim
