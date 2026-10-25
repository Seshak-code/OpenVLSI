#pragma once
// The waveform calculator (Cadence's): expressions over a run's results, for ADE outputs and the waveform viewer.
//
//   access   VT(net) VF(net) VDC(net) VOP(net)  a node voltage in the tran, ac, dc or operating-point results
//            IT(src) IF(src) IDC(src) IOP(src)  a source's current
//            VN(out) VN(in)                     the output / input-referred noise density; VNTOT(out|in) its total
//   numbers  1.5k 10meg 2u 1e-9 (SPICE suffixes); + - * / ^, unary -, parentheses
//   any      value(w, x)  mag(w)  dB20(w)  dB10(w)  phase(w) (degrees)  real(w)  imag(w)  abs(w)  sqrt(w)  log10(w)
//   real w   ymax(w)  ymin(w)  average(w)  rms(w)  cross(w, level [, n [, "rising"|"falling"|"either"]])
//            riseTime(w [, lo%, hi%])  fallTime(w [, lo%, hi%]) (percent of the transition)  slewRate(w [, lo%, hi%])
//            overshoot(w)
//            settlingTime(w [, percent])  delay(w1, level1, w2, level2)
//   ac w     bandwidth(w [, dB])  ugf(w)  phaseMargin(w)  gainMargin(w)
//
// Waveforms combine pointwise when they share a scale; a number combines with anything. Every failure (an unknown
// function or vector, no crossing, a complex waveform where a real one is needed) is an error, never a NaN.
#include "sim/run.h"

#include <complex>
#include <functional>
#include <string>
#include <vector>

namespace sim {

struct Wave {  // a number (x empty, one y) or a waveform y(x)
    std::vector<double> x;
    std::vector<std::complex<double>> y;
    std::string scale;  // a waveform's x: time, frequency, v-sweep, ...
    bool scalar() const { return x.empty(); }
};

double siNumber(const std::string& s);  // "1.5k", "10meg", "2u", "1e-9"; anything else throws

// kind is the access function's name (VT, VF, ...) and name its argument; it throws when the results lack it
using Lookup = std::function<Wave(const std::string& kind, const std::string& name)>;

Wave evaluate(const std::string& expression, const Lookup& lookup);

// The access functions over a run's results (plots is read, not copied: it outlives the lookup). VT/IT the transient,
// VF/IF the AC, VDC/IDC the DC sweep, VOP/IOP the operating point, VN/VNTOT the noise; names ignore case.
Lookup resultsLookup(const std::vector<Plot>& plots);

}  // namespace sim
