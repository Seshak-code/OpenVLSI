#include "superposition.h"
#include <algorithm>

std::vector<SuperpositionSolver::SubProblem> SuperpositionSolver::decompose(const TensorNetlist& original) {
    std::vector<SubProblem> problems;
    
    // 1. Identify all independent sources
    const auto& vSources = original.globalBlock.voltageSources;
    const auto& cSources = original.globalBlock.currentSources;
    
    // Total sources
    size_t totalSources = vSources.size() + cSources.size();
    if (totalSources == 0) return problems;

    auto createSubProblem = [&](std::string activeName, bool isVolt, int idx) {
        SubProblem sub;
        sub.sourceName = activeName;
        sub.netlist = original; // Deep copy
        
        // Zero out inactive Voltage Sources
        for (size_t i = 0; i < sub.netlist.globalBlock.voltageSources.size(); ++i) {
            bool isActive = (isVolt && (int)i == idx);
            if (!isActive) {
                // Short Circuit: 0V
                sub.netlist.globalBlock.voltageSources[i].voltage_V = 0.0;
            }
        }
        
        // Zero out inactive Current Sources
        for (size_t i = 0; i < sub.netlist.globalBlock.currentSources.size(); ++i) {
            bool isActive = (!isVolt && (int)i == idx);
            if (!isActive) {
                // Open Circuit: 0A
                sub.netlist.globalBlock.currentSources[i].current_A = 0.0;
            }
        }
        
        return sub;
    };
    
    // Generate sub-problem for each Voltage Source
    for (size_t i = 0; i < vSources.size(); ++i) {
        problems.push_back(createSubProblem("V" + std::to_string(i), true, i));
    }
    
    // Generate sub-problem for each Current Source
    for (size_t i = 0; i < cSources.size(); ++i) {
        problems.push_back(createSubProblem("I" + std::to_string(i), false, i));
    }
    
    return problems;
}

std::vector<double> SuperpositionSolver::combine(const std::vector<SubProblem>& results) {
    if (results.empty()) return {};
    
    // Initialize sum with the first solution (or zeros if first solution is empty/invalid)
    size_t vecSize = 0;
    for(const auto& r : results) {
        if (!r.solution.empty()) {
            vecSize = r.solution.size();
            break;
        }
    }
    
    if (vecSize == 0) return {};
    
    std::vector<double> total(vecSize, 0.0);
    
    for (const auto& r : results) {
        if (r.solution.size() != vecSize) continue; // Skip mismatch
        
        for (size_t i = 0; i < vecSize; ++i) {
            total[i] += r.solution[i];
        }
    }
    
    return total;
}
