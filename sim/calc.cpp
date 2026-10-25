#include "sim/calc.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace sim {

namespace {

using cplx = std::complex<double>;
[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("calculator: " + what); }

struct Val {  // an argument: a number or waveform, or a string ("rising")
    Wave w;
    std::string s;
    bool isString = false;
};

Wave number(cplx v) { return {{}, {v}}; }
Wave map(const Wave& w, const std::function<cplx(cplx)>& f) {
    Wave o{w.x, {}, w.scale};
    for (const cplx v : w.y) o.y.push_back(f(v));
    return o;
}

const Wave& wave(const Val& v, const std::string& fn) {
    if (v.isString) fail(fn + ": a string where a value belongs");
    return v.w;
}
const Wave& waveform(const Val& v, const std::string& fn) {
    const Wave& w = wave(v, fn);
    if (w.scalar() || w.x.size() < 2) fail(fn + ": needs a waveform (VT(out), VF(out), ...), not a number");
    return w;
}
std::vector<double> realY(const Wave& w, const std::string& fn) {
    std::vector<double> y;
    for (const cplx v : w.y) {
        if (v.imag() != 0) fail(fn + ": the waveform is complex; take mag, dB20, phase, real or imag first");
        y.push_back(v.real());
    }
    return y;
}
double real(const Val& v, const std::string& fn) {
    const Wave& w = wave(v, fn);
    if (!w.scalar()) fail(fn + ": needs a number, not a waveform");
    if (w.y[0].imag() != 0) fail(fn + ": needs a real number");
    return w.y[0].real();
}

// y at x, linear between the points around it (the scale may run either way, a dc sweep down)
cplx at(const Wave& w, double x, const std::string& fn) {
    for (size_t k = 1; k < w.x.size(); ++k) {
        const double a = w.x[k - 1], b = w.x[k];
        if ((x - a) * (x - b) > 0) continue;
        const double t = a == b ? 0 : (x - a) / (b - a);
        return w.y[k - 1] + t * (w.y[k] - w.y[k - 1]);
    }
    fail(fn + ": " + std::to_string(x) + " is outside the waveform (" + std::to_string(w.x.front()) + " .. " + std::to_string(w.x.back()) + ")");
}

// where y crosses level: edge +1 rising, -1 falling, 0 either
std::vector<double> crossings(const std::vector<double>& x, const std::vector<double>& y, double level, int edge) {
    std::vector<double> out;
    for (size_t k = 1; k < y.size(); ++k) {
        const double a = y[k - 1] - level, b = y[k] - level;
        const bool rising = a < 0 && b >= 0, falling = a > 0 && b <= 0;
        if ((rising && edge >= 0) || (falling && edge <= 0)) out.push_back(x[k - 1] + (x[k] - x[k - 1]) * a / (a - b));
    }
    return out;
}
double firstCross(const Wave& w, double level, int edge, const std::string& fn, double after = -HUGE_VAL) {
    for (const double c : crossings(w.x, realY(w, fn), level, edge))
        if (c >= after) return c;
    fail(fn + ": the waveform never crosses " + std::to_string(level));
}

double integral(const std::vector<double>& x, const std::vector<double>& y) {
    double s = 0;
    for (size_t k = 1; k < x.size(); ++k) s += (x[k] - x[k - 1]) * (y[k] + y[k - 1]) / 2;
    return s;
}

std::vector<double> db20(const Wave& w, const std::string& fn) {
    std::vector<double> m;
    for (const cplx v : w.y) {
        if (std::abs(v) == 0) fail(fn + ": the magnitude reaches 0 (dB of 0)");
        m.push_back(20 * std::log10(std::abs(v)));
    }
    return m;
}
std::vector<double> unwrappedPhase(const Wave& w) {  // degrees, without the ±360 jumps
    std::vector<double> p;
    for (const cplx v : w.y) {
        double d = std::arg(v) * 180 / M_PI;
        if (!p.empty()) d += 360 * std::round((p.back() - d) / 360);
        p.push_back(d);
    }
    return p;
}

// a transition's initial and final values, and the time between two percentages of it
struct Transition { double initial, final; };
Transition transition(const Wave& w, const std::string& fn) {
    const std::vector<double> y = realY(w, fn);
    if (y.front() == y.back()) fail(fn + ": the waveform ends where it starts: no transition");
    return {y.front(), y.back()};
}
double between(const Wave& w, double lo, double hi, int need, const std::string& fn) {
    const Transition t = transition(w, fn);
    const int dir = t.final > t.initial ? 1 : -1;
    if (need && dir != need) fail(fn + ": the waveform " + (dir > 0 ? "rises" : "falls"));
    const double a = firstCross(w, t.initial + lo / 100 * (t.final - t.initial), dir, fn);
    return firstCross(w, t.initial + hi / 100 * (t.final - t.initial), dir, fn, a) - a;
}

class Parser {
public:
    Parser(const std::string& text, const Lookup& lookup) : s_(text), lookup_(lookup) {}
    Wave parse() {
        Val v = expr();
        skip();
        if (i_ != s_.size()) fail("unexpected '" + s_.substr(i_) + "'");
        if (v.isString) fail("the expression is a string, not a value");
        return v.w;
    }

private:
    const std::string& s_;
    const Lookup& lookup_;
    size_t i_ = 0;

    void skip() { while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) ++i_; }
    bool eat(char c) {
        skip();
        if (i_ < s_.size() && s_[i_] == c) { ++i_; return true; }
        return false;
    }
    void expect(char c) {
        if (!eat(c)) fail(std::string("expected '") + c + "' at " + (i_ < s_.size() ? "'" + s_.substr(i_) + "'" : "the end"));
    }

    static Wave combine(const Wave& a, const Wave& b, char op) {
        auto f = [op](cplx p, cplx q) {
            switch (op) {
                case '+': return p + q;
                case '-': return p - q;
                case '*': return p * q;
                case '/':
                    if (q == cplx(0)) fail("division by 0");
                    return p / q;
                default: return std::pow(p, q);
            }
        };
        if (!a.scalar() && !b.scalar() && a.x != b.x) fail("the waveforms have different scales (different analyses?)");
        Wave o{a.scalar() ? b.x : a.x, {}, a.scalar() ? b.scale : a.scale};
        const size_t n = std::max(a.y.size(), b.y.size());
        for (size_t k = 0; k < n; ++k) o.y.push_back(f(a.y[a.scalar() ? 0 : k], b.y[b.scalar() ? 0 : k]));
        return o;
    }
    Val arith(const Val& a, const Val& b, char op) {
        return {combine(wave(a, std::string(1, op)), wave(b, std::string(1, op)), op), "", false};
    }

    Val expr() {
        Val v = term();
        for (;;) {
            if (eat('+')) v = arith(v, term(), '+');
            else if (eat('-')) v = arith(v, term(), '-');
            else return v;
        }
    }
    Val term() {
        Val v = unary();
        for (;;) {
            if (eat('*')) v = arith(v, unary(), '*');
            else if (eat('/')) v = arith(v, unary(), '/');
            else return v;
        }
    }
    Val unary() {
        if (eat('-')) return arith({number(0), "", false}, unary(), '-');
        Val v = primary();
        if (eat('^')) v = arith(v, unary(), '^');
        return v;
    }
    Val primary() {
        skip();
        if (i_ >= s_.size()) fail("the expression ends early");
        const char c = s_[i_];
        if (eat('(')) {
            Val v = expr();
            expect(')');
            return v;
        }
        if (c == '"') {
            const size_t e = s_.find('"', i_ + 1);
            if (e == std::string::npos) fail("an unterminated string");
            Val v{{}, s_.substr(i_ + 1, e - i_ - 1), true};
            i_ = e + 1;
            return v;
        }
        if (std::isdigit((unsigned char)c) || c == '.') {
            const size_t start = i_;
            while (i_ < s_.size() && (std::isdigit((unsigned char)s_[i_]) || s_[i_] == '.')) ++i_;
            if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E') && i_ + 1 < s_.size() &&
                (std::isdigit((unsigned char)s_[i_ + 1]) || ((s_[i_ + 1] == '-' || s_[i_ + 1] == '+') && i_ + 2 < s_.size() && std::isdigit((unsigned char)s_[i_ + 2])))) {
                i_ += 2;
                while (i_ < s_.size() && std::isdigit((unsigned char)s_[i_])) ++i_;
            }
            while (i_ < s_.size() && std::isalpha((unsigned char)s_[i_])) ++i_;
            return {number(siNumber(s_.substr(start, i_ - start))), "", false};
        }
        if (!std::isalpha((unsigned char)c) && c != '_') fail("unexpected '" + s_.substr(i_) + "'");
        const size_t start = i_;
        while (i_ < s_.size() && (std::isalnum((unsigned char)s_[i_]) || s_[i_] == '_')) ++i_;
        const std::string name = s_.substr(start, i_ - start);
        expect('(');
        static const std::set<std::string> access = {"VT", "VF", "VDC", "VOP", "IT", "IF", "IDC", "IOP", "VN", "VNTOT"};
        if (access.count(name)) {  // its argument is a name, taken as written
            const size_t e = s_.find(')', i_);
            if (e == std::string::npos) fail(name + "( has no ')'");
            std::string arg = s_.substr(i_, e - i_);
            i_ = e + 1;
            arg.erase(0, arg.find_first_not_of(" \t\""));
            arg.erase(arg.find_last_not_of(" \t\"") + 1);
            if (arg.empty()) fail(name + "() names nothing");
            return {lookup_(name, arg), "", false};
        }
        std::vector<Val> args;
        if (!eat(')')) {
            do args.push_back(expr());
            while (eat(','));
            expect(')');
        }
        return {call(name, args), "", false};
    }

    static Wave call(const std::string& fn, const std::vector<Val>& a) {
        auto arity = [&](size_t lo, size_t hi) {
            if (a.size() < lo || a.size() > hi)
                fail(fn + " takes " + (lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + std::to_string(hi)) + " argument(s)");
        };
        auto opt = [&](size_t k, double dflt) { return k < a.size() ? real(a[k], fn) : dflt; };
        auto pointwise = [&](const std::function<cplx(cplx)>& f) { arity(1, 1); return map(wave(a[0], fn), f); };
        if (fn == "mag" || fn == "abs") return pointwise([](cplx v) { return cplx(std::abs(v)); });
        if (fn == "real") return pointwise([](cplx v) { return cplx(v.real()); });
        if (fn == "imag") return pointwise([](cplx v) { return cplx(v.imag()); });
        if (fn == "phase") return pointwise([](cplx v) { return cplx(std::arg(v) * 180 / M_PI); });
        if (fn == "sqrt") return pointwise([](cplx v) { return std::sqrt(v); });
        if (fn == "dB20" || fn == "dB10" || fn == "log10")
            return pointwise([fn](cplx v) {
                const double m = fn == "log10" ? v.real() : std::abs(v);
                if (m <= 0 || (fn == "log10" && v.imag() != 0)) fail(fn + " of " + (m == 0 ? "0" : "a negative or complex value"));
                return cplx((fn == "dB20" ? 20 : fn == "dB10" ? 10 : 1) * std::log10(m));
            });
        if (fn == "value") {
            arity(2, 2);
            return number(at(waveform(a[0], fn), real(a[1], fn), fn));
        }
        if (fn == "ymax" || fn == "ymin") {
            arity(1, 1);
            const auto y = realY(waveform(a[0], fn), fn);
            return number(fn == "ymax" ? *std::max_element(y.begin(), y.end()) : *std::min_element(y.begin(), y.end()));
        }
        if (fn == "average" || fn == "rms") {
            arity(1, 1);
            const Wave& w = waveform(a[0], fn);
            std::vector<double> y = realY(w, fn);
            if (fn == "rms") for (double& v : y) v *= v;
            const double m = integral(w.x, y) / (w.x.back() - w.x.front());
            return number(fn == "rms" ? std::sqrt(m) : m);
        }
        if (fn == "cross") {
            arity(2, 4);
            const Wave& w = waveform(a[0], fn);
            const int n = (int)opt(2, 1);
            const std::string edge = a.size() > 3 ? a[3].s : "either";
            if (a.size() > 3 && (!a[3].isString || (edge != "rising" && edge != "falling" && edge != "either")))
                fail("cross: the edge is \"rising\", \"falling\" or \"either\"");
            const auto c = crossings(w.x, realY(w, fn), real(a[1], fn), edge == "rising" ? 1 : edge == "falling" ? -1 : 0);
            if (n < 1 || (size_t)n > c.size()) fail("cross: no crossing " + std::to_string(n) + " (there are " + std::to_string(c.size()) + ")");
            return number(c[(size_t)n - 1]);
        }
        if (fn == "riseTime" || fn == "fallTime") {
            arity(1, 3);
            return number(between(waveform(a[0], fn), opt(1, 10), opt(2, 90), fn == "riseTime" ? 1 : -1, fn));
        }
        if (fn == "slewRate") {
            arity(1, 3);
            const Wave& w = waveform(a[0], fn);
            const double lo = opt(1, 10), hi = opt(2, 90);
            const Transition t = transition(w, fn);
            return number((hi - lo) / 100 * std::abs(t.final - t.initial) / between(w, lo, hi, 0, fn));
        }
        if (fn == "overshoot") {
            arity(1, 1);
            const Wave& w = waveform(a[0], fn);
            const Transition t = transition(w, fn);
            const auto y = realY(w, fn);
            const double peak = t.final > t.initial ? *std::max_element(y.begin(), y.end()) : *std::min_element(y.begin(), y.end());
            return number(std::max(0.0, (peak - t.final) / (t.final - t.initial) * 100));
        }
        if (fn == "settlingTime") {
            arity(1, 2);
            const Wave& w = waveform(a[0], fn);
            const Transition t = transition(w, fn);
            const auto y = realY(w, fn);
            const double band = opt(1, 1) / 100 * std::abs(t.final - t.initial);
            size_t k = y.size();  // the last point outside the band
            while (k-- > 0 && std::abs(y[k] - t.final) <= band) {}
            if (k + 1 >= y.size()) fail("settlingTime: the waveform has not settled by its end");
            const double e0 = std::abs(y[k] - t.final) - band, e1 = std::abs(y[k + 1] - t.final) - band;
            return number(w.x[k] + (w.x[k + 1] - w.x[k]) * e0 / (e0 - e1) - w.x.front());
        }
        if (fn == "delay") {
            arity(4, 4);
            return number(firstCross(waveform(a[2], fn), real(a[3], fn), 0, fn) - firstCross(waveform(a[0], fn), real(a[1], fn), 0, fn));
        }
        if (fn == "bandwidth" || fn == "ugf" || fn == "phaseMargin" || fn == "gainMargin") {
            fn == "bandwidth" ? arity(1, 2) : arity(1, 1);
            const Wave& w = waveform(a[0], fn);
            const Wave m{w.x, {}};
            Wave mdb = m;
            for (const double d : db20(w, fn)) mdb.y.push_back(d);
            if (fn == "bandwidth") return number(firstCross(mdb, mdb.y[0].real() - opt(1, 3), -1, fn));
            Wave ph = m;
            for (const double p : unwrappedPhase(w)) ph.y.push_back(p);
            if (fn == "gainMargin") {
                const double f180 = firstCross(ph, ph.y[0].real() > -180 ? -180 : 180, 0, fn);
                return number(-at(mdb, f180, fn).real());
            }
            const double u = firstCross(mdb, 0, 0, fn);
            return fn == "ugf" ? number(u) : number(180 + at(ph, u, fn).real());
        }
        fail("no function " + fn + "()");
    }
};

}  // namespace

double siNumber(const std::string& text) {
    const char* b = text.c_str();
    char* end = nullptr;
    const double v = std::strtod(b, &end);
    if (end == b || text.empty() || std::isspace((unsigned char)text[0])) fail("'" + text + "' is not a number");
    std::string suffix(end);
    for (auto& c : suffix) c = (char)std::tolower((unsigned char)c);
    if (!std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isalpha(c); })) fail("'" + text + "' is not a number");
    // SPICE: a scale letter (meg and mil before m), then any unit letters, ignored
    static const std::vector<std::pair<std::string, double>> scale = {{"meg", 1e6}, {"mil", 25.4e-6}, {"t", 1e12}, {"g", 1e9}, {"k", 1e3},
                                                                       {"m", 1e-3}, {"u", 1e-6}, {"n", 1e-9}, {"p", 1e-12}, {"f", 1e-15}, {"a", 1e-18}};
    for (const auto& [s, m] : scale)
        if (suffix.rfind(s, 0) == 0) return v * m;
    return v;
}

Wave evaluate(const std::string& expression, const Lookup& lookup) { return Parser(expression, lookup).parse(); }

namespace {
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
        if (scaled) {
            for (const auto& x : p->data[0]) w.x.push_back(x.real());
            w.scale = p->vectors[0];
        }
        w.y = scaled ? p->data[k] : std::vector<std::complex<double>>{p->data[k][0]};
        return w;
    }
    throw std::runtime_error("calculator: " + kind + "(" + arg + "): the " + p->name + " has no " + want);
}

}  // namespace

Lookup resultsLookup(const std::vector<Plot>& plots) {
    return [&plots](const std::string& kind, const std::string& name) { return access(plots, kind, name); };
}

}  // namespace sim
