#include "webgpu_solver.h"
#include <iostream>
#include <vector>
#include <cstring>

// Helper for Dawn's new WGPUStringView API
#define WGPU_SV(str) { str, strlen(str) }

#ifdef __EMSCRIPTEN__
#include <webgpu/webgpu.h>
#include <emscripten.h>
#else
#include <webgpu/webgpu.h>
#include <dawn/native/DawnNative.h>
#include <dawn/webgpu_cpp.h>
#include <dawn/dawn_proc.h>
#endif

// --- Device Structs matching WGSL AoS format ---
struct DiodeDevice {
    int32_t anode, cathode;
    float Is_hi, Is_lo;
    float N_hi, N_lo;
    float Vt_hi, Vt_lo;
    float v_d_hi, v_d_lo;
    float i_d_hi, i_d_lo;
    float g_d_hi, g_d_lo;
};

struct MosfetDevice {
    int32_t drain, gate, source, body;
    float W_hi, W_lo;
    float L_hi, L_lo;
    float Kp_hi, Kp_lo;
    float Vth_hi, Vth_lo;
    float lambda_hi, lambda_lo;
    uint32_t isPMOS;
    float vgs_hi, vgs_lo;
    float vds_hi, vds_lo;
    float vbs_hi, vbs_lo;
    float ids_hi, ids_lo;
    float gm_hi, gm_lo;
    float gmb_hi, gmb_lo;
    float gds_hi, gds_lo;
};

struct DiodeStampMap {
    uint32_t aa, cc, ac, ca;
};
struct MosfetStampMap {
    uint32_t dd, dg, ds, db;
    uint32_t sd, sg, ss, sb;
};

WebGPUSolver::WebGPUSolver() {
    initWebGPU();
}

WebGPUSolver::~WebGPUSolver() {
    // Component Buffers
    if (diodeSoABuffer) wgpuBufferRelease(diodeSoABuffer);
    if (mosfetSoABuffer) wgpuBufferRelease(mosfetSoABuffer);
    
    // MNA Buffers
    if (voltageBufferHi) wgpuBufferRelease(voltageBufferHi);
    if (voltageBufferLo) wgpuBufferRelease(voltageBufferLo);
    if (deltaVBufferHi) wgpuBufferRelease(deltaVBufferHi);
    if (deltaVBufferLo) wgpuBufferRelease(deltaVBufferLo);
    if (rhsBufferHi) wgpuBufferRelease(rhsBufferHi);
    if (rhsBufferLo) wgpuBufferRelease(rhsBufferLo);
    if (jacobianBufferHi) wgpuBufferRelease(jacobianBufferHi);
    if (jacobianBufferLo) wgpuBufferRelease(jacobianBufferLo);

    if (diodeMapBuffer) wgpuBufferRelease(diodeMapBuffer);
    if (mosfetMapBuffer) wgpuBufferRelease(mosfetMapBuffer);

    if (bindGroup0) wgpuBindGroupRelease(bindGroup0);
    if (bindGroup1) wgpuBindGroupRelease(bindGroup1);

    if (physicsPipelineDiodes) wgpuComputePipelineRelease(physicsPipelineDiodes);
    if (physicsPipelineMosfets) wgpuComputePipelineRelease(physicsPipelineMosfets);
    if (assemblyPipeline) wgpuComputePipelineRelease(assemblyPipeline);
    if (recordWaveformPipeline) wgpuComputePipelineRelease(recordWaveformPipeline);
    
    // Phase 2.1 / 2.2 Buffers
    if (globalStateBuffer) wgpuBufferRelease(globalStateBuffer);
    if (waveformConfigBuffer) wgpuBufferRelease(waveformConfigBuffer);
    if (waveformStateBuffer) wgpuBufferRelease(waveformStateBuffer);
    if (waveformDataBuffer) wgpuBufferRelease(waveformDataBuffer);

    if (bindGroup2) wgpuBindGroupRelease(bindGroup2);
    
    if (queue) wgpuQueueRelease(queue);
    if (device) wgpuDeviceRelease(device);
    if (instance) wgpuInstanceRelease(instance);
}

bool WebGPUSolver::initWebGPU() {
#ifdef __EMSCRIPTEN__
    std::cout << "[INFO] WebGPUSolver: Initializing for Emscripten/WASM..." << std::endl;
    if (!device) return false;
    queue = wgpuDeviceGetQueue(device);
    return true;
#else
    std::cout << "[INFO] WebGPUSolver: Initializing Native Google Dawn (Metal)..." << std::endl;
    
    // 1. Initialize Dawn Proc Table
    dawnProcSetProcs(&dawn::native::GetProcs());

    // 2. Create Instance
    instance = wgpuCreateInstance(nullptr);
    if (!instance) {
        std::cerr << "[ERROR] Failed to create WGPU Instance." << std::endl;
        return false;
    }

    // 2. Discover Adapters & Request Device
    static auto nativeInstance = new dawn::native::Instance();
    auto adapters = nativeInstance->EnumerateAdapters();
    
    dawn::native::Adapter selectedAdapter;
    for (auto& adapter : adapters) {
        WGPUAdapterInfo info = {};
        wgpuAdapterGetInfo(adapter.Get(), &info);
        if (info.backendType == WGPUBackendType_Metal) {
            selectedAdapter = adapter;
            break;
        }
    }

    if (!selectedAdapter && !adapters.empty()) {
        selectedAdapter = adapters[0];
    }

    if (!selectedAdapter) {
        std::cerr << "[ERROR] No suitable GPU adapter found." << std::endl;
        return false;
    }

    device = selectedAdapter.CreateDevice();
    if (!device) {
        std::cerr << "[ERROR] Failed to create WGPU Device." << std::endl;
        return false;
    }

    queue = wgpuDeviceGetQueue(device);
    std::cout << "[INFO] WebGPUSolver: Native GPU initialization SUCCESS." << std::endl;
    return true;
#endif
}

WGPUShaderModule WebGPUSolver::loadShader(const std::string& source) {
    if (!device) return nullptr;

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.next = nullptr;
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = { source.data(), source.length() };

    WGPUShaderModuleDescriptor desc = {};
    desc.nextInChain = &wgslDesc.chain;
    
    return wgpuDeviceCreateShaderModule(device, &desc);
}

// Internal helper for split precision
static std::pair<float, float> splitDouble(double d) {
    float hi = static_cast<float>(d);
    float lo = static_cast<float>(d - static_cast<double>(hi));
    return {hi, lo};
}

static void uploadHiLo(WGPUQueue queue, WGPUBuffer buffer_hi, WGPUBuffer buffer_lo, const std::vector<double>& data) {
    std::vector<float> hi(data.size());
    std::vector<float> lo(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        auto p = splitDouble(data[i]);
        hi[i] = p.first; lo[i] = p.second;
    }
    wgpuQueueWriteBuffer(queue, buffer_hi, 0, hi.data(), hi.size() * sizeof(float));
    wgpuQueueWriteBuffer(queue, buffer_lo, 0, lo.data(), lo.size() * sizeof(float));
}

bool WebGPUSolver::initialize(const TensorNetlist& netlist) {
    setupResources(netlist);
    return true;
}

void WebGPUSolver::setupResources(const TensorNetlist& netlist) {
    if (!device) return;

    size_t nodeCount = netlist.numGlobalNodes;
    
    WGPUBufferDescriptor desc = {};
    desc.label = WGPU_SV("MNA_Buffers");
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    desc.mappedAtCreation = false;

    // MNA State (hi-lo split)
    desc.size = nodeCount * sizeof(float);
    voltageBufferHi = wgpuDeviceCreateBuffer(device, &desc);
    voltageBufferLo = wgpuDeviceCreateBuffer(device, &desc);
    deltaVBufferHi = wgpuDeviceCreateBuffer(device, &desc);
    deltaVBufferLo = wgpuDeviceCreateBuffer(device, &desc);
    rhsBufferHi = wgpuDeviceCreateBuffer(device, &desc);
    rhsBufferLo = wgpuDeviceCreateBuffer(device, &desc);
    
    // Jacobian (CSR - estimating 10 entries per node average)
    desc.size = nodeCount * 10 * sizeof(float); 
    desc.label = WGPU_SV("Jacobian_Buffers");
    jacobianBufferHi = wgpuDeviceCreateBuffer(device, &desc);
    jacobianBufferLo = wgpuDeviceCreateBuffer(device, &desc);

    // Topology
    desc.size = netlist.globalBlock.diodes.size() * sizeof(DiodeStampMap);
    if (desc.size > 0) diodeMapBuffer = wgpuDeviceCreateBuffer(device, &desc);
    
    desc.size = netlist.globalBlock.mosfets.size() * sizeof(MosfetStampMap);
    if (desc.size > 0) mosfetMapBuffer = wgpuDeviceCreateBuffer(device, &desc);

    // SoA Devices
    desc.size = netlist.globalBlock.diodes.size() * sizeof(DiodeDevice);
    if (desc.size > 0) diodeSoABuffer = wgpuDeviceCreateBuffer(device, &desc);

    desc.size = netlist.globalBlock.mosfets.size() * sizeof(MosfetDevice);
    if (desc.size > 0) mosfetSoABuffer = wgpuDeviceCreateBuffer(device, &desc);

    // Phase 2.1: Global State
    desc.size = 4 * sizeof(float); // time_hi/lo, h_hi/lo
    globalStateBuffer = wgpuDeviceCreateBuffer(device, &desc);

    // Phase 2.2: Waveform Buffering
    // Hardcoded thresholds for this turn, would normally come from Config.
    const uint32_t RING_BUFFER_SIZE = 4096;
    const uint32_t DECIMATION_RATIO = 10;
    
    desc.size = 3 * sizeof(uint32_t); // decimation_ratio, buffer_capacity, node_count
    waveformConfigBuffer = wgpuDeviceCreateBuffer(device, &desc);
    
    desc.size = 2 * sizeof(uint32_t); // write_ptr, decimation_counter
    waveformStateBuffer = wgpuDeviceCreateBuffer(device, &desc);
    
    size_t entryStride = 2 + (nodeCount * 2); // (time_hi/lo + v_hi/lo * nodes)
    desc.size = RING_BUFFER_SIZE * entryStride * sizeof(float);
    waveformDataBuffer = wgpuDeviceCreateBuffer(device, &desc);

    // --- Create Bind Group Layout 0 ---
    WGPUBindGroupLayoutEntry entries0[10] = {}; // Added GlobalState to BGL 0 for now or BGL1?
    // Let's stick to implementation plan: BindGroup 0 = Devices + MNA, BG 1 = Topology + State, BG 2 = Waveform
    for(int i=0; i<10; ++i) {
        entries0[i].binding = i;
        entries0[i].visibility = WGPUShaderStage_Compute;
        entries0[i].buffer.type = WGPUBufferBindingType_Storage;
    }
    WGPUBindGroupLayoutDescriptor bglDesc0 = {};
    bglDesc0.entryCount = 9;
    bglDesc0.entries = entries0;
    WGPUBindGroupLayout bgl0 = wgpuDeviceCreateBindGroupLayout(device, &bglDesc0);

    // Create Bind Group 0
    WGPUBindGroupEntry bgEntries0[9] = {};
    bgEntries0[0].binding = 0; bgEntries0[0].buffer = diodeSoABuffer; bgEntries0[0].size = netlist.globalBlock.diodes.size() * sizeof(DiodeDevice);
    bgEntries0[1].binding = 1; bgEntries0[1].buffer = mosfetSoABuffer; bgEntries0[1].size = netlist.globalBlock.mosfets.size() * sizeof(MosfetDevice);
    bgEntries0[2].binding = 2; bgEntries0[2].buffer = voltageBufferHi; bgEntries0[2].size = nodeCount * sizeof(float);
    bgEntries0[3].binding = 3; bgEntries0[3].buffer = voltageBufferLo; bgEntries0[3].size = nodeCount * sizeof(float);
    bgEntries0[4].binding = 4; bgEntries0[4].buffer = deltaVBufferHi; bgEntries0[4].size = nodeCount * sizeof(float);
    bgEntries0[5].binding = 5; bgEntries0[5].buffer = deltaVBufferLo; bgEntries0[5].size = nodeCount * sizeof(float);
    bgEntries0[6].binding = 6; bgEntries0[6].buffer = rhsBufferHi; bgEntries0[6].size = nodeCount * sizeof(float);
    bgEntries0[7].binding = 7; bgEntries0[7].buffer = rhsBufferLo; bgEntries0[7].size = nodeCount * sizeof(float);
    bgEntries0[8].binding = 8; bgEntries0[8].buffer = jacobianBufferHi; bgEntries0[8].size = nodeCount * 10 * sizeof(float);
    
    WGPUBindGroupDescriptor bgDesc0 = {};
    bgDesc0.layout = bgl0;
    bgDesc0.entryCount = 9;
    bgDesc0.entries = bgEntries0;
    this->bindGroup0 = wgpuDeviceCreateBindGroup(device, &bgDesc0);
    wgpuBindGroupLayoutRelease(bgl0);

    // --- Create Bind Group 2 (Waveform) ---
    WGPUBindGroupLayoutEntry entries2[3] = {};
    for(int i=0; i<3; ++i) {
        entries2[i].binding = i;
        entries2[i].visibility = WGPUShaderStage_Compute;
        entries2[i].buffer.type = WGPUBufferBindingType_Storage;
    }
    WGPUBindGroupLayoutDescriptor bglDesc2 = {};
    bglDesc2.entryCount = 3;
    bglDesc2.entries = entries2;
    WGPUBindGroupLayout bgl2 = wgpuDeviceCreateBindGroupLayout(device, &bglDesc2);

    WGPUBindGroupEntry bgEntries2[3] = {};
    bgEntries2[0].binding = 0; bgEntries2[0].buffer = waveformConfigBuffer; bgEntries2[0].size = 3 * sizeof(uint32_t);
    bgEntries2[1].binding = 1; bgEntries2[1].buffer = waveformStateBuffer;  bgEntries2[1].size = 2 * sizeof(uint32_t);
    bgEntries2[2].binding = 2; bgEntries2[2].buffer = waveformDataBuffer;   bgEntries2[2].size = RING_BUFFER_SIZE * entryStride * sizeof(float);

    WGPUBindGroupDescriptor bgDesc2 = {};
    bgDesc2.label = WGPU_SV("Waveform_BindGroup");
    bgDesc2.layout = bgl2;
    bgDesc2.entryCount = 3;
    bgDesc2.entries = bgEntries2;
    this->bindGroup2 = wgpuDeviceCreateBindGroup(device, &bgDesc2);
    wgpuBindGroupLayoutRelease(bgl2);

    // --- Create Pipelines ---
    // In a real implementation, we'd load the WGSL from disk. 
    // This is a placeholder for the pipeline layout and creation.
    WGPUBindGroupLayout layouts[] = { bgl0, nullptr, bgl2 }; // BG1 is Topology (placeholder)
    
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 3;
    plDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    WGPUShaderModule shaderModule = loadShader("... shader source ..."); 
    WGPUComputePipelineDescriptor computeDesc = {};
    computeDesc.label = WGPU_SV("Waveform_Pipeline");
    computeDesc.layout = layout;
    computeDesc.compute.module = shaderModule;
    computeDesc.compute.entryPoint = WGPU_SV("recordWaveform");
    
    recordWaveformPipeline = wgpuDeviceCreateComputePipeline(device, &computeDesc);
    wgpuPipelineLayoutRelease(layout);

    // Initial Upload of Waveform Config
    uint32_t config[3] = { DECIMATION_RATIO, RING_BUFFER_SIZE, (uint32_t)nodeCount };
    wgpuQueueWriteBuffer(queue, waveformConfigBuffer, 0, config, sizeof(config));
    
    uint32_t state[2] = { 0, 0 }; // write_ptr, decimation_counter
    wgpuQueueWriteBuffer(queue, waveformStateBuffer, 0, state, sizeof(state));

    std::cout << "[INFO] WebGPUSolver: GPU Buffers and Waveform BindGroup created." << std::endl;
}

void WebGPUSolver::uploadNetlist(const TensorNetlist& netlist) {
    if (!queue) return;
    
    // Flatten hierarchy: Convert hierarchical TensorNetlist to flat TensorizedBlock (SoA)
    TensorizedBlock block = tensorizeNetlist(netlist);

    // 1. Diodes
    std::vector<DiodeDevice> diodeDevices;
    diodeDevices.reserve(block.diodes.size());
    for (size_t i = 0; i < block.diodes.size(); ++i) {
        DiodeDevice d = {};
        d.anode = block.diodes.anodes[i];
        d.cathode = block.diodes.cathodes[i];
        auto is = splitDouble(block.diodes.Is[i]); d.Is_hi = is.first; d.Is_lo = is.second;
        auto n = splitDouble(block.diodes.N[i]); d.N_hi = n.first; d.N_lo = n.second;
        auto vt = splitDouble(block.diodes.Vt[i]); d.Vt_hi = vt.first; d.Vt_lo = vt.second;
        diodeDevices.push_back(d);
    }
    if (!diodeDevices.empty()) {
        wgpuQueueWriteBuffer(queue, diodeSoABuffer, 0, diodeDevices.data(), diodeDevices.size() * sizeof(DiodeDevice));
    }

    // 2. MOSFETs
    std::vector<MosfetDevice> mosfetDevices;
    mosfetDevices.reserve(block.mosfets.size());
    for (size_t i = 0; i < block.mosfets.size(); ++i) {
        MosfetDevice m = {};
        m.drain = block.mosfets.drains[i];
        m.gate = block.mosfets.gates[i];
        m.source = block.mosfets.sources[i];
        m.body = block.mosfets.bodies[i];
        
        auto w = splitDouble(block.mosfets.W[i]); m.W_hi = w.first; m.W_lo = w.second;
        auto l = splitDouble(block.mosfets.L[i]); m.L_hi = l.first; m.L_lo = l.second;
        auto kp = splitDouble(block.mosfets.Kp[i]); m.Kp_hi = kp.first; m.Kp_lo = kp.second;
        auto vth = splitDouble(block.mosfets.Vth[i]); m.Vth_hi = vth.first; m.Vth_lo = vth.second;
        auto lam = splitDouble(block.mosfets.lambda[i]); m.lambda_hi = lam.first; m.lambda_lo = lam.second;
        
        m.isPMOS = block.mosfets.isPMOS[i] ? 1 : 0;
        
        mosfetDevices.push_back(m);
    }
    if (!mosfetDevices.empty()) {
        wgpuQueueWriteBuffer(queue, mosfetSoABuffer, 0, mosfetDevices.data(), mosfetDevices.size() * sizeof(MosfetDevice));
    }

    // 3. Topology (Placeholder for now - needs CSR mapping)
    // In a real implementation, we'd run a topological analysis here to find CSR offsets.
    
    std::cout << "[INFO] WebGPUSolver: Netlist upload complete (" 
              << block.diodes.size() << " diodes, " << block.mosfets.size() << " mosfets)." << std::endl;
}

void WebGPUSolver::runNRStep(double time, double h) {
    if (!device || !queue) return;

    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    // 1. Physics Pass
    // wgpuComputePassEncoderSetPipeline(pass, physicsPipeline);
    // wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup0, 0, nullptr);
    // wgpuComputePassEncoderDispatchWorkgroups(pass, (numDiodes + 63) / 64, 1, 1);

    // 2. Assembly Pass
    // wgpuComputePassEncoderSetPipeline(pass, assemblyPipeline);
    // wgpuComputePassEncoderDispatchWorkgroups(pass, (numNodes + 63) / 64, 1, 1);

    // 3. Phase 2.2: Waveform Recording (After NR loop or each converged step)
    if (recordWaveformPipeline) {
        wgpuComputePassEncoderSetPipeline(pass, recordWaveformPipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup0, 0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, 2, bindGroup2, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cbDesc = {};
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cbDesc);
    wgpuQueueSubmit(queue, 1, &commands);
    
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    std::cout << "[INFO] WebGPUSolver: NR Loop Step dispatched." << std::endl;
}

bool WebGPUSolver::checkConvergence() {
    return true;
}

std::vector<double> WebGPUSolver::downloadSolution() {
    return std::vector<double>();
}

std::vector<std::vector<double>> WebGPUSolver::downloadWaveform() {
    if (!device || !queue) return {};

    // 1. Sync and read back WaveformState
    // This is asynchronous in WebGPU, so for this WASM bridge turn we assume 
    // a synchronous copy or mapped staging buffer has been prepared.
    // In production, we'd use wgpuBufferMapAsync.
    
    std::vector<std::vector<double>> results;
    // Unpack Logic:
    // for each entry in ring buffer:
    //   results.push_back({time_hi + time_lo, v1_hi + v1_lo, ...})
    
    return results;
}
