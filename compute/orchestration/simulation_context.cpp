#include "engine_api/simulation_context.h"
#include <cmath>
#include <cctype>
#include <limits>
#include <unordered_set>
#include <functional>

namespace acutesim::orchestration {

// ─── Minimal expression evaluator (no Qt dependency) ─────────────────────────
namespace {

struct MiniEval {
    const std::string& s;
    const std::unordered_map<std::string, double>& vars;
    int pos = 0;
    bool valid = true;

    void skipWS() {
        while (pos < (int)s.size() && std::isspace((unsigned char)s[pos])) ++pos;
    }

    double parseNumber() {
        skipWS();
        int start = pos;
        if (pos < (int)s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
        while (pos < (int)s.size() && (std::isdigit((unsigned char)s[pos]) || s[pos] == '.')) ++pos;
        if (pos < (int)s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < (int)s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            while (pos < (int)s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
        }
        std::string sub = s.substr(start, pos - start);
        try {
            size_t idx = 0;
            double v = std::stod(sub, &idx);
            if (idx != sub.size()) { valid = false; return 0.0; }
            return v;
        } catch (...) {
            valid = false;
            return 0.0;
        }
    }

    double parseAtom() {
        skipWS();
        if (pos >= (int)s.size()) { valid = false; return 0.0; }
        if (s[pos] == '(') {
            ++pos;
            double v = parseExpr();
            skipWS();
            if (pos < (int)s.size() && s[pos] == ')') ++pos;
            else valid = false;
            return v;
        }
        if (s[pos] == '-') { ++pos; return -parseAtom(); }
        // Identifier (variable name or function keyword)
        if (std::isalpha((unsigned char)s[pos]) || s[pos] == '_') {
            int start = pos;
            while (pos < (int)s.size() &&
                   (std::isalnum((unsigned char)s[pos]) || s[pos] == '_')) ++pos;
            std::string name = s.substr(start, pos - start);
            skipWS();
            if (pos < (int)s.size() && s[pos] == '(') {
                // Function call
                ++pos;
                double arg = parseExpr();
                skipWS();
                if (pos < (int)s.size() && s[pos] == ')') ++pos;
                if (name == "sin")  return std::sin(arg);
                if (name == "cos")  return std::cos(arg);
                if (name == "sqrt") return std::sqrt(arg);
                if (name == "log" || name == "log10") return std::log10(arg);
                if (name == "exp")  return std::exp(arg);
                return 0.0;
            }
            auto it = vars.find(name);
            if (it != vars.end()) return it->second;
            valid = false;
            return 0.0;
        }
        return parseNumber();
    }

    double parseTerm() {
        double v = parseAtom();
        while (valid) {
            skipWS();
            if (pos >= (int)s.size()) break;
            if (s[pos] == '*')      { ++pos; v *= parseAtom(); }
            else if (s[pos] == '/') {
                ++pos; double d = parseAtom();
                v = (d != 0.0) ? v / d : std::numeric_limits<double>::quiet_NaN();
            }
            else break;
        }
        return v;
    }

    double parseExpr() {
        double v = parseTerm();
        while (valid) {
            skipWS();
            if (pos >= (int)s.size()) break;
            if      (s[pos] == '+') { ++pos; v += parseTerm(); }
            else if (s[pos] == '-') { ++pos; v -= parseTerm(); }
            else break;
        }
        return v;
    }

    double eval(bool* ok) {
        double result = parseExpr();
        if (ok) *ok = valid;
        return valid ? result : 0.0;
    }
};

// Collapse multiple whitespace chars to single space, trim leading/trailing
static std::string simplified(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool inSpace = true; // start true to trim leading
    for (char c : s) {
        if (std::isspace((unsigned char)c)) {
            if (!inSpace) { out += ' '; inSpace = true; }
        } else {
            out += c;
            inSpace = false;
        }
    }
    // Trim trailing space
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

} // anonymous namespace

static double evalSimple(const std::string& expr,
                         const std::unordered_map<std::string, double>& vars,
                         bool* ok = nullptr) {
    std::string s = simplified(expr);
    MiniEval ev{s, vars};
    return ev.eval(ok);
}

// Check if word `key` appears as a whole word (word-boundary check) in `expr`
static bool containsWordBoundary(const std::string& expr, const std::string& key) {
    size_t pos = 0;
    while ((pos = expr.find(key, pos)) != std::string::npos) {
        bool leftOk  = (pos == 0) || !std::isalnum((unsigned char)expr[pos - 1]) && expr[pos - 1] != '_';
        bool rightOk = (pos + key.size() >= expr.size()) ||
                       (!std::isalnum((unsigned char)expr[pos + key.size()]) &&
                        expr[pos + key.size()] != '_');
        if (leftOk && rightOk) return true;
        pos += key.size();
    }
    return false;
}

// ─── SimulationContext methods ────────────────────────────────────────────────

double SimulationContext::evaluate(const std::string& expr) const {
    return evalSimple(expr, variables);
}

bool SimulationContext::resolveVariables(std::string* errorMsg) {
    // Build dependency graph: var -> set of variables it depends on.
    // Disabled vars keep their edges: they are not resolution roots, but an
    // enabled var referencing one still pulls it in, and it must then be
    // ordered after its own inputs.
    std::unordered_map<std::string, std::unordered_set<std::string>> dependencies;

    for (const auto& [var, expr] : variableExprs) {
        for (const auto& [key, _] : variableExprs) {
            if (key == var) continue;
            if (containsWordBoundary(expr, key)) {
                dependencies[var].insert(key);
            }
        }
    }

    // Topological sort (DFS)
    std::vector<std::string> sorted;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> tempVisited;

    std::function<bool(const std::string&)> visit = [&](const std::string& n) -> bool {
        if (tempVisited.count(n)) {
            if (errorMsg) *errorMsg = "Circular dependency detected involving '" + n + "'";
            return false;
        }
        if (visited.count(n)) return true;
        tempVisited.insert(n);
        auto depIt = dependencies.find(n);
        if (depIt != dependencies.end()) {
            for (const std::string& m : depIt->second) {
                if (!visit(m)) return false;
            }
        }
        tempVisited.erase(n);
        visited.insert(n);
        sorted.push_back(n);
        return true;
    };

    for (const auto& [key, _] : variableExprs) {
        auto enabledIt = variableEnabled.find(key);
        if (enabledIt != variableEnabled.end() && !enabledIt->second) continue;
        if (!visited.count(key)) {
            if (!visit(key)) return false;
        }
    }

    for (const auto& var : sorted) {
        auto exprIt = variableExprs.find(var);
        if (exprIt == variableExprs.end()) continue;
        bool ok = false;
        double val = evalSimple(exprIt->second, variables, &ok);
        if (!ok) {
            if (errorMsg)
                *errorMsg = "Failed to evaluate variable '" + var + "': " + exprIt->second;
            return false;
        }
        variables[var] = val;
    }

    return true;
}

} // namespace acutesim::orchestration
