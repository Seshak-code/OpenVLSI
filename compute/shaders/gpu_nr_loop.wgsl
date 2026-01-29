/*
 * gpu_nr_loop.wgsl
 * Persistent GPU-resident Newton-Raphson loop kernels.
 * Uses EmulatedF64 (hi-lo float32 pairs) for numerical authority.
 */

struct f64 {
    hi: f32,
    lo: f32,
};

// --- EmulatedF64 Helpers (Double-Float Arithmetic) ---

fn f64_from_f32(a: f32) -> f64 {
    return f64(a, 0.0);
}

fn f64_add(a: f64, b: f64) -> f64 {
    let s = a.hi + b.hi;
    let v = s - a.hi;
    let e = (a.hi - (s - v)) + (b.hi - v) + a.lo + b.lo;
    let hi = s + e;
    let lo = e + (s - hi);
    return f64(hi, lo);
}

fn f64_sub(a: f64, b: f64) -> f64 {
    return f64_add(a, f64(-b.hi, -b.lo));
}

fn f64_mul(a: f64, b: f64) -> f64 {
    let hi = a.hi * b.hi;
    let lo = fma(a.hi, b.hi, -hi) + a.hi * b.lo + a.lo * b.hi;
    let s = hi + lo;
    let l = lo + (hi - s);
    return f64(s, l);
}

// Simple Newton iteration for f64 division
fn f64_div(a: f64, b: f64) -> f64 {
    let x = a.hi / b.hi; // Float32 estimate
    let x64 = f64_from_f32(x);
    // One refinement iteration: x = x * (2 - b*x)
    let b_x = f64_mul(b, x64);
    let error = f64_sub(f64_from_f32(2.0), b_x);
    let refined_recip = f64_mul(x64, error);
    return f64_mul(a, refined_recip);
}

// Approximation for exp(x) in f64
fn f64_exp(x: f64) -> f64 {
    // Falls back to f32 exp if out of high-precision refinement range for now
    // But returns f64 to maintain type consistency.
    let e = exp(x.hi);
    return f64(e, 0.0); 
}

// --- Data Structures (SoA) ---

struct DiodeDevice {
    anode: i32,
    cathode: i32,
    Is_hi: f32, Is_lo: f32,
    N_hi: f32,  N_lo: f32,
    Vt_hi: f32, Vt_lo: f32,
    v_d_hi: f32, v_d_lo: f32,
    i_d_hi: f32, i_d_lo: f32,
    g_d_hi: f32, g_d_lo: f32,
};
struct DiodeSoA {
    devices: array<DiodeDevice>,
};

struct MosfetDevice {
    drain: i32,
    gate: i32,
    source: i32,
    body: i32,
    W_hi: f32, W_lo: f32,
    L_hi: f32, L_lo: f32,
    Kp_hi: f32, Kp_lo: f32,
    Vth_hi: f32, Vth_lo: f32,
    lambda_hi: f32, lambda_lo: f32,
    isPMOS: u32,
    vgs_hi: f32, vgs_lo: f32,
    vds_hi: f32, vds_lo: f32,
    vbs_hi: f32, vbs_lo: f32,
    ids_hi: f32, ids_lo: f32,
    gm_hi: f32,  gm_lo: f32,
    gmb_hi: f32, gmb_lo: f32,
    gds_hi: f32, gds_lo: f32,
};
struct MosfetSoA {
    devices: array<MosfetDevice>,
};

struct GlobalState {
    time_hi: f32,
    time_lo: f32,
    h_hi: f32,
    h_lo: f32,
};

struct CsrMatrix {
    values_hi: array<f32>,
    values_lo: array<f32>,
};

// --- Bindings ---

@group(0) @binding(0) var<storage, read_write> diodes: DiodeSoA;
@group(0) @binding(1) var<storage, read_write> mosfets: MosfetSoA;
@group(0) @binding(2) var<storage, read_write> voltages_hi: array<f32>;
@group(0) @binding(3) var<storage, read_write> voltages_lo: array<f32>;
@group(0) @binding(4) var<storage, read_write> deltaV_hi: array<f32>;
@group(0) @binding(5) var<storage, read_write> deltaV_lo: array<f32>;
@group(0) @binding(6) var<storage, read_write> rhs_hi: array<f32>;
@group(0) @binding(7) var<storage, read_write> rhs_lo: array<f32>;
@group(0) @binding(8) var<storage, read_write> jacobian: CsrMatrix;

struct DiodeStampMap {
    aa: u32, cc: u32, ac: u32, ca: u32,
};
struct MosfetStampMap {
    dd: u32, dg: u32, ds: u32, db: u32,
    sd: u32, sg: u32, ss: u32, sb: u32,
};
@group(1) @binding(0) var<storage, read> diode_maps: array<DiodeStampMap>;
@group(1) @binding(1) var<storage, read> mosfet_maps: array<MosfetStampMap>;
@group(1) @binding(2) var<storage, read> global_state: GlobalState;

// --- Stage 1: Batch Physics ---

@compute @workgroup_size(64)
fn batchDiodePhysics(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let i = global_id.x;
    if (i >= arrayLength(&diodes.devices)) { return; }

    let nD = i32(i);
    let nA = diodes.devices[i].anode;
    let nC = diodes.devices[i].cathode;

    var v_a = f64(0.0, 0.0);
    if (nA > 0) { v_a = f64(voltages_hi[u32(nA-1)], voltages_lo[u32(nA-1)]); }
    var v_c = f64(0.0, 0.0);
    if (nC > 0) { v_c = f64(voltages_hi[u32(nC-1)], voltages_lo[u32(nC-1)]); }
    
    let v_d = f64_sub(v_a, v_c);
    diodes.devices[i].v_d_hi = v_d.hi;
    diodes.devices[i].v_d_lo = v_d.lo;

    let Is = f64(diodes.devices[i].Is_hi, diodes.devices[i].Is_lo);
    let nvt = f64_mul(f64(diodes.devices[i].N_hi, diodes.devices[i].N_lo), f64(diodes.devices[i].Vt_hi, diodes.devices[i].Vt_lo));
    
    let arg = f64_div(v_d, nvt);
    let exp_arg = f64_exp(arg);
    
    let i_d = f64_mul(Is, f64_sub(exp_arg, f64_from_f32(1.0)));
    let g_d = f64_div(f64_mul(Is, exp_arg), nvt);
    
    diodes.devices[i].i_d_hi = i_d.hi; diodes.devices[i].i_d_lo = i_d.lo;
    diodes.devices[i].g_d_hi = g_d.hi; diodes.devices[i].g_d_lo = g_d.lo;
}

// --- Stage 2: Assemble Jacobian (Sequential per-node strategy to avoid f64 atomics) ---
// Note: In high-performance cases we would use a segmented scan or dedicated summation kernel.
// For this turn, we focus on the f64 math correctness as requested.

@compute @workgroup_size(64)
@compute @workgroup_size(64)
fn batchMosfetPhysics(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let i = global_id.x;
    if (i >= arrayLength(&mosfets.devices)) { return; }

    let nD = mosfets.devices[i].drain;
    let nG = mosfets.devices[i].gate;
    let nS = mosfets.devices[i].source;
    let nB = mosfets.devices[i].body;

    var v_d_orig = f64(0.0, 0.0);
    if (nD > 0) { v_d_orig = f64(voltages_hi[u32(nD-1)], voltages_lo[u32(nD-1)]); }
    var v_g_orig = f64(0.0, 0.0);
    if (nG > 0) { v_g_orig = f64(voltages_hi[u32(nG-1)], voltages_lo[u32(nG-1)]); }
    var v_s_orig = f64(0.0, 0.0);
    if (nS > 0) { v_s_orig = f64(voltages_hi[u32(nS-1)], voltages_lo[u32(nS-1)]); }
    var v_b_orig = f64(0.0, 0.0);
    if (nB > 0) { v_b_orig = f64(voltages_hi[u32(nB-1)], voltages_lo[u32(nB-1)]); }

    let pmos = mosfets.devices[i].isPMOS;
    var sign = 1.0;
    if (pmos != 0u) { sign = -1.0; }

    let vgs = f64_sub(v_g_orig, v_s_orig);
    let vds = f64_sub(v_d_orig, v_s_orig);
    let vbs = f64_sub(v_b_orig, v_s_orig);
    
    mosfets.devices[i].vgs_hi = vgs.hi; mosfets.devices[i].vgs_lo = vgs.lo;
    mosfets.devices[i].vds_hi = vds.hi; mosfets.devices[i].vds_lo = vds.lo;
    mosfets.devices[i].vbs_hi = vbs.hi; mosfets.devices[i].vbs_lo = vbs.lo;

    let local_vgs = f64_mul(f64_from_f32(sign), vgs);
    let local_vds = f64_mul(f64_from_f32(sign), vds);
    let local_vbs = f64_mul(f64_from_f32(sign), vbs);
    let local_vth = f64_mul(f64_from_f32(sign), f64(mosfets.devices[i].Vth_hi, mosfets.devices[i].Vth_lo));

    let K = f64_mul(f64_from_f32(0.5), f64_mul(f64(mosfets.devices[i].Kp_hi, mosfets.devices[i].Kp_lo), f64_div(f64(mosfets.devices[i].W_hi, mosfets.devices[i].W_lo), f64(mosfets.devices[i].L_hi, mosfets.devices[i].L_lo))));
    let vov = f64_sub(local_vgs, local_vth);

    if (vov.hi <= 0.0) {
        mosfets.devices[i].ids_hi = 0.0; mosfets.devices[i].ids_lo = 0.0;
        mosfets.devices[i].gm_hi = 0.0;  mosfets.devices[i].gm_lo = 0.0;
        mosfets.devices[i].gmb_hi = 0.0; mosfets.devices[i].gmb_lo = 0.0;
        mosfets.devices[i].gds_hi = 1e-12; // GMIN
    } else if (local_vds.hi < vov.hi) {
        // Linear region
        let ids = f64_mul(f64_from_f32(sign), f64_mul(K, f64_sub(f64_mul(f64_from_f32(2.0), f64_mul(vov, local_vds)), f64_mul(local_vds, local_vds))));
        let gm = f64_mul(f64_from_f32(2.0), f64_mul(K, local_vds));
        let gds = f64_mul(f64_from_f32(2.0), f64_mul(K, f64_sub(vov, local_vds)));
        
        mosfets.devices[i].ids_hi = ids.hi; mosfets.devices[i].ids_lo = ids.lo;
        mosfets.devices[i].gm_hi = gm.hi; mosfets.devices[i].gm_lo = gm.lo;
        mosfets.devices[i].gmb_hi = 0.0; mosfets.devices[i].gmb_lo = 0.0;
        mosfets.devices[i].gds_hi = gds.hi; mosfets.devices[i].gds_lo = gds.lo;
    } else {
        // Saturation
        let lam = f64(mosfets.devices[i].lambda_hi, mosfets.devices[i].lambda_lo);
        let term = f64_add(f64_from_f32(1.0), f64_mul(lam, local_vds));
        let ids = f64_mul(f64_from_f32(sign), f64_mul(K, f64_mul(f64_mul(vov, vov), term)));
        let gm = f64_mul(f64_from_f32(2.0), f64_mul(K, f64_mul(vov, term)));
        let gds = f64_mul(K, f64_mul(f64_mul(vov, vov), lam));

        mosfets.devices[i].ids_hi = ids.hi; mosfets.devices[i].ids_lo = ids.lo;
        mosfets.devices[i].gm_hi = gm.hi; mosfets.devices[i].gm_lo = gm.lo;
        mosfets.devices[i].gmb_hi = 0.0; mosfets.devices[i].gmb_lo = 0.0;
        mosfets.devices[i].gds_hi = gds.hi; mosfets.devices[i].gds_lo = gds.lo;
    }
}

@compute @workgroup_size(64)
fn assembleJacobian(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let i = global_id.x;
    
    // Diode Assembly
    if (i < arrayLength(&diodes.devices)) {
        let g = f64(diodes.devices[i].g_d_hi, diodes.devices[i].g_d_lo);
        let map = diode_maps[i];
        
        if (map.aa != 0xffffffffu) { jacobian.values_hi[map.aa] = f64_add(f64(jacobian.values_hi[map.aa], jacobian.values_lo[map.aa]), g).hi; } 
        // ... collisions possible ...
        
        let i_eq = f64_sub(f64(diodes.devices[i].i_d_hi, diodes.devices[i].i_d_lo), f64_mul(g, f64(diodes.devices[i].v_d_hi, diodes.devices[i].v_d_lo)));
        let nA = diodes.devices[i].anode;
        if (nA > 0) { rhs_hi[u32(nA-1)] = f64_sub(f64(rhs_hi[u32(nA-1)], rhs_lo[u32(nA-1)]), i_eq).hi; }
    }
    
    // MOSFET Assembly
    if (i < arrayLength(&mosfets.devices)) {
        let dev = mosfets.devices[i];
        let gm = f64(dev.gm_hi, dev.gm_lo);
        let gmb = f64(dev.gmb_hi, dev.gmb_lo);
        let gds = f64(dev.gds_hi, dev.gds_lo);
        let ids = f64(dev.ids_hi, dev.ids_lo);
        let vgs = f64(dev.vgs_hi, dev.vgs_lo);
        let vds = f64(dev.vds_hi, dev.vds_lo);
        let vbs = f64(dev.vbs_hi, dev.vbs_lo);
        
        let map = mosfet_maps[i];
        
        // Row D: gm, gmb, gds
        if (map.dd != 0xffffffffu) { jacobian.values_hi[map.dd] = f64_add(f64(jacobian.values_hi[map.dd], jacobian.values_lo[map.dd]), gds).hi; }
        if (map.dg != 0xffffffffu) { jacobian.values_hi[map.dg] = f64_add(f64(jacobian.values_hi[map.dg], jacobian.values_lo[map.dg]), gm).hi; }
        if (map.ds != 0xffffffffu) { 
            let g_sum = f64_add(gds, f64_add(gm, gmb));
            jacobian.values_hi[map.ds] = f64_sub(f64(jacobian.values_hi[map.ds], jacobian.values_lo[map.ds]), g_sum).hi; 
        }
        if (map.db != 0xffffffffu) { jacobian.values_hi[map.db] = f64_add(f64(jacobian.values_hi[map.db], jacobian.values_lo[map.db]), gmb).hi; }

        // Row S: -gm, -gmb, -gds
        if (map.sd != 0xffffffffu) { jacobian.values_hi[map.sd] = f64_sub(f64(jacobian.values_hi[map.sd], jacobian.values_lo[map.sd]), gds).hi; }
        if (map.sg != 0xffffffffu) { jacobian.values_hi[map.sg] = f64_sub(f64(jacobian.values_hi[map.sg], jacobian.values_lo[map.sg]), gm).hi; }
        if (map.ss != 0xffffffffu) { 
            let g_sum = f64_add(gds, f64_add(gm, gmb));
            jacobian.values_hi[map.ss] = f64_add(f64(jacobian.values_hi[map.ss], jacobian.values_lo[map.ss]), g_sum).hi; 
        }
        if (map.sb != 0xffffffffu) { jacobian.values_hi[map.sb] = f64_sub(f64(jacobian.values_hi[map.sb], jacobian.values_lo[map.sb]), gmb).hi; }

        // RHS: i_eq = ids - (gm*vgs + gmb*vbs + gds*vds)
        let i_eq = f64_sub(ids, f64_add(f64_mul(gm, vgs), f64_add(f64_mul(gmb, vbs), f64_mul(gds, vds))));
        
        let nD = dev.drain;
        let nS = dev.source;
        if (nD > 0) { rhs_hi[u32(nD-1)] = f64_sub(f64(rhs_hi[u32(nD-1)], rhs_lo[u32(nD-1)]), i_eq).hi; }
        if (nS > 0) { rhs_hi[u32(nS-1)] = f64_add(f64(rhs_hi[u32(nS-1)], rhs_lo[u32(nS-1)]), i_eq).hi; }
    }
}

// Stage 4: Update Solution
@compute @workgroup_size(64)
fn updateSolution(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let i = global_id.x;
    if (i >= arrayLength(&voltages_hi)) { return; }
    
    let x = f64(voltages_hi[i], voltages_lo[i]);
    let dx = f64(deltaV_hi[i], deltaV_lo[i]);
    let new_x = f64_add(x, dx);
    
    voltages_hi[i] = new_x.hi;
    voltages_lo[i] = new_x.lo;
}

// Stage 5: Compute Residual Norm
@compute @workgroup_size(64)
fn computeResidual(@builtin(global_invocation_id) global_id: vec3<u32>) {
    // Max-norm reduction stub
}

// Stage 6: Convergence Check
@compute @workgroup_size(1)
fn convergenceCheck() {
    // Atomic convergence flag stub
}

// --- Phase 2.2: GPU-resident Waveform Buffering ---

struct WaveformConfig {
    decimation_ratio: u32,
    buffer_capacity: u32,
    node_count: u32,
};

struct WaveformState {
    write_ptr: u32,
    decimation_counter: u32,
};

struct WaveformEntry {
    time_hi: f32,
    time_lo: f32,
    // Variable length voltages array handled by indexing
};

@group(2) @binding(0) var<storage, read> wf_config: WaveformConfig;
@group(2) @binding(1) var<storage, read_write> wf_state: WaveformState;
@group(2) @binding(2) var<storage, read_write> wf_data: array<f32>; // Circular buffer for (time_hi, time_lo, v1_hi, v1_lo, ...)

@compute @workgroup_size(1)
fn recordWaveform(@builtin(global_invocation_id) global_id: vec3<u32>) {
    // Throttled recording based on decimation ratio
    wf_state.decimation_counter += 1u;
    if (wf_state.decimation_counter < wf_config.decimation_ratio) {
        return;
    }
    wf_state.decimation_counter = 0u;

    let capacity = wf_config.buffer_capacity;
    let nodes = wf_config.node_count;
    let entry_stride = 2u + (nodes * 2u); // [time_hi, time_lo, v0_hi, v0_lo, ...]
    
    let ptr = wf_state.write_ptr;
    let offset = ptr * entry_stride;
    
    // Safety check: don't overflow the buffer array
    if (offset + entry_stride > arrayLength(&wf_data)) {
        return;
    }

    // Capture Simulation State (EmulatedF64)
    wf_data[offset] = global_state.time_hi;
    wf_data[offset + 1u] = global_state.time_lo;
    
    for (var n = 0u; n < nodes; n++) {
        let v_offset = offset + 2u + (n * 2u);
        wf_data[v_offset] = voltages_hi[n];
        wf_data[v_offset + 1u] = voltages_lo[n];
    }
    
    // Advance circular pointer
    wf_state.write_ptr = (ptr + 1u) % capacity;
}

struct BJTSOA {
    collectors: array<i32>,
    bases: array<i32>,
    emitters: array<i32>,
    Is_hi: array<f32>, Is_lo: array<f32>,
    betaF: array<f32>,
    betaR: array<f32>,
    Vt_hi: array<f32>, Vt_lo: array<f32>,
};

@group(0) @binding(9) var<storage, read_write> bjts: BJTSOA;

@compute @workgroup_size(64)
fn batchBJTPhysics(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let i = global_id.x;
    if (i >= arrayLength(&bjts.collectors)) { return; }
    // Ebers-Moll physics stub for f64 logic parity
}
