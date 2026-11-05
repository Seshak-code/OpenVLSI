#include "sim/verifier.h"

#include "sim/ade.h"

#include <QJsonArray>
#include <QString>

#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace sim {

namespace {

int rank(const QString& status) {
    static const std::map<QString, int> order = {{"pass", 0}, {"unmapped", 1}, {"notRun", 2}, {"noSpec", 3}, {"stale", 4}, {"error", 5}, {"fail", 6}};
    return order.at(status);
}

QString number(double v) { return QString::number(v, 'g', 6); }

QJsonObject checkMapping(const QJsonObject& req, const QJsonObject& mapping, const QJsonObject& history, const QJsonObject& current) {
    QJsonObject out = mapping;
    const QString name = mapping["library"].toString() + "/" + mapping["cell"].toString() + "/" + mapping["view"].toString() + " " + mapping["test"].toString() + "." + mapping["output"].toString();
    const auto done = [&](const char* status, const QString& why) {
        out["status"] = status;
        out["derivation"] = name + ": " + why;
        return out;
    };
    if (current.isEmpty()) return done("error", "the view has no setup");
    bool hasOutput = false;
    for (const auto& o : current["outputs"].toArray())
        hasOutput = hasOutput || (o.toObject()["test"] == mapping["test"] && o.toObject()["name"] == mapping["output"]);
    if (!hasOutput) return done("error", "the view's setup has no such output");
    if (history.isEmpty()) return done("notRun", "no history yet");
    out["history"] = history["name"];
    const QString runStatus = history["status"].toString();
    if (runStatus != "done") return done(runStatus == "stopped" || runStatus == "failed" ? "error" : "notRun", "history " + history["name"].toString() + " is " + runStatus);
    if (history["setup"].toObject() != current) return done("stale", "the setup changed after history " + history["name"].toString() + " ran");

    int failedPoints = 0;
    for (const auto& p : history["points"].toArray())
        if (p.toObject()["test"] == mapping["test"] && p.toObject()["status"] != "done") ++failedPoints;
    if (failedPoints) return done("error", QString::number(failedPoints) + " of its points did not simulate");

    const QJsonObject summ = summary(history);
    QJsonObject entry;
    for (const auto& o : summ["outputs"].toArray())
        if (o.toObject()["test"] == mapping["test"] && o.toObject()["output"] == mapping["output"]) entry = o.toObject();
    if (!entry.isEmpty() && entry["errors"].toInt()) return done("error", QString::number(entry["errors"].toInt()) + " of its values could not be computed");
    if (entry.isEmpty() || entry["n"].toInt() == 0) return done("notRun", "history " + history["name"].toString() + " holds no value of it");

    const double lo = entry["min"].toDouble(), hi = entry["max"].toDouble();
    out["n"] = entry["n"];
    out["low"] = lo;
    out["high"] = hi;
    const QString range = QString::number(entry["n"].toInt()) + " points from " + number(lo) + " to " + number(hi);

    if (req.contains("min") || req.contains("max")) {
        double margin = HUGE_VAL;
        if (req.contains("min")) margin = std::min(margin, lo - req["min"].toDouble());
        if (req.contains("max")) margin = std::min(margin, req["max"].toDouble() - hi);
        out["margin"] = margin;
        return done(margin >= 0 ? "pass" : "fail", range + "; required " + (req.contains("min") ? "at least " + number(req["min"].toDouble()) + " " : "") +
                                                       (req.contains("max") ? "at most " + number(req["max"].toDouble()) : "") + ", margin " + number(margin));
    }
    if (!entry.contains("pass")) return done("noSpec", range + "; neither the requirement nor the output gives a spec");
    const int pass = entry["pass"].toInt(), fail = entry["fail"].toInt();
    if (entry.contains("worst")) out["margin"] = entry["worst"].toObject()["margin"];
    if (req.contains("minYield")) {
        const double yield = entry["yield"].toDouble();
        out["yield"] = yield;
        return done(yield >= req["minYield"].toDouble() ? "pass" : "fail", range + "; yield " + number(yield) + " against " + number(req["minYield"].toDouble()) + " required");
    }
    return done(fail ? "fail" : "pass", range + "; its own spec: " + QString::number(pass) + " pass, " + QString::number(fail) + " fail");
}

}  // namespace

void validateRequirements(const QJsonObject& setup) {
    for (auto it = setup.begin(); it != setup.end(); ++it)
        if (it.key() != "requirements") throw std::invalid_argument("a verifier setup has no field '" + it.key().toStdString() + "'");
    std::set<QString> ids;
    const QJsonArray reqs = setup["requirements"].toArray();
    for (const auto& v : reqs) {
        const QJsonObject r = v.toObject();
        const QString id = r["id"].toString();
        if (id.isEmpty()) throw std::invalid_argument("a requirement needs an id");
        if (!ids.insert(id).second) throw std::invalid_argument("requirement id '" + id.toStdString() + "' appears twice");
        for (const char* k : {"min", "max", "minYield"})
            if (r.contains(k) && !r[k].isDouble()) throw std::invalid_argument("requirement " + id.toStdString() + "'s " + k + " must be a number");
        if (r.contains("min") && r.contains("max") && r["min"].toDouble() > r["max"].toDouble()) throw std::invalid_argument("requirement " + id.toStdString() + " has min above max");
        if (r.contains("minYield") && (r["minYield"].toDouble() < 0 || r["minYield"].toDouble() > 1)) throw std::invalid_argument("requirement " + id.toStdString() + "'s minYield is a share, 0 to 1");
        if (r.contains("minYield") && (r.contains("min") || r.contains("max"))) throw std::invalid_argument("requirement " + id.toStdString() + ": minYield applies to the output's own spec, so not with min or max");
        for (const auto& m : r["mappings"].toArray())
            for (const char* k : {"library", "cell", "view", "test", "output"})
                if (m.toObject()[k].toString().isEmpty()) throw std::invalid_argument("requirement " + id.toStdString() + " has a mapping without its " + k);
    }
    std::map<QString, QString> parents;
    for (const auto& v : reqs) {
        const QJsonObject r = v.toObject();
        if (!r.contains("parent")) continue;
        if (!ids.count(r["parent"].toString())) throw std::invalid_argument("requirement " + r["id"].toString().toStdString() + " has an unknown parent");
        parents[r["id"].toString()] = r["parent"].toString();
    }
    for (const auto& [id, parent] : parents) {
        std::set<QString> seen{id};
        for (QString up = parent; !up.isEmpty(); up = parents.count(up) ? parents[up] : QString())
            if (!seen.insert(up).second) throw std::invalid_argument("requirement " + id.toStdString() + " is its own ancestor");
    }
}

QJsonObject evaluateRequirements(const QJsonObject& setup, const std::function<QJsonObject(const QJsonObject&)>& historyOf,
                                 const std::function<QJsonObject(const QJsonObject&)>& setupOf) {
    validateRequirements(setup);
    const QJsonArray reqs = setup["requirements"].toArray();
    std::map<QString, std::vector<QString>> children;
    std::map<QString, QJsonObject> byId;
    for (const auto& v : reqs) {
        const QJsonObject r = v.toObject();
        byId[r["id"].toString()] = r;
        if (r.contains("parent")) children[r["parent"].toString()].push_back(r["id"].toString());
    }
    std::map<QString, QJsonObject> results;
    std::function<QString(const QString&)> statusOf = [&](const QString& id) -> QString {
        if (const auto done = results.find(id); done != results.end()) return done->second["status"].toString();
        const QJsonObject& r = byId[id];
        QJsonObject result = r;
        QString worst = "unmapped";
        bool any = false;
        const auto take = [&](const QString& status) {
            worst = !any || rank(status) > rank(worst) ? status : worst;
            any = true;
        };
        QJsonArray mapped;
        for (const auto& m : r["mappings"].toArray()) {
            const QJsonObject mapping = m.toObject();
            const QJsonObject checked = checkMapping(r, mapping, historyOf(mapping), setupOf(mapping));
            mapped.append(checked);
            take(checked["status"].toString());
        }
        result["mappings"] = mapped;
        const auto kids = children.find(id);
        if (kids != children.end())
            for (const QString& kid : kids->second) take(statusOf(kid));
        result["status"] = worst;
        results[id] = result;
        return worst;
    };
    QJsonArray list;
    std::map<QString, int> counts;
    int leaves = 0, mappedLeaves = 0;
    for (const auto& v : reqs) {
        const QString id = v.toObject()["id"].toString();
        const QString status = statusOf(id);
        list.append(results[id]);
        if (children.count(id)) continue;
        ++leaves;
        ++counts[status];
        if (status != "unmapped") ++mappedLeaves;
    }
    QJsonObject countJson;
    for (const auto& [k, n] : counts) countJson[k] = n;
    return {{"requirements", list}, {"counts", countJson}, {"leaves", leaves}, {"mapped", mappedLeaves}, {"verified", counts["pass"]},
            {"complete", leaves > 0 && counts["pass"] == leaves}};
}

}  // namespace sim
