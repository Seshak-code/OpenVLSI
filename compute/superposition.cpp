#include "superposition.h"
#include <algorithm>

std::vector<SuperpositionSolver::SubProblem> SuperpositionSolver::decompose(const TensorNetlist& original, Mode mode) {
    std::vector<SubProblem> problems;
    
    // COMPONENT LEVEL: Decompose Ideal Independent Sources
    if (mode == Mode::COMPONENT_LEVEL) {
        const auto& vSources = original.globalBlock.voltageSources;
        const auto& cSources = original.globalBlock.currentSources;
        
        size_t totalSources = vSources.size() + cSources.size();
        if (totalSources == 0) return problems;

        auto createIdealSub = [&](std::string activeName, bool isVolt, int idx) {
            SubProblem sub;
            sub.sourceName = activeName;
            sub.netlist = original; // Deep copy
            
            // Zero out inactive Voltage Sources
            for (size_t i = 0; i < sub.netlist.globalBlock.voltageSources.size(); ++i) {
                if (! (isVolt && (int)i == idx) ) {
                    sub.netlist.globalBlock.voltageSources[i].voltage_V = 0.0; // Short Circuit
                }
            }
            
            // Zero out inactive Current Sources
            for (size_t i = 0; i < sub.netlist.globalBlock.currentSources.size(); ++i) {
                if( ! (!isVolt && (int)i == idx) ) {
                    sub.netlist.globalBlock.currentSources[i].current_A = 0.0; // Open Circuit
                }
            }
            // In component mode, we assume System Sources are OFF or ignored,
            // or the user hasn't added them yet. For safety, we can silence them too?
            // For now, assume Component Mode only uses ideal sources.
            return sub;
        };

        for (size_t i = 0; i < vSources.size(); ++i) problems.push_back(createIdealSub("V" + std::to_string(i), true, i));
        for (size_t i = 0; i < cSources.size(); ++i) problems.push_back(createIdealSub("I" + std::to_string(i), false, i));
    
    } 
    // SYSTEM LEVEL: Decompose Power Rails and Signal Generators
    else if (mode == Mode::SYSTEM_LEVEL) {
        const auto& rails = original.globalBlock.powerRails;
        const auto& signals = original.globalBlock.signalSources;
        
        // Helper to kill system sources while keeping parasitics (ESR/Decap) active
        auto createSystemSub = [&](std::string activeName, int railIdx, int sigIdx) {
            SubProblem sub;
            sub.sourceName = activeName;
            sub.netlist = original;
            
            // Handle Power Rails
            for (size_t i=0; i < sub.netlist.globalBlock.powerRails.size(); ++i) {
                if ((int)i != railIdx) {
                    // Turn OFF source contribution, keep network interactions
                    sub.netlist.globalBlock.powerRails[i].nominal_V = 0.0;
                    sub.netlist.globalBlock.powerRails[i].ripple_Vpp = 0.0;
                    // ESR, ESL, Decap are PHYSICAL properties and remain active.
                }
            }
            
            // Handle Signal Sources
            for (size_t i=0; i < sub.netlist.globalBlock.signalSources.size(); ++i) {
                if ((int)i != sigIdx) {
                    sub.netlist.globalBlock.signalSources[i].amplitude_V = 0.0;
                    sub.netlist.globalBlock.signalSources[i].offset_V = 0.0;
                    // Quiet line driver
                }
            }
            return sub;
        };
        
        for (size_t i=0; i < rails.size(); ++i) problems.push_back(createSystemSub(rails[i].name, i, -1));
        for (size_t i=0; i < signals.size(); ++i) problems.push_back(createSystemSub(signals[i].name, -1, i));
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
