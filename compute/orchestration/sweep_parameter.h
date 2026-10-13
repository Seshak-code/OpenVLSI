#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace acutesim::compute::orchestration {

// A parametric sweep: linear (start..stop by step, inclusive) or log (step = point count).
struct SweepParameter {
    std::string variableName;
    double start = 0.0;
    double stop  = 0.0;
    double step  = 0.0;
    bool isLog   = false;

    std::vector<double> generatePoints() const {
        std::vector<double> pts;
        if (step == 0) return pts;
        if (isLog) {
            const int n = std::max(2, static_cast<int>(step));
            const double a = std::log10(start), d = (std::log10(stop) - a) / (n - 1);
            for (int i = 0; i < n; ++i) pts.push_back(std::pow(10.0, a + i * d));
            return pts;
        }
        const double span = (stop - start) / step;   // index-based: no accumulated drift
        if (!(span >= 0)) return {start};
        for (long i = 0, n = static_cast<long>(std::floor(span + 1e-9)); i <= n; ++i)
            pts.push_back(start + i * step);
        return pts;
    }
};

} // namespace acutesim::compute::orchestration
