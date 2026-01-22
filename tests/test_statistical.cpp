#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <numeric>
#include "../physics/statistical_engine.h"

// Helper to calculate mean and stddev
std::pair<double, double> getStats(const std::vector<double>& v) {
    double sum = std::accumulate(v.begin(), v.end(), 0.0);
    double mean = sum / v.size();
    
    double sq_sum = std::inner_product(v.begin(), v.end(), v.begin(), 0.0);
    double stdev = std::sqrt(sq_sum / v.size() - mean * mean);
    
    return {mean, stdev};
}

int main() {
    std::cout << "[TEST] Verifying Statistical Engine (LHS)..." << std::endl;
    
    Statistics::RandomEngine rng;
    int nSamples = 1000;
    
    // 1. Gaussian Test
    {
        double targetMean = 100.0;
        double targetSigma = 10.0;
        auto dist = Statistics::Distribution::Gaussian(targetMean, targetSigma);
        auto samples = rng.sampleLHS(dist, nSamples);
        
        auto [mean, sigma] = getStats(samples);
        std::cout << "  -> Gaussian(100, 10): Mean=" << mean << " Sigma=" << sigma << " ... ";
        
        // LHS should be very accurate even with 1000 samples
        assert(std::abs(mean - targetMean) < 0.5); // < 0.5% error
        assert(std::abs(sigma - targetSigma) < 0.5);
        std::cout << "PASS" << std::endl;
    }
    
    // 2. Uniform Test
    {
        double min = 0.0;
        double max = 10.0;
        auto dist = Statistics::Distribution::Uniform(min, max);
        auto samples = rng.sampleLHS(dist, nSamples);
        
        auto [mean, sigma] = getStats(samples);
        double expectedMean = 5.0;
        // Variance of uniform is (max-min)^2 / 12 = 100/12 = 8.333 -> Sigma = 2.886
        double expectedSigma = std::sqrt(100.0/12.0);
        
        std::cout << "  -> Uniform(0, 10): Mean=" << mean << " Sigma=" << sigma << " ... ";
        assert(std::abs(mean - expectedMean) < 0.1);
        assert(std::abs(sigma - expectedSigma) < 0.1);
        std::cout << "PASS" << std::endl;
    }
    
    std::cout << "[TEST] Statistical Engine Verified Successfully!" << std::endl;
    return 0;
}
