#include "simulation_context.h"
#include <QStack>
#include <QSet>
#include <QRegularExpression>
#include <QtMath>
#include <limits>

namespace acutesim::orchestration {

// ─── Minimal expression evaluator (no Qt::Qml dependency) ────────────────────
namespace {

struct MiniEval {
    const QString& s;
    const QHash<QString, double>& vars;
    int pos = 0;
    bool valid = true;

    void skipWS() { while (pos < s.size() && s[pos].isSpace()) ++pos; }

    double parseNumber() {
        skipWS();
        int start = pos;
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
        while (pos < s.size() && (s[pos].isDigit() || s[pos] == '.')) ++pos;
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            while (pos < s.size() && s[pos].isDigit()) ++pos;
        }
        bool ok;
        double v = s.mid(start, pos - start).toDouble(&ok);
        if (!ok) valid = false;
        return ok ? v : 0.0;
    }

    double parseAtom() {
        skipWS();
        if (pos >= s.size()) { valid = false; return 0.0; }
        if (s[pos] == '(') {
            ++pos;
            double v = parseExpr();
            skipWS();
            if (pos < s.size() && s[pos] == ')') ++pos;
            else valid = false;
            return v;
        }
        if (s[pos] == '-') { ++pos; return -parseAtom(); }
        // Identifier (variable name or function keyword)
        if (s[pos].isLetter() || s[pos] == '_') {
            int start = pos;
            while (pos < s.size() && (s[pos].isLetterOrNumber() || s[pos] == '_')) ++pos;
            QString name = s.mid(start, pos - start);
            skipWS();
            if (pos < s.size() && s[pos] == '(') {
                // Function call: sin, cos, log, sqrt, pow (no-op: return 0 for unsupported)
                ++pos;
                double arg = parseExpr();
                skipWS();
                if (pos < s.size() && s[pos] == ')') ++pos;
                if (name == "sin")  return std::sin(arg);
                if (name == "cos")  return std::cos(arg);
                if (name == "sqrt") return std::sqrt(arg);
                if (name == "log" || name == "log10") return std::log10(arg);
                if (name == "exp")  return std::exp(arg);
                // Second arg for pow
                return 0.0;
            }
            if (vars.contains(name)) return vars[name];
            valid = false;
            return 0.0;
        }
        return parseNumber();
    }

    double parseTerm() {
        double v = parseAtom();
        while (valid) {
            skipWS();
            if (pos >= s.size()) break;
            if (s[pos] == '*')     { ++pos; v *= parseAtom(); }
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
            if (pos >= s.size()) break;
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

} // anonymous namespace

static double evalSimple(const QString& expr, const QHash<QString, double>& vars, bool* ok = nullptr) {
    MiniEval ev{expr.simplified(), vars};
    return ev.eval(ok);
}

// ─── SimulationContext methods ────────────────────────────────────────────────

double SimulationContext::evaluate(const QString& expr) const {
    return evalSimple(expr, variables);
}

bool SimulationContext::resolveVariables(QString* errorMsg) {
    QHash<QString, QSet<QString>> dependencies;

    QHashIterator<QString, QString> i(variableExprs);
    while (i.hasNext()) {
        i.next();
        QString var = i.key();
        QString expr = i.value();
        if (!variableEnabled.value(var, true)) continue;
        for (auto key : variableExprs.keys()) {
            if (key == var) continue;
            QRegularExpression re(QString("\\b%1\\b").arg(QRegularExpression::escape(key)));
            if (expr.contains(re)) {
                dependencies[var].insert(key);
            }
        }
    }

    QList<QString> sorted;
    QSet<QString> visited;
    QSet<QString> tempVisited;

    std::function<bool(const QString&)> visit = [&](const QString& n) -> bool {
        if (tempVisited.contains(n)) {
            if (errorMsg) *errorMsg = QString("Circular dependency detected involving '%1'").arg(n);
            return false;
        }
        if (visited.contains(n)) return true;
        tempVisited.insert(n);
        for (const QString& m : dependencies.value(n)) {
            if (!visit(m)) return false;
        }
        tempVisited.remove(n);
        visited.insert(n);
        sorted.append(n);
        return true;
    };

    auto keys = variableExprs.keys();
    for (const auto& key : keys) {
        if (!variableEnabled.value(key, true)) continue;
        if (!visited.contains(key)) {
            if (!visit(key)) return false;
        }
    }

    for (const auto& var : sorted) {
        QString expr = variableExprs.value(var);
        bool ok = false;
        double val = evalSimple(expr, variables, &ok);
        if (!ok) {
            if (errorMsg) *errorMsg = QString("Failed to evaluate variable '%1': %2").arg(var, expr);
            return false;
        }
        variables[var] = val;
    }

    return true;
}

} // namespace acutesim::orchestration
