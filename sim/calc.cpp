#include "sim/calc.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace sim {

namespace {

using cplx = std::complex<double>;
using Notes = std::vector<std::string>*;  // where a step's explanation goes (null: nowhere)
[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("calculator: " + what); }
void note(Notes n, const std::string& text) { if (n) n->push_back(text); }

std::string num(cplx v) {
    if (v.imag() == 0) return si(v.real());
    return si(v.real()) + (v.imag() < 0 ? " - j" : " + j") + si(std::abs(v.imag()));
}
std::string pt(double x, double y) { return "(" + si(x) + ", " + si(y) + ")"; }

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

// y at x, linear between the points around it (the scale may run either way, a dc sweep down); `how` says which
cplx at(const Wave& w, double x, const std::string& fn, std::string* how = nullptr) {
    for (size_t k = 1; k < w.x.size(); ++k) {
        const double a = w.x[k - 1], b = w.x[k];
        if ((x - a) * (x - b) > 0) continue;
        const double t = a == b ? 0 : (x - a) / (b - a);
        if (how) *how = t == 0 ? "the point at " + si(a) : t == 1 ? "the point at " + si(b)
                      : "linear between " + si(a) + " (" + num(w.y[k - 1]) + ") and " + si(b) + " (" + num(w.y[k]) + ")";
        return w.y[k - 1] + t * (w.y[k] - w.y[k - 1]);
    }
    fail(fn + ": " + std::to_string(x) + " is outside the waveform (" + std::to_string(w.x.front()) + " .. " + std::to_string(w.x.back()) + ")");
}

// where y crosses level (edge +1 rising, -1 falling, 0 either): x, interpolated between points k-1 and k
struct Cross { double x; size_t k; };
std::vector<Cross> crossings(const std::vector<double>& x, const std::vector<double>& y, double level, int edge) {
    std::vector<Cross> out;
    for (size_t k = 1; k < y.size(); ++k) {
        const double a = y[k - 1] - level, b = y[k] - level;
        const bool rising = a < 0 && b >= 0, falling = a > 0 && b <= 0;
        if ((rising && edge >= 0) || (falling && edge <= 0)) out.push_back({x[k - 1] + (x[k] - x[k - 1]) * a / (a - b), k});
    }
    return out;
}
std::string crossed(const Wave& w, const std::vector<double>& y, double level, const Cross& c) {
    return "crosses " + si(level) + " between " + pt(w.x[c.k - 1], y[c.k - 1]) + " and " + pt(w.x[c.k], y[c.k]) +
           ", linearly at " + si(c.x);
}
double firstCross(const Wave& w, double level, int edge, const std::string& fn, Notes n = nullptr, const std::string& what = "",
                  double after = -HUGE_VAL) {
    const std::vector<double> y = realY(w, fn);
    for (const Cross& c : crossings(w.x, y, level, edge))
        if (c.x >= after) {
            note(n, "  " + what + (what.empty() ? "" : ": ") + crossed(w, y, level, c));
            return c.x;
        }
    fail(fn + ": the waveform never crosses " + si(level) + (edge > 0 ? " rising" : edge < 0 ? " falling" : ""));
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
double between(const Wave& w, double lo, double hi, int need, const std::string& fn, Notes n) {
    const Transition t = transition(w, fn);
    const int dir = t.final > t.initial ? 1 : -1;
    if (need && dir != need) fail(fn + ": the waveform " + (dir > 0 ? "rises" : "falls"));
    note(n, "  initial " + si(t.initial) + " (the first point), final " + si(t.final) + " (the last): it " + (dir > 0 ? "rises" : "falls") +
                " by " + si(std::abs(t.final - t.initial)));
    const double a = firstCross(w, t.initial + lo / 100 * (t.final - t.initial), dir, fn, n, si(lo) + "% of the transition");
    const double b = firstCross(w, t.initial + hi / 100 * (t.final - t.initial), dir, fn, n, si(hi) + "% of the transition", a);
    note(n, "  " + si(b) + " - " + si(a) + " = " + si(b - a));
    return b - a;
}

// what a value is, for a step: a number, or a waveform's extent
std::string describe(const Wave& w) {
    if (w.scalar()) return num(w.y[0]);
    bool complex = false;
    for (const cplx v : w.y) complex = complex || v.imag() != 0;
    return "a waveform of " + std::to_string(w.x.size()) + " points over " + (w.scale.empty() ? "x" : w.scale) + " " + si(w.x.front()) + " .. " +
           si(w.x.back()) + (complex ? " (complex)" : "");
}

class Parser {
public:
    Parser(const std::string& text, const Lookup& lookup, Notes notes) : s_(text), lookup_(lookup), n_(notes) {}
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
    Notes n_;
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
    Val arith(const Val& a, const Val& b, char op, bool negate = false) {
        const Wave& x = wave(a, std::string(1, op));
        const Wave& y = wave(b, std::string(1, op));
        Wave r = combine(x, y, op);
        if (negate) note(n_, "-(" + describe(y) + ") = " + describe(r));
        else if (r.scalar()) note(n_, num(x.y[0]) + " " + op + " " + num(y.y[0]) + " = " + num(r.y[0]));
        else note(n_, (x.scalar() ? num(x.y[0]) : "the waveform") + std::string(" ") + op + " " + (y.scalar() ? num(y.y[0]) : "the waveform") +
                          ", point by point: " + describe(r));
        return {r, "", false};
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
        if (eat('-')) return arith({number(0), "", false}, unary(), '-', true);
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
        static const std::set<std::string> access = {"VT", "VF", "VDC", "VOP", "IT", "IF", "IDC", "IOP", "VN", "VNTOT", "STB", "VPSS", "IPSS", "VH", "IH", "VPAC", "IPAC", "VPN", "VHB", "IHB", "VHBT", "IHBT"};
        if (access.count(name)) {  // its argument is a name, taken as written
            const size_t e = s_.find(')', i_);
            if (e == std::string::npos) fail(name + "( has no ')'");
            std::string arg = s_.substr(i_, e - i_);
            i_ = e + 1;
            arg.erase(0, arg.find_first_not_of(" \t\""));
            arg.erase(arg.find_last_not_of(" \t\"") + 1);
            if (arg.empty()) fail(name + "() names nothing");
            Wave w = lookup_(name, arg);
            note(n_, name + "(" + arg + ") = " + describe(w));
            return {std::move(w), "", false};
        }
        std::vector<Val> args;
        std::vector<std::string> texts;  // each argument as written, for its step
        if (!eat(')')) {
            do {
                skip();
                const size_t from = i_;
                args.push_back(expr());
                std::string t = s_.substr(from, i_ - from);
                t.erase(t.find_last_not_of(" \t") + 1);
                texts.push_back(t);
            } while (eat(','));
            expect(')');
        }
        return {call(name, args, texts), "", false};
    }

    // a function's value, and its step: what it computed from, and how
    Wave call(const std::string& fn, const std::vector<Val>& a, const std::vector<std::string>& texts) {
        std::vector<std::string> how;
        Wave r = compute(fn, a, n_ ? &how : nullptr);
        std::string args;
        for (size_t k = 0; k < texts.size(); ++k) args += (k ? ", " : "") + texts[k];
        note(n_, fn + "(" + args + ") = " + describe(r));
        if (n_) n_->insert(n_->end(), how.begin(), how.end());
        return r;
    }

    static Wave compute(const std::string& fn, const std::vector<Val>& a, Notes n) {
        auto arity = [&](size_t lo, size_t hi) {
            if (a.size() < lo || a.size() > hi)
                fail(fn + " takes " + (lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + std::to_string(hi)) + " argument(s)");
        };
        auto opt = [&](size_t k, double dflt) { return k < a.size() ? real(a[k], fn) : dflt; };
        auto pointwise = [&](const std::function<cplx(cplx)>& f) {
            arity(1, 1);
            static const std::map<std::string, std::string> what = {
                {"mag", "|y|"}, {"abs", "|y|"}, {"real", "Re y"}, {"imag", "Im y"}, {"phase", "arg y in degrees (−180 .. 180)"},
                {"sqrt", "√y"}, {"dB20", "20·log10|y|"}, {"dB10", "10·log10|y|"}, {"log10", "log10 y"}};
            const Wave& w = wave(a[0], fn);
            note(n, "  " + what.at(fn) + (w.scalar() ? " of " + num(w.y[0]) : " at each of its " + std::to_string(w.y.size()) + " points"));
            return map(w, f);
        };
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
            std::string how;
            const cplx v = at(waveform(a[0], fn), real(a[1], fn), fn, &how);
            note(n, "  at " + si(real(a[1], fn)) + ": " + how);
            return number(v);
        }
        if (fn == "harmonic") {  // a PSS harmonics waveform's k-th point: the phasor at k fund
            arity(2, 2);
            const Wave& w = waveform(a[0], fn);
            const double k = real(a[1], fn);
            if (k != std::floor(k) || k < 0 || k >= (double)w.y.size()) fail("harmonic: k is a whole number 0 .. " + std::to_string(w.y.size() - 1));
            note(n, "  harmonic " + num(k) + " of " + std::to_string(w.y.size() - 1) + ", at " + si(w.x[(size_t)k]) + " " + w.scale);
            return number(w.y[(size_t)k]);
        }
        if (fn == "fundamental") {  // a PSS harmonics waveform's first harmonic frequency (an oscillator's, found)
            arity(1, 1);
            const Wave& w = waveform(a[0], fn);
            if (w.x.size() < 2) fail("fundamental needs a harmonics waveform (VH or IH)");
            note(n, "  the frequency of harmonic 1: " + si(w.x[1]) + " " + w.scale);
            return number(w.x[1]);
        }
        if (fn == "thd") {  // percent: the harmonics above the first against the first
            arity(1, 1);
            const Wave& w = waveform(a[0], fn);
            if (w.y.size() < 3 || std::abs(w.y[1]) == 0) fail("thd needs harmonics 0 .. 2 at least and a first harmonic");
            double sum = 0;
            for (size_t k = 2; k < w.y.size(); ++k) sum += std::norm(w.y[k]);
            const double t = 100 * std::sqrt(sum) / std::abs(w.y[1]);
            note(n, "  100·√(Σ |V_k|², k = 2 .. " + std::to_string(w.y.size() - 1) + ") / |V_1| = 100·" + si(std::sqrt(sum)) + " / " + si(std::abs(w.y[1])));
            return number(t);
        }
        if (fn == "ymax" || fn == "ymin") {
            arity(1, 1);
            const Wave& w = waveform(a[0], fn);
            const auto y = realY(w, fn);
            const auto it = fn == "ymax" ? std::max_element(y.begin(), y.end()) : std::min_element(y.begin(), y.end());
            note(n, std::string("  the ") + (fn == "ymax" ? "largest" : "smallest") + " of its " + std::to_string(y.size()) + " points, at " +
                        si(w.x[(size_t)(it - y.begin())]));
            return number(*it);
        }
        if (fn == "average" || fn == "rms") {
            arity(1, 1);
            const Wave& w = waveform(a[0], fn);
            std::vector<double> y = realY(w, fn);
            if (fn == "rms") for (double& v : y) v *= v;
            const double m = integral(w.x, y) / (w.x.back() - w.x.front());
            note(n, "  the trapezoidal integral of " + std::string(fn == "rms" ? "y²" : "y") + " over " + si(w.x.front()) + " .. " + si(w.x.back()) +
                        ", divided by its span " + si(w.x.back() - w.x.front()) + (fn == "rms" ? " = " + si(m) + ", then its square root" : ""));
            return number(fn == "rms" ? std::sqrt(m) : m);
        }
        if (fn == "cross") {
            arity(2, 4);
            const Wave& w = waveform(a[0], fn);
            const int k = (int)opt(2, 1);
            const std::string edge = a.size() > 3 ? a[3].s : "either";
            if (a.size() > 3 && (!a[3].isString || (edge != "rising" && edge != "falling" && edge != "either")))
                fail("cross: the edge is \"rising\", \"falling\" or \"either\"");
            const auto y = realY(w, fn);
            const auto c = crossings(w.x, y, real(a[1], fn), edge == "rising" ? 1 : edge == "falling" ? -1 : 0);
            if (k < 1 || (size_t)k > c.size()) fail("cross: no crossing " + std::to_string(k) + " (there are " + std::to_string(c.size()) + ")");
            note(n, "  crossing " + std::to_string(k) + " of " + std::to_string(c.size()) + " (" + edge + "): " + crossed(w, y, real(a[1], fn), c[(size_t)k - 1]));
            return number(c[(size_t)k - 1].x);
        }
        if (fn == "riseTime" || fn == "fallTime") {
            arity(1, 3);
            return number(between(waveform(a[0], fn), opt(1, 10), opt(2, 90), fn == "riseTime" ? 1 : -1, fn, n));
        }
        if (fn == "slewRate") {
            arity(1, 3);
            const Wave& w = waveform(a[0], fn);
            const double lo = opt(1, 10), hi = opt(2, 90);
            const Transition t = transition(w, fn);
            const double dt = between(w, lo, hi, 0, fn, n), dv = (hi - lo) / 100 * std::abs(t.final - t.initial);
            note(n, "  " + si(hi - lo) + "% of the transition, " + si(dv) + ", over " + si(dt));
            return number(dv / dt);
        }
        if (fn == "overshoot") {
            arity(1, 1);
            const Wave& w = waveform(a[0], fn);
            const Transition t = transition(w, fn);
            const auto y = realY(w, fn);
            const double peak = t.final > t.initial ? *std::max_element(y.begin(), y.end()) : *std::min_element(y.begin(), y.end());
            note(n, "  initial " + si(t.initial) + ", final " + si(t.final) + ", peak " + si(peak) + ": (" + si(peak) + " - " + si(t.final) + ") / (" +
                        si(t.final) + " - " + si(t.initial) + ") · 100, at least 0");
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
            const double x = w.x[k] + (w.x[k + 1] - w.x[k]) * e0 / (e0 - e1);
            note(n, "  final " + si(t.final) + ", band ±" + si(band) + " (" + si(opt(1, 1)) + "% of the " + si(std::abs(t.final - t.initial)) +
                        " transition); it last leaves the band between " + pt(w.x[k], y[k]) + " and " + pt(w.x[k + 1], y[k + 1]) + ", at " + si(x) +
                        "; minus the start " + si(w.x.front()));
            return number(x - w.x.front());
        }
        if (fn == "delay") {
            arity(4, 4);
            const double t1 = firstCross(waveform(a[0], fn), real(a[1], fn), 0, fn, n, "the first waveform");
            const double t2 = firstCross(waveform(a[2], fn), real(a[3], fn), 0, fn, n, "the second");
            note(n, "  " + si(t2) + " - " + si(t1));
            return number(t2 - t1);
        }
        if (fn == "bandwidth" || fn == "ugf" || fn == "phaseMargin" || fn == "gainMargin") {
            fn == "bandwidth" ? arity(1, 2) : arity(1, 1);
            const Wave& w = waveform(a[0], fn);
            const Wave m{w.x, {}};
            Wave mdb = m;
            for (const double d : db20(w, fn)) mdb.y.push_back(d);
            if (fn == "bandwidth") {
                note(n, "  20·log10|y| at the first point (" + si(w.x[0]) + ") is " + si(mdb.y[0].real()) + " dB; " + si(opt(1, 3)) + " dB below is " +
                            si(mdb.y[0].real() - opt(1, 3)) + " dB");
                return number(firstCross(mdb, mdb.y[0].real() - opt(1, 3), -1, fn, n, "the gain in dB"));
            }
            Wave ph = m;
            for (const double p : unwrappedPhase(w)) ph.y.push_back(p);
            if (fn == "gainMargin") {
                const double target = ph.y[0].real() > -180 ? -180 : 180;
                const double f180 = firstCross(ph, target, 0, fn, n, "the phase (unwrapped, degrees)");
                std::string how;
                const double g = at(mdb, f180, fn, &how).real();
                note(n, "  the gain there: " + si(g) + " dB (" + how + "); the margin is its negative");
                return number(-g);
            }
            const double u = firstCross(mdb, 0, 0, fn, n, "the gain in dB (unity gain)");
            if (fn == "ugf") return number(u);
            std::string how;
            const double p = at(ph, u, fn, &how).real();
            note(n, "  the phase there (unwrapped): " + si(p) + "° (" + how + "); 180 + " + si(p));
            return number(180 + p);
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

Wave evaluate(const std::string& expression, const Lookup& lookup, std::vector<std::string>* steps) {
    return Parser(expression, lookup, steps).parse();
}

std::string si(double v, int digits) {
    if (v == 0 || !std::isfinite(v)) return v == 0 ? "0" : std::to_string(v);
    static const std::pair<double, const char*> prefixes[] = {{1e12, "T"}, {1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"},
                                                              {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}, {1e-18, "a"}};
    for (const auto& [m, p] : prefixes)
        if (std::abs(v) >= m * 0.9999999) {
            char b[64];
            std::snprintf(b, sizeof b, "%.*g%s", digits, v / m, p);
            return b;
        }
    char b[64];
    std::snprintf(b, sizeof b, "%.*g", digits, v);
    return b;
}

namespace {
const Plot* plot(const std::vector<Plot>& plots, const std::string& name) {
    for (const auto& p : plots) if (p.name == name) return &p;
    return nullptr;
}

Wave access(const std::vector<Plot>& plots, const std::string& kind, const std::string& arg) {
    static const std::map<std::string, std::string> plotOf = {
        {"VT", "Transient Analysis"}, {"IT", "Transient Analysis"}, {"VF", "AC Analysis"}, {"IF", "AC Analysis"},
        {"VDC", "DC transfer characteristic"}, {"IDC", "DC transfer characteristic"}, {"VOP", "Operating Point"}, {"IOP", "Operating Point"},
        {"VN", "Noise Spectral Density Curves"}, {"VNTOT", "Integrated Noise"}, {"STB", "Stability Analysis"},
        {"VPSS", "Periodic Steady State"}, {"IPSS", "Periodic Steady State"}, {"VH", "PSS Harmonics"}, {"IH", "PSS Harmonics"},
        {"VPN", "Periodic Noise"}, {"VHB", "HB Spectrum"}, {"IHB", "HB Spectrum"}, {"VHBT", "HB Time"}, {"IHBT", "HB Time"}};
    std::string a = arg;
    for (auto& c : a) c = (char)std::tolower((unsigned char)c);
    std::string plotName = plotOf.count(kind) ? plotOf.at(kind) : "";
    if (kind == "VPAC" || kind == "IPAC") {  // VPAC(net, m): the pac sideband m, at f + m fund
        const size_t comma = a.find(',');
        std::string m = comma == std::string::npos ? "" : a.substr(comma + 1);
        m.erase(0, m.find_first_not_of(" \t"));
        m.erase(m.find_last_not_of(" \t") + 1);
        static const std::regex whole("^[+-]?[0-9]+$");
        if (!std::regex_match(m, whole)) throw std::runtime_error("calculator: " + kind + "(" + arg + "): a net and a sideband number");
        a = a.substr(0, comma);
        a.erase(a.find_last_not_of(" \t") + 1);
        plotName = "PAC Sideband " + std::to_string(std::stoi(m));
    }
    const Plot* p = plot(plots, plotName);
    if (!p) throw std::runtime_error("calculator: " + kind + "(" + arg + "): the test ran no " + plotName);
    std::string want;
    if (kind == "STB") {
        want = a;  // loopgain
    } else if (kind == "VPN") {
        if (a != "out") throw std::runtime_error("calculator: VPN(" + arg + "): out (the pnoise output's density)");
        want = "onoise_spectrum";
    } else if (kind == "VN" || kind == "VNTOT") {
        if (a != "out" && a != "in") throw std::runtime_error("calculator: " + kind + "(" + arg + "): out or in");
        want = std::string(1, a[0]) + (kind == "VN" ? "noise_spectrum" : "noise_total");  // onoise_spectrum, inoise_total, ...
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
