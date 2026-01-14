#include "webgpu_solver.h"
#ifdef __EMSCRIPTEN__
#include <webgpu/webgpu.h>
#include <emscripten/html5_webgpu.h>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>

// =============================================================================
// WGSL SHADER CODE - Component Kernels with Fixed-Point Atomic Accumulation
// =============================================================================
// We use atomic<i32> for the RHS and Matrix buffers because WebGPU does not
// support atomic<f32>. Values are scaled by FIXED_POINT_SCALE on upload
// and unscaled on download. This allows race-free parallel accumulation.
// =============================================================================

const char* WGSL_KERNELS = R"(
// --- Constants ---
const FIXED_POINT_SCALE: f32 = 1e9; // Precision: ~1 nano-unit
const MAX_EXP_ARG: f32 = 40.0;      // Prevent exp() overflow

// --- Buffers (Bind Group 0) ---
// Global voltage vector (read-only for stamping pass)
@group(0) @binding(0) var<storage, read> voltages : array<f32>;
// RHS vector (atomic i32 for accumulation, scaled)
@group(0) @binding(1) var<storage, read_write> rhs_atomic : array<atomic<i32>>;
// Matrix (CSR triplet format: row, col, val_atomic) - Simplified Dense for now
// For a real sparse solver, this would be COO then reduced to CSR.
@group(0) @binding(2) var<storage, read_write> matrix_atomic : array<atomic<i32>>; 
// Matrix dimension (N x N)
@group(0) @binding(3) var<uniform> matrixDim : u32;

// --- Helper: Fixed-Point Atomic Add ---
fn atomicAddFloat(ptr: ptr<storage, atomic<i32>, read_write>, value: f32) {
    let scaled = i32(value * FIXED_POINT_SCALE);
    atomicAdd(ptr, scaled);
}

// --- Helper: Stamp to dense matrix (row-major) ---
fn stampMatrix(row: i32, col: i32, val: f32) {
    if (row < 0 || col < 0) { return; } // Ground node (0) is implicit
    let idx = u32(row) * matrixDim + u32(col);
    atomicAddFloat(&matrix_atomic[idx], val);
}

// --- Helper: Stamp to RHS vector ---
fn stampRHS(node: i32, val: f32) {
    if (node < 0) { return; } // Ground
    atomicAddFloat(&rhs_atomic[u32(node)], val);
}

// --- Helper: Get voltage (0-indexed, ground=0V) ---
fn getVoltage(node: i32) -> f32 {
    if (node < 0) { return 0.0; }
    return voltages[u32(node)];
}

// --- Helper: Safe Exponential ---
fn safeExp(x: f32) -> f32 {
    if (x > MAX_EXP_ARG) { return exp(MAX_EXP_ARG); }
    return exp(x);
}

// =============================================================================
// RESISTOR KERNEL
// =============================================================================
struct ResistorData {
    n1: i32,
    n2: i32,
    R: f32,
    _pad: f32, // Alignment padding
};
@group(1) @binding(0) var<storage, read> resistors : array<ResistorData>;

@compute @workgroup_size(64)
fn resistor_stamp(@builtin(global_invocation_id) gid : vec3<u32>) {
    let idx = gid.x;
    if (idx >= arrayLength(&resistors)) { return; }
    
    let r = resistors[idx];
    let n1 = r.n1 - 1; // Convert 1-indexed to 0-indexed
    let n2 = r.n2 - 1;
    let G = 1.0 / r.R;
    
    // MNA Stamp: G goes on diagonal, -G on off-diagonal
    stampMatrix(n1, n1, G);
    stampMatrix(n2, n2, G);
    stampMatrix(n1, n2, -G);
    stampMatrix(n2, n1, -G);
}

// =============================================================================
// CAPACITOR KERNEL (Trapezoidal Integration)
// =============================================================================
struct CapacitorData {
    p1: i32,
    p2: i32,
    C: f32,
    v_prev: f32,
    i_prev: f32,
    h: f32,      // Time step
    _pad1: f32,
    _pad2: f32,
};
@group(1) @binding(1) var<storage, read> capacitors : array<CapacitorData>;

@compute @workgroup_size(64)
fn capacitor_stamp(@builtin(global_invocation_id) gid : vec3<u32>) {
    let idx = gid.x;
    if (idx >= arrayLength(&capacitors)) { return; }
    
    let c = capacitors[idx];
    let n1 = c.p1 - 1;
    let n2 = c.p2 - 1;
    
    // Trapezoidal Companion: Geq = 2C/h, Ieq = -Geq*v_prev - i_prev
    let Geq = (2.0 * c.C) / c.h;
    let Ieq = -Geq * c.v_prev - c.i_prev;
    
    // Stamp Geq (like resistor)
    stampMatrix(n1, n1, Geq);
    stampMatrix(n2, n2, Geq);
    stampMatrix(n1, n2, -Geq);
    stampMatrix(n2, n1, -Geq);
    
    // Stamp Ieq to RHS
    stampRHS(n1, -Ieq); // Current leaving n1
    stampRHS(n2, Ieq);  // Current entering n2
}

// =============================================================================
// INDUCTOR KERNEL (Trapezoidal Integration)
// =============================================================================
struct InductorData {
    c1: i32,
    c2: i32,
    L: f32,
    i_prev: f32,
    v_prev: f32,
    h: f32,      // Time step
    _pad1: f32,
    _pad2: f32,
};
@group(1) @binding(5) var<storage, read> inductors : array<InductorData>;

@compute @workgroup_size(64)
fn inductor_stamp(@builtin(global_invocation_id) gid : vec3<u32>) {
    let idx = gid.x;
    if (idx >= arrayLength(&inductors)) { return; }
    
    let ind = inductors[idx];
    let n1 = ind.c1 - 1;
    let n2 = ind.c2 - 1;
    
    // Trapezoidal Companion: Geq = h / (2*L), Ieq = i_prev + Geq * v_prev
    let Geq = ind.h / (2.0 * ind.L);
    let Ieq = ind.i_prev + Geq * ind.v_prev;
    
    // Stamp Geq (like resistor)
    stampMatrix(n1, n1, Geq);
    stampMatrix(n2, n2, Geq);
    stampMatrix(n1, n2, -Geq);
    stampMatrix(n2, n1, -Geq);
    
    // Stamp Ieq to RHS (current source)
    stampRHS(n1, Ieq);  // Current entering n1
    stampRHS(n2, -Ieq); // Current leaving n2
}

// =============================================================================
// DIODE KERNEL (Shockley + Linear Continuation)
// =============================================================================
struct DiodeData {
    anode: i32,
    cathode: i32,
    Is: f32,
    N: f32,
    Vt: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};
@group(1) @binding(2) var<storage, read> diodes : array<DiodeData>;

@compute @workgroup_size(64)
fn diode_stamp(@builtin(global_invocation_id) gid : vec3<u32>) {
    let idx = gid.x;
    if (idx >= arrayLength(&diodes)) { return; }
    
    let d = diodes[idx];
    let nA = d.anode - 1;
    let nC = d.cathode - 1;
    
    let vA = getVoltage(nA);
    let vC = getVoltage(nC);
    let vD = vA - vC;
    
    let n_vt = d.N * d.Vt;
    let arg = vD / n_vt;
    
    var i_d: f32;
    var g_d: f32;
    
    // Linear continuation for numerical stability
    if (arg > MAX_EXP_ARG) {
        let exp_max = exp(MAX_EXP_ARG);
        let i_max = d.Is * (exp_max - 1.0);
        let g_max = (d.Is / n_vt) * exp_max;
        let delta = vD - (n_vt * MAX_EXP_ARG);
        i_d = i_max + delta * g_max;
        g_d = g_max;
    } else {
        let exp_arg = safeExp(arg);
        i_d = d.Is * (exp_arg - 1.0);
        g_d = (d.Is / n_vt) * exp_arg;
    }
    
    // Clamp conductance
    g_d = max(g_d, 1e-15);
    
    // Linearized stamp: I_eq = i_d - g_d * v_d
    let i_eq = i_d - g_d * vD;
    
    stampMatrix(nA, nA, g_d);
    stampMatrix(nC, nC, g_d);
    stampMatrix(nA, nC, -g_d);
    stampMatrix(nC, nA, -g_d);
    
    stampRHS(nA, -i_eq); // Current leaving anode
    stampRHS(nC, i_eq);  // Current entering cathode
}

// =============================================================================
// MOSFET KERNEL (Level 1 NMOS)
// =============================================================================
struct MosfetData {
    drain: i32,
    gate: i32,
    source: i32,
    body: i32,
    W: f32,
    L: f32,
    Kp: f32,
    Vth: f32,
    lambda: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};
@group(1) @binding(3) var<storage, read> mosfets : array<MosfetData>;

@compute @workgroup_size(64)
fn mosfet_stamp(@builtin(global_invocation_id) gid : vec3<u32>) {
    let idx = gid.x;
    if (idx >= arrayLength(&mosfets)) { return; }
    
    let m = mosfets[idx];
    let nD = m.drain - 1;
    let nG = m.gate - 1;
    let nS = m.source - 1;
    
    let vD = getVoltage(nD);
    let vG = getVoltage(nG);
    let vS = getVoltage(nS);
    
    let vgs = vG - vS;
    let vds = vD - vS;
    
    let K = 0.5 * m.Kp * (m.W / m.L);
    let vov = vgs - m.Vth;
    
    var ids: f32 = 0.0;
    var gm: f32 = 0.0;
    var gds: f32 = 1e-12; // GMIN
    
    if (vov > 0.0) {
        if (vds < vov) {
            // Linear Region
            ids = K * (2.0 * vov * vds - vds * vds);
            gm = 2.0 * K * vds;
            gds = 2.0 * K * (vov - vds);
        } else {
            // Saturation
            ids = K * vov * vov * (1.0 + m.lambda * vds);
            gm = 2.0 * K * vov * (1.0 + m.lambda * vds);
            gds = K * vov * vov * m.lambda;
        }
    }
    
    gds = max(gds, 1e-12);
    
    // Linearized: i_eq = ids - gm*(vG - vS) - gds*(vD - vS)
    let i_eq = ids - gm * vgs - gds * vds;
    
    // Stamp GM (VCCS)
    stampMatrix(nD, nG, gm);
    stampMatrix(nD, nS, -gm);
    stampMatrix(nS, nG, -gm);
    stampMatrix(nS, nS, gm);
    
    // Stamp GDS (Resistor D-S)
    stampMatrix(nD, nD, gds);
    stampMatrix(nD, nS, -gds);
    stampMatrix(nS, nD, -gds);
    stampMatrix(nS, nS, gds);
    
    // Stamp RHS
    stampRHS(nD, -i_eq);
    stampRHS(nS, i_eq);
}

// =============================================================================
// BJT KERNEL (Ebers-Moll NPN)
// =============================================================================
struct BJTData {
    collector: i32,
    base: i32,
    emitter: i32,
    isNPN: i32, // 1 = NPN, 0 = PNP
    Is: f32,
    BetaF: f32,
    BetaR: f32,
    Vt: f32,
};
@group(1) @binding(4) var<storage, read> bjts : array<BJTData>;

@compute @workgroup_size(64)
fn bjt_stamp(@builtin(global_invocation_id) gid : vec3<u32>) {
    let idx = gid.x;
    if (idx >= arrayLength(&bjts)) { return; }
    
    let q = bjts[idx];
    let nC = q.collector - 1;
    let nB = q.base - 1;
    let nE = q.emitter - 1;
    
    let vC = getVoltage(nC);
    let vB = getVoltage(nB);
    let vE = getVoltage(nE);
    
    var sign: f32 = 1.0;
    if (q.isNPN == 0) { sign = -1.0; }
    
    let v_be = sign * (vB - vE);
    let v_bc = sign * (vB - vC);
    
    let exp_be = safeExp(v_be / q.Vt);
    let exp_bc = safeExp(v_bc / q.Vt);
    
    let i_f = q.Is * (exp_be - 1.0);
    let i_r = q.Is * (exp_bc - 1.0);
    let g_f = (q.Is / q.Vt) * exp_be;
    let g_r = (q.Is / q.Vt) * exp_bc;
    
    let invBetaF = 1.0 / q.BetaF;
    let invBetaR = 1.0 / q.BetaR;
    
    let i_ct = i_f - i_r;
    let ic = sign * (i_ct - i_r * invBetaR);
    let ib = sign * (i_f * invBetaF + i_r * invBetaR);
    let ie = -ic - ib;
    
    // Conductances (NPN derivation)
    let dIc_dVbe = g_f;
    let dIc_dVbc = -g_r * (1.0 + invBetaR);
    let dIb_dVbe = g_f * invBetaF;
    let dIb_dVbc = g_r * invBetaR;
    let dIe_dVbe = -(dIc_dVbe + dIb_dVbe);
    let dIe_dVbc = -(dIc_dVbc + dIb_dVbc);
    
    // Map to terminal conductances (NPN specific)
    var g_cc = -dIc_dVbc;
    var g_cb = dIc_dVbe + dIc_dVbc;
    var g_ce = -dIc_dVbe;
    var g_bc = -dIb_dVbc;
    var g_bb = dIb_dVbe + dIb_dVbc;
    var g_be = -dIb_dVbe;
    var g_ec = -dIe_dVbc;
    var g_eb = dIe_dVbe + dIe_dVbc;
    var g_ee = -dIe_dVbe;
    
    if (q.isNPN == 0) { // PNP: flip signs
        g_cc = -g_cc; g_cb = -g_cb; g_ce = -g_ce;
        g_bc = -g_bc; g_bb = -g_bb; g_be = -g_be;
        g_ec = -g_ec; g_eb = -g_eb; g_ee = -g_ee;
    }
    
    // I_eq vectors
    let i_eq_c = ic - (g_cc*vC + g_cb*vB + g_ce*vE);
    let i_eq_b = ib - (g_bc*vC + g_bb*vB + g_be*vE);
    let i_eq_e = ie - (g_ec*vC + g_eb*vB + g_ee*vE);
    
    // Stamp 3x3 conductance matrix
    stampMatrix(nC, nC, g_cc); stampMatrix(nC, nB, g_cb); stampMatrix(nC, nE, g_ce);
    stampMatrix(nB, nC, g_bc); stampMatrix(nB, nB, g_bb); stampMatrix(nB, nE, g_be);
    stampMatrix(nE, nC, g_ec); stampMatrix(nE, nB, g_eb); stampMatrix(nE, nE, g_ee);
    
    // Stamp RHS
    stampRHS(nC, -i_eq_c);
    stampRHS(nB, -i_eq_b);
    stampRHS(nE, -i_eq_e);
}
)";

// =============================================================================
// C++ Implementation
// =============================================================================

WebGPUSolver::WebGPUSolver() {
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain = nullptr;
    WGPUInstance instance = wgpuCreateInstance(&desc);
    
    // Async device request is required for Emscripten.
    // For now, we stub and rely on JS-side initialization.
    std::cerr << "[WebGPU] Solver initialized (device pending async setup)" << std::endl;
}

WebGPUSolver::~WebGPUSolver() {
    if (resistorBuffer) wgpuBufferRelease(resistorBuffer);
    if (diodeBuffer) wgpuBufferRelease(diodeBuffer);
    if (rhsBuffer) wgpuBufferRelease(rhsBuffer);
    if (matrixBuffer) wgpuBufferRelease(matrixBuffer);
    if (queue) wgpuQueueRelease(queue);
    if (device) wgpuDeviceRelease(device);
}

bool WebGPUSolver::initialize(const TensorNetlist& netlist) {
    if (!device) return false;
    createBuffers(netlist);
    createPipelines();
    return true;
}

void WebGPUSolver::createBuffers(const TensorNetlist& netlist) {
    size_t N = netlist.numGlobalNodes;
    
    // RHS Buffer (atomic i32)
    WGPUBufferDescriptor rhsDesc = {};
    rhsDesc.label = "RHS Atomic Buffer";
    rhsDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    rhsDesc.size = N * sizeof(int32_t);
    rhsBuffer = wgpuDeviceCreateBuffer(device, &rhsDesc);
    
    // Matrix Buffer (dense N*N atomic i32)
    WGPUBufferDescriptor matDesc = {};
    matDesc.label = "Matrix Atomic Buffer";
    matDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    matDesc.size = N * N * sizeof(int32_t);
    matrixBuffer = wgpuDeviceCreateBuffer(device, &matDesc);
}

void WebGPUSolver::createPipelines() {
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = WGSL_KERNELS;
    
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    shaderDesc.label = "Circuit Solver Kernels";
    
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    
    // Create pipelines for each kernel
    auto createPipeline = [&](const char* entryPoint) -> WGPUComputePipeline {
        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.compute.module = shaderModule;
        pipelineDesc.compute.entryPoint = entryPoint;
        return wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    };
    
    stampingPipeline = createPipeline("resistor_stamp"); // Primary stamping
    // Additional pipelines would be created similarly for capacitor, diode, mosfet, bjt
}

void WebGPUSolver::uploadState(const std::vector<double>& currentVoltages) {
    std::vector<float> v_f(currentVoltages.begin(), currentVoltages.end());
    wgpuQueueWriteBuffer(queue, voltageBuffer, 0, v_f.data(), v_f.size() * sizeof(float));
}

void WebGPUSolver::runMatrixAssembly() {
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
    
    wgpuComputePassEncoderSetPipeline(pass, stampingPipeline);
    // BindGroups would be set here...
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue, 1, &commands);
}

void WebGPUSolver::runSolverSteps(int maxIterations) {
    // Future: GPU-based PCG iteration
}

std::vector<double> WebGPUSolver::downloadSolution() {
    // GPU Readback: Map buffer async, copy, unmap.
    // Scale back from fixed-point.
    return {};
}

#endif // __EMSCRIPTEN__
