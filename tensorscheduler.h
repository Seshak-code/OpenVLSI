#pragma once
#include "circuit.h"
#include <vector>
#include <map>
#include <string>

/**
 * @file tensorscheduler.h
 * @brief Tensor Scheduler - Manages batching and structural reuse.
 */

struct SolverJob {
    std::string blockName;       // The TensorBlock template being solved
    std::vector<int> instanceIds; // Indices of instances in TensorNetlist::instances
};

class TensorScheduler {
public:
    std::vector<SolverJob> jobs;
    
    /**
     * @brief Analyzes the netlist and groups identical blocks into jobs.
     * 
     * This is the first step toward "Tensor-indexed" solving. By grouping
     * instances of the same block, we prepare for future optimizations like
     * symbolic factorization reuse.
     */
    void schedule(const TensorNetlist& netlist) {
        jobs.clear();
        std::map<std::string, std::vector<int>> groupedInstances;
        
        // Group instances by their block name
        for (int i = 0; i < (int)netlist.instances.size(); ++i) {
            groupedInstances[netlist.instances[i].blockName].push_back(i);
        }
        
        // Create a SolverJob for each unique block type
        for (auto const& [name, instances] : groupedInstances) {
            jobs.push_back({name, instances});
        }
    }
};
