// The RTL stage: a digital design's sources, read and written, and checked. A design is a record beside the workspace (rtl/<name>.json): its Verilog, its top, its testbenches.
#include "cmd/registry.h"
#include "flow/rtl_flow.h"
#include "flow/subprocess.h"
#include "oadb/oadb.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cmd {

namespace {

namespace fs = std::filesystem;
using namespace acutesim::compute::orchestration;

const QJsonObject kDesign = param("string", "the design's name: letters, digits, _");
const QJsonObject kFiles = {{"type", "array"}, {"description", "absolute paths"}, {"items", QJsonObject{{"type", "string"}}}};

std::string s(const QJsonObject& a, const char* key) { return a[key].toString().toStdString(); }
QString q(const std::string& text) { return QString::fromStdString(text); }

fs::path designFile(Context& c, const QJsonObject& a) {
    static const QRegularExpression good("^[A-Za-z0-9_]+$");
    if (!good.match(a["design"].toString()).hasMatch()) throw std::invalid_argument("a design's name is letters, digits and _");
    return fs::absolute(fs::path(c.workspace.file()).parent_path()) / "rtl" / (s(a, "design") + ".json");
}

void needAbsolute(const QJsonValue& files, const char* what) {
    for (const auto& v : files.toArray()) {
        const fs::path f = v.toString().toStdString();
        if (!f.is_absolute()) throw std::invalid_argument(std::string(what) + " must be absolute paths: " + f.string());
        if (!fs::exists(f)) throw std::invalid_argument(std::string("no file ") + f.string());
    }
}

QJsonObject readDesign(const fs::path& file) {
    QFile f(QString::fromStdString(file.string()));
    if (!f.open(QIODevice::ReadOnly)) throw std::invalid_argument("no design " + file.stem().string());
    return QJsonDocument::fromJson(f.readAll()).object();
}

// iverilog's "file:line: message" lines, with what it calls each
QJsonArray diagnostics(const std::string& log) {
    static const QRegularExpression line("^(.+?):(\\d+):\\s*(?:(error|warning|sorry|syntax error)[: ]*)?(.*)$", QRegularExpression::MultilineOption);
    QJsonArray out;
    auto it = line.globalMatch(q(log));
    while (it.hasNext()) {
        const auto m = it.next();
        const QString kind = m.captured(3), rest = m.captured(4).trimmed();
        out.append(QJsonObject{{"file", m.captured(1)}, {"line", m.captured(2).toInt()},
                               {"severity", kind == "warning" ? "warning" : kind.isEmpty() && rest.startsWith("warning") ? "warning" : "error"},
                               {"message", kind.contains("syntax") ? kind + (rest.isEmpty() ? QString() : ": " + rest) : rest}});
    }
    return out;
}

}  // namespace

void addRtlCommands(Registry& r) {
    r.add({"rtl_design_set", "Records a digital design: its Verilog, its top module, its testbenches and clock. Replaces one of that name.",
           object({{"design", kDesign}, {"verilog", kFiles}, {"top", param("string", "the top module")}},
                  {{"testbenches", kFiles}, {"clock_port", param("string", "the clock's port (default clk)")}, {"clock_period_ns", param("number", "the clock's period in ns (default 10)")},
                   {"platform", param("string", "a platform of flow_platforms (default ihp-sg13g2)")}, {"core_utilization", param("number", "percent of the core the cells fill (default 40)")}}),
           [](Context& c, const QJsonObject& a) {
               const fs::path file = designFile(c, a);
               if (a["verilog"].toArray().isEmpty()) throw std::invalid_argument("a design needs its Verilog");
               needAbsolute(a["verilog"], "verilog");
               needAbsolute(a["testbenches"], "testbenches");
               const QString top = a["top"].toString();
               static const QRegularExpression name("^[A-Za-z_][A-Za-z0-9_$]*$");
               if (!name.match(top).hasMatch()) throw std::invalid_argument("the top must be a Verilog name");
               QJsonObject design = a;
               if (!design.contains("clock_port")) design["clock_port"] = "clk";
               if (!design.contains("clock_period_ns")) design["clock_period_ns"] = 10.0;
               if (!design.contains("platform")) design["platform"] = "ihp-sg13g2";
               if (!flow::findPlatform(s(design, "platform"))) throw std::invalid_argument("no platform " + s(design, "platform") + " (known: " + flow::knownPlatforms() + ")");
               fs::create_directories(file.parent_path());
               std::ofstream(file) << QJsonDocument(design).toJson().toStdString();
               return QJsonValue(design);
           }});
    r.add({"rtl_design_get", "A digital design's record.", object({{"design", kDesign}}),
           [](Context& c, const QJsonObject& a) { return QJsonValue(readDesign(designFile(c, a))); }, true});
    r.add({"rtl_design_list", "The digital designs recorded in this workspace.", object({}),
           [](Context& c, const QJsonObject&) {
               QJsonArray out;
               const fs::path dir = fs::absolute(fs::path(c.workspace.file()).parent_path()) / "rtl";
               if (!fs::is_directory(dir)) return QJsonValue(out);
               std::vector<fs::path> files;
               for (const auto& e : fs::directory_iterator(dir))
                   if (e.path().extension() == ".json") files.push_back(e.path());
               std::sort(files.begin(), files.end());
               for (const auto& f : files) out.append(readDesign(f));
               return QJsonValue(out);
           },
           true});
    r.add({"rtl_design_remove", "Forgets a digital design; its files stay.", object({{"design", kDesign}}),
           [](Context& c, const QJsonObject& a) {
               const fs::path file = designFile(c, a);
               if (!fs::remove(file)) throw std::invalid_argument("no design " + s(a, "design"));
               return QJsonValue(true);
           }});
    r.add({"rtl_lint", "Compiles a design's Verilog (or the files given) with Icarus and returns each error and warning: file, line, message. It finds its top with top given. clean is true with no errors.",
           object({}, {{"design", kDesign}, {"verilog", kFiles}, {"top", param("string", "the top module")}}),
           [](Context& c, const QJsonObject& a) {
               QJsonArray files = a["verilog"].toArray();
               QString top = a["top"].toString();
               if (a.contains("design")) {
                   const QJsonObject design = readDesign(designFile(c, a));
                   if (files.isEmpty()) files = design["verilog"].toArray();
                   if (top.isEmpty()) top = design["top"].toString();
               }
               if (files.isEmpty()) throw std::invalid_argument("give a design, or verilog");
               const RtlFlowTools tools = RtlFlowTools::detect();
               if (tools.iverilog.empty()) throw std::runtime_error("Icarus Verilog is needed");
               std::vector<std::string> args{"-g2012", "-Wall", "-t", "null"};
               if (!top.isEmpty()) {
                   args.push_back("-s");
                   args.push_back(top.toStdString());
               }
               for (const auto& f : files) args.push_back(f.toString().toStdString());
               acutesim::orchestration::Subprocess p(tools.iverilog, args);
               const auto res = p.run();
               const std::string log = res.stdout_output + res.stderr_output;
               const QJsonArray found = diagnostics(log);
               int errors = 0;
               for (const auto& d : found) errors += d.toObject()["severity"] == "error";
               const bool clean = res.exit_code == 0 && errors == 0;
               QJsonObject out{{"clean", clean}, {"diagnostics", found}, {"errors", errors}, {"warnings", (int)found.size() - errors}};
               if (res.exit_code != 0 && !errors) out["log"] = q(log);
               return QJsonValue(out);
           },
           true});
    r.add({"rtl_read", "A source file's text.", object({{"file", param("string", "an absolute path")}}),
           [](Context&, const QJsonObject& a) {
               const fs::path f = s(a, "file");
               if (!f.is_absolute()) throw std::invalid_argument("the file must be an absolute path");
               std::ifstream in(f);
               if (!in) throw std::invalid_argument("cannot read " + f.string());
               return QJsonValue(q(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>())));
           },
           true});
    r.add({"rtl_write", "Writes a source file's text, whole. Returns its size.", object({{"file", param("string", "an absolute path")}, {"text", param("string", "its text")}}),
           [](Context&, const QJsonObject& a) {
               const fs::path f = s(a, "file");
               if (!f.is_absolute()) throw std::invalid_argument("the file must be an absolute path");
               if (!fs::is_directory(f.parent_path())) throw std::invalid_argument("no directory " + f.parent_path().string());
               const fs::path tmp = f.string() + ".openvlsi-tmp";
               const std::string text = s(a, "text");
               {
                   std::ofstream out(tmp);
                   if (!(out << text) || !out.flush()) throw std::runtime_error("cannot write " + f.string());
               }
               fs::rename(tmp, f);  // a crash leaves the old file or the new one
               return QJsonValue((double)text.size());
           },
           true});
}

}  // namespace cmd
