#include "flow/vcd.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace flow {

namespace {

[[noreturn]] void bad(const std::string& path, long line, const std::string& why) {
    throw std::runtime_error("VCD " + path + " line " + std::to_string(line) + ": " + why);
}

double unitSeconds(const std::string& unit) {
    if (unit == "s") return 1;
    if (unit == "ms") return 1e-3;
    if (unit == "us") return 1e-6;
    if (unit == "ns") return 1e-9;
    if (unit == "ps") return 1e-12;
    if (unit == "fs") return 1e-15;
    return 0;
}

}  // namespace

Vcd readVcd(const std::string& path, bool headerOnly, const std::set<std::string>& wanted) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot read " + path);
    Vcd out;
    std::vector<std::string> scope;
    std::set<std::string> keep;
    bool all = wanted.empty(), body = false, sawEnd = false;
    std::string line;
    long at = 0;
    int64_t now = 0;
    while (std::getline(f, line)) {
        ++at;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        if (!body) {
            std::istringstream words(line);
            std::string word;
            words >> word;
            if (word == "$timescale") {
                std::string rest, text;
                while (words >> rest && rest != "$end") text += rest;
                while (rest != "$end" && std::getline(f, line)) {
                    ++at;
                    std::istringstream more(line);
                    while (more >> rest && rest != "$end") text += rest;
                }
                size_t digits = 0;
                while (digits < text.size() && std::isdigit((unsigned char)text[digits])) ++digits;
                const double unit = unitSeconds(text.substr(digits));
                if (!digits || unit == 0) bad(path, at, "timescale '" + text + "'");
                out.secondsPerUnit = std::stod(text.substr(0, digits)) * unit;
            } else if (word == "$scope") {
                std::string kind, name;
                words >> kind >> name;
                if (name.empty()) bad(path, at, "a scope without a name");
                scope.push_back(name);
            } else if (word == "$upscope") {
                if (scope.empty()) bad(path, at, "$upscope outside any scope");
                scope.pop_back();
            } else if (word == "$var") {
                std::string kind, code, name, range;
                int width = 0;
                words >> kind >> width >> code >> name;
                if (width <= 0 || code.empty() || name.empty()) bad(path, at, "a malformed $var");
                std::string full;
                for (const auto& s : scope) full += s + ".";
                out.vars.push_back({full + name, width, code});
            } else if (word == "$enddefinitions") {
                std::sort(out.vars.begin(), out.vars.end(), [](const VcdSignal& a, const VcdSignal& b) { return a.name < b.name; });
                if (headerOnly) return out;
                for (const auto& s : out.vars)
                    if (all || wanted.count(s.name)) keep.insert(s.code);
                body = true;
            } else if (word == "$date" || word == "$version" || word == "$comment") {
                while (line.find("$end") == std::string::npos && std::getline(f, line)) ++at;
            }
            continue;
        }
        const char c = line[first];
        if (c == '#') {
            char* end = nullptr;
            const long long t = std::strtoll(line.c_str() + first + 1, &end, 10);
            if (end == line.c_str() + first + 1) bad(path, at, "a malformed time");
            now = t;
            out.endTime = std::max<int64_t>(out.endTime, now);
            sawEnd = true;
        } else if (c == '$') {
            continue;  // $dumpvars, $dumpall, $end
        } else if (c == 'b' || c == 'B' || c == 'r' || c == 'R') {
            const size_t space = line.find(' ', first);
            if (space == std::string::npos) bad(path, at, "a vector value without its identifier");
            const std::string code = line.substr(space + 1);
            if (all || keep.count(code)) {
                std::string v = line.substr(first + 1, space - first - 1);
                if (c == 'r' || c == 'R') v = "r" + v;
                else for (auto& ch : v) ch = (char)std::tolower((unsigned char)ch);
                out.changes[code].push_back({now, v});
            }
        } else if (c == '0' || c == '1' || c == 'x' || c == 'X' || c == 'z' || c == 'Z') {
            const std::string code = line.substr(first + 1);
            if (code.empty()) bad(path, at, "a value without its identifier");
            if (all || keep.count(code)) out.changes[code].push_back({now, std::string(1, (char)std::tolower((unsigned char)c))});
        } else {
            bad(path, at, "cannot read '" + line + "'");
        }
    }
    if (!body) throw std::runtime_error("VCD " + path + " has no $enddefinitions: truncated, or not a dump");
    (void)sawEnd;
    return out;
}

std::string valueAt(const std::vector<VcdChange>& changes, int64_t time) {
    const auto it = std::upper_bound(changes.begin(), changes.end(), time, [](int64_t t, const VcdChange& c) { return t < c.time; });
    return it == changes.begin() ? "x" : std::prev(it)->value;
}

std::string toHex(const std::string& bits) {
    if (bits.empty() || bits[0] == 'r') return bits;
    std::string out;
    size_t have = bits.size() % 4 ? bits.size() % 4 : 4;
    for (size_t i = 0; i < bits.size(); i += have, have = 4) {
        const std::string nib = bits.substr(i, have);
        if (nib.find('x') != std::string::npos) out += 'x';
        else if (nib.find('z') != std::string::npos) out += 'z';
        else out += "0123456789abcdef"[std::stoi(nib, nullptr, 2)];
    }
    return out;
}

}  // namespace flow
