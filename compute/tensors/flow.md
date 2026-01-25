# Tensors Flow

The `compute/tensors/` directory defines the **Data Layout** of the simulation engine.
It implements the logic to convert the user-facing "Array of Structures" (AoS) (e.g., `std::vector<Resistor>`) into a solver-optimized "Structure of Arrays" (SoA) (e.g., `struct ResistorTensor { vector<double> R; ... }`).

## Philosophy: Data-Oriented Design (DoD)

AcuteSim distinguishes between a component's **Identity** (Component ID, Name, Metadata) and its **Physics** (Conductanace, Current, Voltage).
*   **Identity** lives in `components/`. It's object-oriented, easy to edit, and inefficient to compute.
*   **Physics** lives in `compute/tensors/`. It's data-oriented, harder to edit, but optimal for SIMD (Single Instruction, Multiple Data) and GPU compute.

## Key Files & Structures

### 1. `physics_tensors.h`: The SoA Definition
Defines the `TensorizedBlock` structure, which holds typed tensors for each device class:
*   `DiodeTensor`: `anodes[]`, `cathodes[]`, `Is[]`, `v_d[]`, `i_d[]`, `g_d[]`.
*   `MosfetTensor`: `drains[]`, `gates[]`, `sources[]`, `W[]`, `L[]`, `vgs[]`, `ids[]`.
*   **Design Rule Masks (`dr_validity`)**: GPU-compatible `uint8` flags (e.g., `DR_THERMAL_VIOLATION`) that allow validitation to happen in parallel with physics evaluation.

### 2. `physics_tensors.cpp`: The Kernel Launcher
Implements the **AoS -> SoA Conversion** (`tensorizeBlock`) and the **Global Physics Wrappers**.
*   **Conversion**: Iterates once over the netlist to pack data into contiguous memory. Includes safety clamps (e.g., Min Resistor = 0.5 Ohm).
*   **Wrappers**: `batchDiodePhysics` etc. now delegate execution to the `KernelDispatcher`.

### 3. `kernel_dispatch.h/cpp`: The Runtime Router
Detects CPU features (AVX-512, regular AVX2, ARM Neon) at startup (`CPUID`) and binds function pointers to the optimal implementation.
*   **Single dispatch**: Checks hardware ONCE at startup, not every loop iteration.
*   **Fallback**: Defaults to "Generic" (Compiler Auto-Vectorized) kernels if no specialized hardware is found.

### 4. `physics_kernels_avx512.cpp`: Optimized SIMD
Contains hand-written intrinsics (`<immintrin.h>`) for AVX-512 enabled processors.
*   **Guarded**: Only compiles if `__AVX512F__` is defined.
*   **Performance**: Uses 512-bit registers (8 doubles per cycle) for massive throughput.

## Core Logic & Snippets

### Batch Physics Kernel (`physics_tensors.h`)
This loop has no branches (jumps) dependent on data, allowing the compiler to generate SIMD instructions.

```cpp
inline void batchDiodePhysics(DiodeTensor& tensor, const vector<double>& voltages) {
    size_t n = tensor.size();
    for (size_t i = 0; i < n; ++i) {
        // 1. Load Data (Contiguous Access)
        double v_d = voltages[tensor.anodes[i]] - voltages[tensor.cathodes[i]];
        double Is = tensor.Is[i];
        
        // 2. Compute (Linear Continuation / Limiting)
        // ... (Math logic without if/else branching) ...
        
        // 3. Store Result (Contiguous Write)
        tensor.i_d[i] = i_d_new;
        tensor.g_d[i] = g_d_new;
    }
}
```

### Safety Clamping (`physics_tensors.cpp`)
Data conversion is the firewall where we sanitize inputs before they reach the critical numerical core.

```cpp
TensorizedBlock tensorizeBlock(const TensorBlock& block) {
    const double R_MIN_SAFE = 0.5; // Avoid Divide-by-Zero in nodal analysis
    
    for (const auto& r : block.resistors) {
        double safeR = r.resistance_ohms;
        if (safeR < R_MIN_SAFE) safeR = R_MIN_SAFE;
        result.resistors.push(..., safeR);
    }
    return result;
}
```

## Development Rules

1.  **Contiguity is King**: Never use `std::list` or `std::map` inside a Tensor. Always use `std::vector`.
2.  **Align Data**: Keep frequently accessed "Hot" data (Voltage, Current, Conductance) in separate arrays from "Cold" data (Temperature, Names) if possible to maximize cache line utilization.
3.  **Branchless Math**: Within batch functions, avoid `if(v > 0)` logic. Use `std::max(0.0, v)` or bitwise masks to maintain SIMD lanes.
4.  **GPU Compat**: The structures in `physics_tensors.h` must map 1:1 to the Storage Buffers defined in `solvers/webgpu_solver.h`. If you add a field here, update the shader binding there.
