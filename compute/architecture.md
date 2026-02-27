# Compute Architecture & Performance Doctrine

This document serves as the absolute contract for the computational core of the AcuteSim platform. It enforces execution safety, structural integrity, and performance realism for SIMD, Multicore, and SIMT (WebGPU) execution.

## 1. Memory Layout Guarantees

*   **Structure of Arrays (SoA) Enforcement**: All tensor structures must strictly follow the SoA format to guarantee contiguous memory accesses.
    *   *Rule*: `struct DeviceBlock { double* __restrict__ p1; double* __restrict__ p2; };`
    *   *Violation*: `std::vector<Device>` where `Device` contains multiple parameters. Interleaved AoS vectors defining devices are banned in inner loops.
*   **Zero-Copy Constraint**: Memory layout in C++ must exactly match WGSL/Dawn layout.
    *   Explicit padding must be present.
    *   Explicit alignment annotations (32 or 64 bytes).
    *   No virtual methods in GPU-facing structs.
    *   No C++ STL containers (`std::vector`, `std::map`) inside structures meant for WebGPU storage buffers; they must be flat POD arrays with explicit size metadata.

## 2. Determinism Guarantees

*   **Bitwise Reproducibility**: Local execution must precisely match remote execution. Floating-point accumulation order perfectly dictates determinism.
*   **Reduction Order**: Must be identical run-to-run. Parallel unordered reductions are prohibited on the strict path.
*   **Execution Modes**:
    *   **Strict Mode**: Disables FMA contraction, forces consistent reduction order. Guarantees bitwise identical results.
    *   **Fast Mode**: Allows non-deterministic FP reductions for maximum hardware throughput.
*   **Monte Carlo**: Uses seeded PRNGs (e.g., `seed=42`) with strict run-index offsets to guarantee identical random sequences regardless of thread scheduling.
*   **Rule**: `No unordered_map in deterministic paths`.

## 3. SIMD Policy

*   **Branch Elimination**: Inner loops (device evaluation, matrix stamping) must be completely branchless.
    *   *Rule*: Pre-bucket devices by type (e.g., `evaluateResistors()`, `evaluateMOS()`). Do not branch on `device.type` inside the tight execution loop.
*   **Vectorization**: Write kernels assuming auto-vectorization first, falling back to manual AVX2/AVX512 intrinsics when compiler auto-vectorization fails to achieve optimal pipeline throughput.
*   **Dynamic Allocation**: `No dynamic allocation inside inner loops`. All memory must be acquired and explicitly sized during the pre-simulation topological analysis phase.

## 4. GPU Dispatch Policy (WebGPU / SIMT)

*   **Dispatcher Rules**: `KernelDispatcher` operates *only* on contiguous SoA buffers with no pointer chasing.
*   **Matrix Assembly**: GPUs execute dynamic sparse stamping poorly. Assembly strategies must be:
    1.  Assemble on CPU, solve on GPU.
    2.  Fully dense block assembly (for localized standard cells).
    3.  Pre-allocated CSR/ELL hybrid formats constructed *a priori*.
*   *Rule*: Do not attempt dynamic random sparse insertion inside a GPU kernel.

## 5. Thread-Safe Sparse Accumulation

*   **CPU Multicore**: Unsynchronized shared Jacobian accumulation like `jacobian[i] += value;` destroys determinism and causes race conditions.
*   **Sanctioned Strategy**: Use thread-local accumulation buffers (scratchpads) per worker thread, followed by a deterministic merge/reduction phase spanning all threads.
*   **Safe Parallel Targets**: Device evaluation per bucket, Monte Carlo run-level parallelization.
*   **Unsafe**: Shared matrix manipulation without explicit deterministic reduction passes.

---

*This document is enforced by code review, static analysis, compiler flags, and CI regression testing.*
