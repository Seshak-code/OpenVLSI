# Compute Math Flow

The `compute/math/` directory contains the **Numerical Kernel** of AcuteSim. It provides the fundamental algebra and calculus operations required to solve circuit equations.

It is designed to be:
1.  **Sparse-Optimized**: Using Compressed Sparse Row (CSR) storage for memory efficiency.
2.  **Parallel-Ready**: Leveraging OpenMP for vector operations and Matrix-Vector Multiplication (SpMV).
3.  **Method-Agnostic**: Using Strategy Patterns to swap integration methods (Trapezoidal vs Gear) dynamically.

## Key Files & Responsibilities

### 1. `linalg.h`: The Linear Algebra Engines
Implements the matrix storage and solvers for `Ax = b`.
*   **Storage**: `Csr_matrix` (Compressed Sparse Row) and `MatrixConstructor` (Triplet-based assembly).
*   **Iterative Solver**: `solvePCG` (Preconditioned Conjugate Gradient). Best for large, symmetric, diagonally dominant matrices (e.g., pure resistor/mesh grids). uses Jacobi Preconditioning.
*   **Direct Solver**: `solveLU_Pivoted` (Dense LU with Partial Pivoting). The fallback for "nasty" matrices (non-symmetric, active components, singular diagonals).
*   **Parallelism**: Uses `#pragma omp parallel for` on SpMV and vector adds.

### 2. `integration_method.h`: The Time-Stepping Strategy
Defines *how* differential equations (C dv/dt = I) are discretized into algebraic equations.
*   **Interface**: `IIntegrationMethod` defines `stampCapacitor` and `predict`.
*   **Trapezoidal**: Default for accuracy. Inherently stable (A-stable) but can oscillate on stiff ringing.
*   **Gear-1 (Backward Euler)**: Maximum stability, introduces artificial damping. Used for initial convergence.
*   **Gear-2 (BDF2)**: Good compromise between accuracy and stability for transient simulations.

## Core Logic & Snippets

### Matrix Assembly (`linalg.h`)
Circuit stamps are collected as "Triplets" (row, col, value) and then compressed. This handles the collision of multiple values summing into the same node (KCL superposition) automatically.

```cpp
Csr_matrix createCsr() {
    // 1. Sort Triplets by Row, then Column
    std::sort(triplets.begin(), triplets.end(), sortFn);
    
    // 2. Sum duplicates (KCL Superposition)
    for (auto& t : triplets) {
        if (isSamePos) values.back() += t.value;
        else pushNew(t);
    }
    
    // 3. Generate Row Pointers (Prefix Sum)
    for (int i = 0; i < numRows; ++i)
        matrix.row_pointer[i+1] += matrix.row_pointer[i];
}
```

### Integration Strategy (`integration_method.h`)
The solver doesn't know *which* method is active; it just calls `stampCapacitor`.

```cpp
void TrapezoidalIntegration::stampCapacitor(..., MatrixConstructor& mat, std::vector<double>& rhs) {
    // I = C * (Vn - Vn-1)/dt  ->  Discretized via Trapezoidal Rule
    double G_eq = (2.0 * C) / timeStep;
    double I_eq = -G_eq * v_prev - i_prev; // Companion Model Source
    
    // Stamp Companion Conducance (G_eq)
    mat.add(nodeI, nodeI, G_eq); 
    
    // Stamp Companion Current Source (I_eq)
    rhs[nodeI - 1] -= I_eq;
}
```

### Solver Selection (`linalg.h`)
The system tries PCG first for speed, then falls back to LU if convergence fails.

```cpp
// PCG relies on M^-1 = 1/diag(A) preconditioner
std::vector<double> getDiagonalInverse(const Csr_matrix &matrix) {
    #pragma omp parallel for
    for (int i = 0; i < rows; ++i) {
        // ... find diagonal element ...
        if (abs(val) > 1e-18) diagInv[i] = 1.0 / val;
    }
}
```

## Development Rules

1.  **Thread Safety**: Any function in `linalg.h` marked with OMP must operate on disjoint data or use reductions.
2.  **Singularity Checks**: Solvers must return `result.converged = false` rather than crashing on singular matrices (floating nodes).
3.  **Dense Fallback**: Always maintain `convertCsrToDense` and `solveLU_Pivoted` as a gold-standard reference, even if PCG is preferred.
4.  **No Allocations in Loop**: `MatrixConstructor` should `reserve()` space if possible to avoid reallocations during stamping.
