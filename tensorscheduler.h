#pragma once
#include "circuit.h"
#include <vector>
#include <map>
#include <string>
#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/**
 * @file tensorscheduler.h
 * @brief Tensor Scheduler - Manages batching, structural reuse, and hardware selection.
 */

// Hardware Backend Types
enum class SolverBackend {
    CPU_SEQUENTIAL,  // Baseline (for debugging)
    CPU_OPENMP,      // Multi-threaded CPU
    WEBGPU_COMPUTE   // Future: GLSL/WGSL Compute Shaders
};

struct SolverJob {
    const TensorBlock* blockTmpl;  // Pointer for fast access (reduces lookups)
    std::vector<int> instanceIds;  // Indices of instances in TensorNetlist::instances
};

class HardwareManager {
public:
    static SolverBackend detectBestBackend() {
        #ifdef __EMSCRIPTEN__
            // In WebAssembly, OpenMP is disabled by default unless specifically compiled for.
            // For now, we default to Sequential CPU.
            // Future: Check navigator.gpu for WebGPU support.
            return SolverBackend::CPU_SEQUENTIAL;
        #else
            // Native C++ typically implies multi-core
            return SolverBackend::CPU_OPENMP;
        #endif
    }
};

class TensorScheduler {
public:
    std::vector<SolverJob> jobs;
    SolverBackend activeBackend;
    
    TensorScheduler() {
        activeBackend = HardwareManager::detectBestBackend();
    }
    
    /**
     * @brief Analyzes the netlist and groups identical blocks into jobs.
     * 
     * Optimizations:
     * 1. Groups by Block Name (Topology).
     * 2. Stores direct pointers to TensorBlock to avoid lookups during solve.
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
            // Find the template block ONE time
            auto it = netlist.blockDefinitions.find(name);
            if (it != netlist.blockDefinitions.end()) {
                jobs.push_back({ &(it->second), instances });
            }
        }
    }
};
