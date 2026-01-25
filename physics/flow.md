# Physics Module Context & Flow

## Overview
The `physics/` directory contains the core numerical engine of CuteApp. It is designed as a "Dual Kernel" architecture where **Physical Truth** (Exact constitutive equations) is separated from **Numerical Stability** (Linearization/Clamping). The engine uses a "Tensor" data layout (SoA) to enable future GPU acceleration and strict memory locality.

## Key Files & Responsibilities

### 1. Constitutive Equations (`device_physics.h`)
**Role**: The "Source of Truth". Contains pure, stateless C++ functions defining low-level device behavior.
-   **Design**: Templated for `double` (fast scalar) and `Dual<double>` (Automatic Differentiation).
-   **Key Concepts**:
    -   **Exact vs. Clamped**: `diode_current_clamped` uses linear continuation to prevent numerical explosion during the solve phase.
    
    ```cpp
    // physics/device_physics.h
    template <typename T, typename P>
    inline T diode_current_clamped(T vd, const DiodeParams<P>& p, double max_arg = 30.0) 
    {
        // Linear Continuation for stability
        T arg = vd / (p.N * p.Vt);
        if (arg > max_arg) {
            // ... linear extrapolation logic ...
            return i_max + delta * g_max; 
        }
        return p.Is * (std::exp(arg) - 1.0);
    }
    ```

    -   **PNJLIM**: SPICE-style voltage limiting logic (`pnjlim`) to dampen Newton-Raphson oscillations.
    
    ```cpp
    // physics/device_physics.h
    inline double pnjlim(double vnew, double vold, double vt, double vcrit) {
        if (vnew > vcrit && std::abs(vnew - vold) > 2.0 * vt) {
            // ... strict limiting logic ...
            vlim = vold + vt * std::log(arg);
        }
        return vlim;
    }
    ```

### 2. Matrix Assembly (`stamps.h`)
**Role**: The "Mapper". Translates physical quantities (Current, Conductance) into Linear Algebra terms (Matrix entries, RHS vector).
-   **Pattern**: MNA (Modified Nodal Analysis) stamping.
-   **Voltage Sources**: Implemented as "Norton Equivalents" to minimize matrix fill-in.
    
    ```cpp
    // physics/stamps.h
    inline void stampVoltageSourceAsNorton(...) {
        double g_int = 1.0 / 1e-3; // High conductance
        double i_eq = voltage * g_int;
        
        // Pure Nodal formulation avoids extra rows for voltage sources
        matrixBuilder.add(nodePos - 1, nodePos - 1, g_int);
        rhsVector[nodePos - 1] += i_eq;
    }
    ```

### 3. Solver Engine (`circuitsim.h`)
**Role**: The "Orchestrator". Manages the time-step evolution and non-linear solution search.
-   **IntegratorArbiter**: Monitors stability via LTE and Lyapunov Energy checks.

    ```cpp
    // physics/circuitsim.h :: IntegratorArbiter
    int analyzeStability(..., double dt, double &next_dt_suggestion) {
        double lte = calculateLTE(netlist, current_voltages, dt);
        
        // M4: Lyapunov Energy Guard
        bool energy_exploding = (current_energy > prev_energy + energy_tolerance) ...;
        
        if (high_lte || energy_exploding) {
             next_dt_suggestion = dt * 0.5; // Cut step
             return 1; // Retry
        }
        return 0; // Accept
    }
    ```

-   **Audit**: `calculatePhysicalResiduals` checks KCL conservation using EXACT physics (no clamping).

    ```cpp
    // physics/circuitsim.h
    double calculatePhysicalResiduals(...) {
        for (const auto &d : netlist.globalBlock.diodes) {
            // Sum currents entering/leaving nodes
            addRes(d.anode, -i_d);
            addRes(d.cathode, i_d);
        }
        // Returns max absolute KCL violation (Amps)
    }
    ```

### 4. Configuration (`simulation_profile.h`)
**Role**: The "Contract". Defines what a simulation run looks like.

    ```cpp
    // physics/simulation_profile.h
    struct SimulationProfile {
        AnalysisType analysis = AnalysisType::DC;
        size_t topologyHash = 0; // Links to unique netlist state
        
        // Explicit constraints for valid run
        bool requiresDC = false;
        bool requiresERC = true; 
    };
    ```

## Data Flow
1.  **Input**: `TensorNetlist` (State + Definition). `BlockState` initialized per instance.
2.  **Loop**:
    -   `CircuitSim::stepTransient` calls `IntegratorArbiter`.
    -   If stable, `matrixBuilder` assembles Jacobian via `stamps.h` (Parallel/Batch).
    -   Solver computes $\Delta V$.
    -   **State Update**: `BlockState` histories (inductors/capacitors) updated.
    -   `calculatePhysicalResiduals` validates the result against physics.
3.  **Output**: `SimulationTrace` containing time-series voltages.

## Development Rules
1.  **Stateless Physics**: Never introduce state variables (like `history`) into `device_physics.h` kernels.
2.  **Explicit Clamping**: All exponentials MUST have a `_clamped` variant.
3.  **Generic Math (ADL)**: Do NOT use `std::exp` or `std::sin` directly in device kernels. Use unqualified `exp()` to allow Argument Dependent Lookup (ADL) to find `acutesim::exp(Dual)` or defaults.
4.  **KCL is King**: Steps are rejected if `calculatePhysicalResiduals` > 1pA, even if the linear solver converged.
