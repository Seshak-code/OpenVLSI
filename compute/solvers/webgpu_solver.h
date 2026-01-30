#pragma once

#include "physics_tensors.h"
#include "../../components/circuit.h"
#include <vector>
#include <string>

#ifdef __EMSCRIPTEN__
#include <webgpu/webgpu.h>
#else
#include <webgpu/webgpu.h>
#include <dawn/native/DawnNative.h>
#include <dawn/webgpu_cpp.h>
#include <dawn/dawn_proc.h>
#endif

/**
 * WebGPUSolver
 * Implements GPU-resident Newton-Raphson simulation using EmulatedF64 logic.
 * 
 * Features:
 * - Persistent GPU Residency: All MNA buffers stay on GPU.
 * - EmulatedF64 Architecture: hi-lo float32 pairs for double-precision parity.
 * - Target: Emscripten / WebAssembly.
 */
class WebGPUSolver {
public:
    WebGPUSolver();
    ~WebGPUSolver();

    bool initialize(const TensorNetlist& netlist);
    
    // Core Solver Loop (Persistent residency)
    void uploadNetlist(const TensorNetlist& netlist);
    void runNRStep(double time, double h);
    bool checkConvergence();
    
    std::vector<double> downloadSolution();
    std::vector<std::vector<double>> downloadWaveform(); // Phase 2.2

private:
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUInstance instance = nullptr;

    // Persistent Buffers (EmulatedF64 split)
    WGPUBuffer voltageBufferHi = nullptr;
    WGPUBuffer voltageBufferLo = nullptr;
    WGPUBuffer deltaVBufferHi = nullptr;
    WGPUBuffer deltaVBufferLo = nullptr;
    WGPUBuffer rhsBufferHi = nullptr;
    WGPUBuffer rhsBufferLo = nullptr;
    WGPUBuffer jacobianBufferHi = nullptr;
    WGPUBuffer jacobianBufferLo = nullptr;
    
    // Topology (Frozen)
    WGPUBuffer csrRowPtrBuffer = nullptr;
    WGPUBuffer csrColIdxBuffer = nullptr;
    WGPUBuffer diodeMapBuffer = nullptr;
    WGPUBuffer mosfetMapBuffer = nullptr;
    WGPUBuffer globalStateBuffer = nullptr; // Phase 2.1

    // Component SoA Buffers
    WGPUBuffer diodeSoABuffer = nullptr;
    WGPUBuffer mosfetSoABuffer = nullptr;

    // Waveform Buffering (Phase 2.2)
    WGPUBuffer waveformConfigBuffer = nullptr;
    WGPUBuffer waveformStateBuffer = nullptr;
    WGPUBuffer waveformDataBuffer = nullptr;

    // Persistent Bind Groups
    WGPUBindGroup bindGroup0 = nullptr;
    WGPUBindGroup bindGroup1 = nullptr;
    WGPUBindGroup bindGroup2 = nullptr; // Waveform group

    // Pipelines
    WGPUComputePipeline physicsPipelineDiodes = nullptr;
    WGPUComputePipeline physicsPipelineMosfets = nullptr;
    WGPUComputePipeline assemblyPipeline = nullptr;
    WGPUComputePipeline solverPipeline = nullptr;
    WGPUComputePipeline convergencePipeline = nullptr;
    WGPUComputePipeline recordWaveformPipeline = nullptr;

    bool initWebGPU();
    void setupResources(const TensorNetlist& netlist);
    WGPUShaderModule loadShader(const std::string& path);
};
