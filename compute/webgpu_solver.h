#pragma once

#include "physics_tensors.h"
#include "circuit.h"
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5_webgpu.h>

/**
 * WebGPUSolver
 * Implements parallel circuit solving using WGSL Compute Shaders.
 * 
 * Pipeline:
 * 1. uploadNetlist: Transfers SoA tensors to GPU Storage Buffers.
 * 2. runMatrixAssembly: Parallel stamping (one thread per component).
 * 3. runPCGIteration: Parallel Conjugate Gradient solve.
 * 4. downloadSolution: Fetches voltage vector back to CPU.
 */
class WebGPUSolver {
public:
    WebGPUSolver();
    ~WebGPUSolver();

    bool initialize(const TensorNetlist& netlist);
    
    // Core Solver Loop Steps
    void uploadState(const std::vector<double>& currentVoltages);
    void runMatrixAssembly(); 
    void runSolverSteps(int maxIterations);
    std::vector<double> downloadSolution();

private:
    // WebGPU Handles
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    
    // Compute Pipelines
    WGPUComputePipeline stampingPipeline = nullptr;
    WGPUComputePipeline solverPipeline = nullptr;
    
    // GPU Buffers
    WGPUBuffer voltageBuffer = nullptr;
    WGPUBuffer rhsBuffer = nullptr;
    WGPUBuffer matrixBuffer = nullptr;
    
    // High Precision Dual-Scale Buffers
    WGPUBuffer rhsBufferFine = nullptr;
    WGPUBuffer matrixBufferFine = nullptr;
    
    // Component Buffers (SoA)
    WGPUBuffer resistorBuffer = nullptr;
    WGPUBuffer diodeBuffer = nullptr;

    bool initWebGPU();
    void createBuffers(const TensorNetlist& netlist);
    void createPipelines();
};

#endif // __EMSCRIPTEN__
