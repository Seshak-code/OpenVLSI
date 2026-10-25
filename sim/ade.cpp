#include "sim/ade.h"

#include "sim/calc.h"
#include "sim/netlist.h"
#include "sim/run.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace sim {

namespace {

namespace fs = std::filesystem;
using Strings = std::vector<std::string>;

[[noreturn]] void bad(const std::string& where, const std::string& what) { throw std::invalid_argument("setup: " + where + ": " + what); }
std::string s(const QJsonValue& v) { return v.toString().toStdString(); }
QString q(const std::string& v) { return QString::fromStdString(v); }

void keys(const QJsonObject& o, const Strings& allowed, const std::string& where) {
    for (auto it = o.begin(); it != o.end(); ++it)
        if (std::find(allowed.begin(), allowed.end(), it.key().toStdString()) == allowed.end()) bad(where, "unknown key '" + it.key().toStdString() + "'");
}
// a value SPICE reads as one word: a non-empty string without spaces
std::string word(const QJsonValue& v, const std::string& where) {
    const std::string w = s(v);
    if (!v.isString() || w.empty() || std::any_of(w.begin(), w.end(), [](unsigned char c) { return std::isspace(c) || c < 0x20 || c == '"'; }))
        bad(where, "a non-empty string without spaces or quotes");
    return w;
}
void numeric(const QJsonValue& v, const std::string& where) {
    try { siNumber(word(v, where)); } catch (const std::runtime_error&) { bad(where, "'" + s(v) + "' is not a number"); }
}
bool flag(const QJsonObject& o, const char* key, const std::string& where) {
    if (!o.contains(key)) return true;
    if (!o[key].isBool()) bad(where + "." + key, "true or false");
    return o[key].toBool();
}
bool identifier(const QString& n) { static const QRegularExpression id("^[A-Za-z_][A-Za-z0-9_]*$"); return id.match(n).hasMatch(); }

struct Fields { Strings required, optional; };
const std::map<std::string, Fields> kAnalyses = {
    {"op", {{}, {}}},
    {"dc", {{"source", "start", "stop", "step"}, {}}},
    {"ac", {{"sweep", "points", "start", "stop"}, {}}},
    {"tran", {{"step", "stop"}, {"start", "maxstep"}}},
    {"noise", {{"output", "source", "sweep", "points", "start", "stop"}, {}}},
    {"tf", {{"output", "source"}, {}}},
    {"sens", {{"output"}, {}}},
    {"pz", {{"in1", "in2", "out1", "out2", "transfer", "kind"}, {}}},
};
const std::map<std::string, Strings> kChoices = {{"sweep", {"dec", "oct", "lin"}}, {"transfer", {"vol", "cur"}}, {"kind", {"pol", "zer", "pz"}}};

// an analysis as its SPICE card (validating it)
std::string card(const QJsonObject& a, const std::string& where) {
    const std::string type = s(a["type"]);
    const auto f = kAnalyses.find(type);
    if (f == kAnalyses.end()) bad(where, "type '" + type + "' is not op, dc, ac, tran, noise, tf, sens or pz");
    Strings all = {"type"};
    all.insert(all.end(), f->second.required.begin(), f->second.required.end());
    all.insert(all.end(), f->second.optional.begin(), f->second.optional.end());
    keys(a, all, where);
    std::string c = "." + type;
    for (const auto& k : f->second.required) {
        if (!a.contains(q(k))) bad(where, type + " needs " + k);
        const std::string v = word(a[q(k)], where + "." + k);
        if (const auto ch = kChoices.find(k); ch != kChoices.end() && std::find(ch->second.begin(), ch->second.end(), v) == ch->second.end())
            bad(where + "." + k, "'" + v + "' is not one of its choices");
        c += " " + v;
    }
    for (const auto& k : f->second.optional) {
        if (!a.contains(q(k))) break;  // positional: a later one needs the earlier ones
        c += " " + word(a[q(k)], where + "." + k);
    }
    return c;
}

void validateModels(const QJsonValue& v, const std::string& where) {
    if (!v.isUndefined() && !v.isArray()) bad(where, "a list of {file, section}");
    for (int k = 0; k < v.toArray().size(); ++k) {
        const QJsonObject m = v.toArray()[k].toObject();
        const std::string w = where + "[" + std::to_string(k) + "]";
        keys(m, {"file", "section"}, w);
        if (s(m["file"]).empty() || s(m["file"]).find('"') != std::string::npos) bad(w + ".file", "a file path");
        if (m.contains("section") && !s(m["section"]).empty()) word(m["section"], w + ".section");
    }
}
void validateWords(const QJsonValue& v, const std::string& where, bool lists) {
    if (!v.isUndefined() && !v.isObject()) bad(where, "an object of name: value");
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!identifier(it.key())) bad(where, "'" + it.key().toStdString() + "' is not a name");
        if (!lists) { word(it.value(), where + "." + it.key().toStdString()); continue; }
        const QStringList items = it.value().toString().split(' ', Qt::SkipEmptyParts);
        if (!it.value().isString() || items.isEmpty()) bad(where + "." + it.key().toStdString(), "a value, or a space-separated list of them");
        for (const auto& i : items) word(i, where + "." + it.key().toStdString());
    }
}

// one combination of the swept variables per point (name order), the fixed ones as they are
std::vector<std::map<std::string, std::string>> combinations(const QJsonObject& vars, const QJsonObject& fixed) {
    std::vector<std::map<std::string, std::string>> out{{}};
    for (auto it = vars.begin(); it != vars.end(); ++it) {
        const std::string n = it.key().toStdString();
        const QStringList values = fixed.contains(it.key()) ? QStringList{fixed[it.key()].toString()} : it.value().toString().split(' ', Qt::SkipEmptyParts);
        std::vector<std::map<std::string, std::string>> next;
        for (const auto& c : out)
            for (const auto& v : values) {
                auto m = c;
                m[n] = v.toStdString();
                next.push_back(m);
            }
        out = std::move(next);
    }
    for (auto it = fixed.begin(); it != fixed.end(); ++it)
        if (!vars.contains(it.key())) for (auto& c : out) c[it.key().toStdString()] = s(it.value());
    return out;
}

void write(const std::string& dir, const QJsonObject& history) {  // whole or not at all: a reader never sees half
    const fs::path tmp = fs::path(dir) / "history.json.tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        const QByteArray b = QJsonDocument(history).toJson();
        if (!f.write(b.constData(), b.size()) || !f.flush()) throw std::runtime_error("cannot write " + tmp.string());
    }
    fs::rename(tmp, fs::path(dir) / "history.json");
}

const Plot* plot(const std::vector<Plot>& plots, const std::string& name) {
    for (const auto& p : plots) if (p.name == name) return &p;
    return nullptr;
}

Wave access(const std::vector<Plot>& plots, const std::string& kind, const std::string& arg) {
    static const std::map<std::string, std::string> plotOf = {
        {"VT", "Transient Analysis"}, {"IT", "Transient Analysis"}, {"VF", "AC Analysis"}, {"IF", "AC Analysis"},
        {"VDC", "DC transfer characteristic"}, {"IDC", "DC transfer characteristic"}, {"VOP", "Operating Point"}, {"IOP", "Operating Point"},
        {"VN", "Noise Spectral Density Curves"}, {"VNTOT", "Integrated Noise"}};
    std::string a = arg;
    for (auto& c : a) c = (char)std::tolower((unsigned char)c);
    const Plot* p = plot(plots, plotOf.at(kind));
    if (!p) throw std::runtime_error("calculator: " + kind + "(" + arg + "): the test ran no " + plotOf.at(kind));
    std::string want;
    if (kind == "VN" || kind == "VNTOT") {
        if (a != "out" && a != "in") throw std::runtime_error("calculator: " + kind + "(" + arg + "): out or in");
        want = kind == "VN" ? a + "oise_spectrum" : a + "oise_total";
    } else {
        want = std::string(kind[0] == 'V' ? "v(" : "i(") + a + ")";
    }
    for (size_t k = 0; k < p->vectors.size(); ++k) {
        std::string n = p->vectors[k];
        for (auto& c : n) c = (char)std::tolower((unsigned char)c);
        if (kind == "VNTOT" && n.size() > 2) n = n.substr(2, n.size() - 3);  // v(onoise_total) / i(inoise_total)
        if (n != want) continue;
        const bool scaled = kind != "VOP" && kind != "IOP" && kind != "VNTOT";
        Wave w;
        if (scaled) for (const auto& x : p->data[0]) w.x.push_back(x.real());
        w.y = scaled ? p->data[k] : std::vector<std::complex<double>>{p->data[k][0]};
        return w;
    }
    throw std::runtime_error("calculator: " + kind + "(" + arg + "): the " + p->name + " has no " + want);
}

QJsonObject outputs(const QJsonObject& setup, const QString& test, const std::vector<Plot>& plots) {
    QJsonObject out;
    for (const auto& v : setup["outputs"].toArray()) {
        const QJsonObject o = v.toObject();
        if (o["test"].toString() != test) continue;
        QJsonObject r;
        try {
            const Wave w = evaluate(s(o["expression"]), [&](const std::string& k, const std::string& n) { return access(plots, k, n); });
            const bool number = w.scalar(), real = number && w.y[0].imag() == 0;
            r["value"] = !number ? QJsonValue(QJsonObject{{"wave", (int)w.x.size()}})
                         : real  ? QJsonValue(w.y[0].real())
                                 : QJsonValue(QJsonObject{{"re", w.y[0].real()}, {"im", w.y[0].imag()}});
            const bool spec = !s(o["min"]).empty() || !s(o["max"]).empty();
            if (spec && !real) throw std::runtime_error("a spec needs a real number, and " + s(o["expression"]) + " is not one");
            if (spec) {
                const double x = w.y[0].real();
                r["pass"] = (s(o["min"]).empty() || x >= siNumber(s(o["min"]))) && (s(o["max"]).empty() || x <= siNumber(s(o["max"])));
            }
        } catch (const std::exception& e) {
            r = QJsonObject{{"error", QString::fromUtf8(e.what())}};
        }
        out[o["name"].toString()] = r;
    }
    return out;
}

}  // namespace

void validateSetup(const QJsonObject& setup) {
    keys(setup, {"tests", "variables", "temperature", "nominal", "corners", "outputs"}, "setup");
    if (setup.contains("temperature")) numeric(setup["temperature"], "temperature");
    flag(setup, "nominal", "setup");
    validateWords(setup["variables"], "variables", true);
    std::set<QString> tests, corners, outs;
    const QJsonArray ts = setup["tests"].toArray();
    for (int k = 0; k < ts.size(); ++k) {
        const QJsonObject t = ts[k].toObject();
        const std::string w = "tests[" + std::to_string(k) + "]";
        keys(t, {"name", "design", "enabled", "analyses", "models", "options"}, w);
        if (!identifier(t["name"].toString()) || !tests.insert(t["name"].toString()).second) bad(w + ".name", "a unique name");
        if (t["design"].toString().split('/').size() != 3) bad(w + ".design", "\"lib/cell/view\"");
        flag(t, "enabled", w);
        const QJsonArray as = t["analyses"].toArray();
        if (as.isEmpty()) bad(w + ".analyses", "at least one analysis");
        for (int a = 0; a < as.size(); ++a) card(as[a].toObject(), w + ".analyses[" + std::to_string(a) + "]");
        validateModels(t["models"], w + ".models");
        validateWords(t["options"], w + ".options", false);
    }
    const QJsonArray cs = setup["corners"].toArray();
    for (int k = 0; k < cs.size(); ++k) {
        const QJsonObject c = cs[k].toObject();
        const std::string w = "corners[" + std::to_string(k) + "]";
        keys(c, {"name", "enabled", "temperature", "variables", "models"}, w);
        if (!identifier(c["name"].toString()) || c["name"] == "Nominal" || !corners.insert(c["name"].toString()).second)
            bad(w + ".name", "a unique name (not Nominal)");
        flag(c, "enabled", w);
        if (c.contains("temperature")) numeric(c["temperature"], w + ".temperature");
        validateWords(c["variables"], w + ".variables", false);
        validateModels(c["models"], w + ".models");
    }
    const QJsonArray os = setup["outputs"].toArray();
    for (int k = 0; k < os.size(); ++k) {
        const QJsonObject o = os[k].toObject();
        const std::string w = "outputs[" + std::to_string(k) + "]";
        keys(o, {"name", "test", "expression", "min", "max"}, w);
        if (o["name"].toString().isEmpty() || !outs.insert(o["name"].toString()).second) bad(w + ".name", "a unique name");
        if (!tests.count(o["test"].toString())) bad(w + ".test", "'" + s(o["test"]) + "' is not a test");
        if (o["expression"].toString().trimmed().isEmpty()) bad(w + ".expression", "an expression");
        for (const char* m : {"min", "max"}) if (!s(o[m]).empty()) numeric(o[m], w + "." + m);
    }
}

Job prepare(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view, const std::string& root) {
    oadb::Library& l = ws.library(lib);
    if (l.viewType(cell, view) != "maestro") throw std::invalid_argument(lib + "/" + cell + "/" + view + " is not an ADE (maestro) view");
    const auto text = l.prop(cell, view, "setup");
    const QJsonObject setup = text ? QJsonDocument::fromJson(QByteArray::fromStdString(*text)).object() : QJsonObject();
    validateSetup(setup);
    const fs::path wsDir = fs::absolute(fs::path(ws.file()).parent_path());

    Job job;
    job.setup = setup;
    std::vector<QJsonObject> corners;
    if (flag(setup, "nominal", "setup"))
        corners.push_back({{"name", "Nominal"}, {"temperature", setup.contains("temperature") ? setup["temperature"] : QJsonValue("27")}});
    for (const auto& c : setup["corners"].toArray())
        if (flag(c.toObject(), "enabled", "corner")) corners.push_back(c.toObject());
    for (const auto& tv : setup["tests"].toArray()) {
        const QJsonObject t = tv.toObject();
        if (!flag(t, "enabled", "test")) continue;
        const QStringList d = t["design"].toString().split('/');
        const std::string design = netlist(ws, d[0].toStdString(), d[1].toStdString(), d[2].toStdString());
        std::string cards;
        for (const auto& a : t["analyses"].toArray()) cards += card(a.toObject(), "analysis") + "\n";
        std::string options;
        const QJsonObject opts = t["options"].toObject();
        for (auto it = opts.begin(); it != opts.end(); ++it)
            options += " " + it.key().toStdString() + "=" + s(it.value());
        for (const auto& corner : corners) {
            // the corner's model file replaces the test's of the same file; relative files are the workspace's
            std::vector<std::pair<std::string, std::string>> models;
            auto addModel = [&](const QJsonObject& m) {
                const fs::path f = fs::path(s(m["file"])).is_absolute() ? fs::path(s(m["file"])) : wsDir / s(m["file"]);
                if (!fs::is_regular_file(f)) throw std::invalid_argument("setup: model file " + f.string() + " does not exist");
                for (auto& [file, section] : models) if (file == f.string()) { section = s(m["section"]); return; }
                models.emplace_back(f.string(), s(m["section"]));
            };
            for (const auto& m : t["models"].toArray()) addModel(m.toObject());
            for (const auto& m : corner["models"].toArray()) addModel(m.toObject());
            const QString temperature = corner.contains("temperature") ? corner["temperature"].toString() : setup["temperature"].toString("27");
            for (const auto& vars : combinations(setup["variables"].toObject(), corner["variables"].toObject())) {
                std::string deck = design;
                QJsonObject shown;
                for (const auto& [n, v] : vars) {
                    deck += ".param " + n + "=" + v + "\n";
                    shown[q(n)] = q(v);
                }
                deck += ".temp " + temperature.toStdString() + "\n";
                if (!options.empty()) deck += ".options" + options + "\n";
                for (const auto& [file, section] : models)
                    deck += section.empty() ? ".include \"" + file + "\"\n" : ".lib \"" + file + "\" " + section + "\n";
                deck += cards + ".end\n";
                const int index = (int)job.points.size() + 1;
                job.points.push_back({QJsonObject{{"index", index}, {"test", t["name"]}, {"corner", corner["name"]}, {"variables", shown},
                                                  {"temperature", temperature}, {"status", "pending"}},
                                      deck});
            }
        }
    }
    if (job.points.empty()) throw std::invalid_argument("setup: nothing to run (no enabled test, or no corner)");

    const fs::path base = fs::path(root) / lib / cell / view;
    fs::create_directories(base);
    fs::path dir;
    for (int n = 1;; ++n)  // the next free history (atomic: another process may be starting one)
        if (fs::create_directory(dir = base / ("Interactive." + std::to_string(n)))) {
            job.name = q("Interactive." + std::to_string(n));
            break;
        }
    job.dir = dir.string();
    QJsonArray points;
    for (auto& p : job.points) {
        const fs::path pd = dir / std::to_string(p.info["index"].toInt());
        fs::create_directory(pd);
        p.info["dir"] = q(pd.string());
        points.append(p.info);
    }
    write(job.dir, {{"name", job.name}, {"status", "running"}, {"started", QDateTime::currentDateTime().toString(Qt::ISODate)},
                    {"setup", setup}, {"points", points}});
    return job;
}

QJsonObject execute(const Job& job, const std::string& simulator, std::chrono::seconds timeout, const std::atomic<bool>* cancel) {
    QJsonObject history = readHistory(job.dir);
    QJsonArray points = history["points"].toArray();
    std::mutex m;
    auto update = [&](size_t k, const QJsonObject& p) {
        std::lock_guard<std::mutex> lock(m);
        points[(qsizetype)k] = p;
        history["points"] = points;
        write(job.dir, history);
    };
    std::atomic<size_t> next{0};
    auto worker = [&] {
        for (size_t k; (k = next++) < job.points.size();) {
            QJsonObject p = job.points[k].info;
            if (cancel && *cancel) {
                p["status"] = "stopped";
                update(k, p);
                continue;
            }
            p["status"] = "running";
            update(k, p);
            try {
                const Run r = simulate(simulator, s(p["dir"]), job.points[k].netlist, timeout, cancel);
                p["outputs"] = outputs(job.setup, p["test"].toString(), r.plots);
                p["status"] = "done";
            } catch (const std::exception& e) {
                p["status"] = cancel && *cancel ? "stopped" : "error";
                p["error"] = QString::fromUtf8(e.what());
            }
            update(k, p);
        }
    };
    // ponytail: half the cores, one point each; treespice's own threads share the rest
    const size_t n = std::min(job.points.size(), (size_t)std::max(1u, std::thread::hardware_concurrency() / 2));
    std::vector<std::thread> threads;
    for (size_t k = 0; k < n; ++k) threads.emplace_back(worker);
    for (auto& t : threads) t.join();
    history["status"] = cancel && *cancel ? "stopped" : "done";
    history["finished"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    write(job.dir, history);
    return history;
}

QJsonObject readHistory(const std::string& dir) {
    std::ifstream f(fs::path(dir) / "history.json", std::ios::binary);
    if (!f) throw std::invalid_argument("no history at " + dir);
    const std::string b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    QJsonParseError e;
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(b), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject()) throw std::runtime_error("history " + dir + " is not readable: " + e.errorString().toStdString());
    return d.object();
}

Jobs::~Jobs() {
    std::lock_guard<std::mutex> lock(m_);
    for (auto& [id, r] : runs_) r->cancel = true;
    for (auto& [id, r] : runs_) if (r->thread.joinable()) r->thread.join();
}

void Jobs::start(const std::string& id, std::function<void(const std::atomic<bool>&)> work) {
    std::lock_guard<std::mutex> lock(m_);
    auto& r = runs_[id];
    if (r && r->thread.joinable()) r->thread.join();  // a finished run of the same id (never a live one: ids are new)
    r = std::make_unique<Run>();
    r->thread = std::thread([run = r.get(), work = std::move(work)] { work(run->cancel); });
}

bool Jobs::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_);
    const auto it = runs_.find(id);
    if (it == runs_.end()) return false;
    it->second->cancel = true;
    return true;
}

}  // namespace sim
