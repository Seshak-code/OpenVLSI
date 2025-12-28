#pragma once
#include "linalg.h"
#include "circuit.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cassert>  // NASA Rule #5

/**
 * @file circuitsim.h
 * @brief Circuit Simulator - Physics and Solver Engine.
 */

// ============================================================================
// STAMP FUNCTIONS (Component -> Matrix Entry)
// ============================================================================

inline void stampResistor(int nodeI, int nodeJ, double resistance, MatrixConstructor& matrixBuilder)
{
    double g = 1.0 / resistance;
    if (nodeI != 0) 
    {
        matrixBuilder.add(nodeI - 1, nodeI - 1, g);
        if (nodeJ != 0) matrixBuilder.add(nodeI - 1, nodeJ - 1, -g);
    }
    if (nodeJ != 0) 
    {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, g);
        if (nodeI != 0) matrixBuilder.add(nodeJ - 1, nodeI - 1, -g);
    }
}

inline void stampCapacitorTrapezoidal(int nodeI, int nodeJ, double C, double h, 
                                       double v_prev, double i_prev, 
                                       MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    double G_eq = (2.0 * C) / h;
    double I_eq = -G_eq * v_prev - i_prev;

    if (nodeI != 0) {
        matrixBuilder.add(nodeI - 1, nodeI - 1, G_eq);
        if (nodeJ != 0) matrixBuilder.add(nodeI - 1, nodeJ - 1, -G_eq);
    }
    if (nodeJ != 0) {
        matrixBuilder.add(nodeJ - 1, nodeJ - 1, G_eq);
        if (nodeI != 0) matrixBuilder.add(nodeJ - 1, nodeI - 1, -G_eq);
    }

    if (nodeI != 0) rhsVector[nodeI - 1] -= I_eq;
    if (nodeJ != 0) rhsVector[nodeJ - 1] += I_eq;
}

inline void stampVoltageSourceAsNorton(int nodePos, int nodeNeg, double voltage, 
                                        MatrixConstructor& matrixBuilder, std::vector<double>& rhsVector)
{
    double r_int = 1e-6;
    double g_int = 1.0 / r_int;
    double i_eq = voltage * g_int;

    stampResistor(nodePos, nodeNeg, r_int, matrixBuilder);

    if (nodePos != 0) rhsVector[nodePos - 1] += i_eq;
    if (nodeNeg != 0) rhsVector[nodeNeg - 1] -= i_eq;
}

inline void stampMosfet(int d, int g, int s, int b, double gm, double gds, MatrixConstructor& mat) {
    // Linearized Small-Signal Model Stamp
    // i_d += gm * (vg - vs) + gds * (vd - vs)
    // i_s -= gm * (vg - vs) - gds * (vd - vs)
    
    auto add = [&](int r, int c, double val) {
        if (r != 0 && c != 0) mat.add(r-1, c-1, val);
    };
    
    // GM Term (VCCS)
    add(d, g, gm); add(d, s, -gm);
    add(s, g, -gm); add(s, s, gm);
    
    // GDS Term (Resistor)
    add(d, d, gds); add(d, s, -gds);
    add(s, d, -gds); add(s, s, gds);
}

// ============================================================================
// CIRCUITSIM CLASS
// ============================================================================

#include "simtrace.h"
#include "tensorscheduler.h"

// ============================================================================
// CIRCUITSIM CLASS
// ============================================================================

/**
 * @brief CircuitSim - Physics and Solver Engine.
 * 
 * In Phase 0/1, this produces SimulationResult (legacy) or writes to a 
 * SimulationTrace (modern research-grade).
 */
#include "integration_method.h"

// ... (SimTrace/TensorScheduler includes)

// ... (Previous includes) ...

/**
 * @brief Heuristic Arbiter for Adaptive Integration (Hardened).
 * Monitors energy stability, weighted ringing, and LTE.
 */
class IntegratorArbiter {
private:
    std::vector<double> prev_voltages;
    std::vector<double> prev_prev_voltages;
    std::vector<double> node_weights; // Physical significance (sum of C and G)
    double prev_energy = 0.0;
    int instability_counter = 0;
    static constexpr int INSTABILITY_THRESHOLD = 3; 

public:
    void initialize(const TensorNetlist& netlist) {
        int n = netlist.numGlobalNodes;
        prev_voltages.assign(n, 0.0);
        prev_prev_voltages.assign(n, 0.0);
        node_weights.assign(n, 1e-12); // Noise floor
        prev_energy = 0.0;
        instability_counter = 0;

        // Calculate Physical Weights: nodes with high C/G are more significant
        auto addWeight = [&](int node, double w) {
            if (node > 0 && node <= n) node_weights[node - 1] += std::abs(w);
        };

        // Scan Global Block
        for (const auto& r : netlist.globalBlock.resistors) {
            double g = 1.0 / r.resistance_ohms;
            addWeight(r.nodeTerminal1, g); addWeight(r.nodeTerminal2, g);
        }
        for (const auto& c : netlist.globalBlock.capacitors) {
            addWeight(c.nodePlate1, c.capacitance_farads); addWeight(c.nodePlate2, c.capacitance_farads);
        }

        // Scan Instances (Tensors)
        for (const auto& inst : netlist.instances) {
            auto it = netlist.blockDefinitions.find(inst.blockName);
            if (it == netlist.blockDefinitions.end()) continue;
            const auto& block = it->second;
            
            auto mapNode = [&](int localNode) {
                return (localNode == 0) ? 0 : inst.nodeMapping[localNode - 1];
            };

            for (const auto& r : block.resistors) {
                double g = 1.0 / r.resistance_ohms;
                addWeight(mapNode(r.nodeTerminal1), g); addWeight(mapNode(r.nodeTerminal2), g);
            }
            for (const auto& c : block.capacitors) {
                addWeight(mapNode(c.nodePlate1), c.capacitance_farads); addWeight(mapNode(c.nodePlate2), c.capacitance_farads);
            }
        }
    }

    const std::vector<double>& getPrevVoltages() const { return prev_voltages; }

    /**
     * @brief Monitors health and recommends Tier-1 (dt) or Tier-2 (method) switch.
     * @return 0: OK, 1: Reduce DT, 2: Switch Method
     */
    int analyzeStability(const TensorNetlist& netlist, const std::vector<double>& current_voltages, IIntegrationMethod* integrator) {
        if (current_voltages.empty()) return 0;
        if (current_voltages.size() != prev_voltages.size()) {
            initialize(netlist);
            return 0;
        }

        // 1. Calculate Circuit Energy: E = 0.5 * sum(C * v^2)
        double current_energy = 0.0;
        for (const auto& c : netlist.globalBlock.capacitors) {
            double v = 0.0;
            if (c.nodePlate1 != 0) v += current_voltages[c.nodePlate1 - 1];
            if (c.nodePlate2 != 0) v -= current_voltages[c.nodePlate2 - 1];
            current_energy += 0.5 * c.capacitance_farads * v * v;
        }
        // (Simplify: Only check global block energy for now)

        // 2. Weighted Ringing & LTE
        double total_err = 0.0;
        double weight_sum = 0.0;
        int weighted_sign_flips = 0;

        for (size_t i = 0; i < current_voltages.size(); ++i) {
            double v = current_voltages[i];
            double v_prev = prev_voltages[i];
            double v_prev2 = prev_prev_voltages[i];
            double w = node_weights[i];

            // LTE: Distance from predictor
            double v_pred = integrator->predict(prev_voltages); // Approximate
            // Better: use specific node history if available? 
            // Simple approach: calculate weighted absolute sign flip delta
            double dv = v - v_prev;
            double dv_prev = v_prev - v_prev2;

            if (std::abs(dv) > 1e-6 && (dv * dv_prev < 0)) {
                weighted_sign_flips += (int)(w * 1e9); // Scale weighting
            }
        }

        bool energy_exploding = (current_energy > prev_energy * 1.5 + 1e-9 && prev_energy > 1e-12);
        
        // Update history
        prev_prev_voltages = prev_voltages;
        prev_voltages = current_voltages;
        prev_energy = current_energy;

        if (energy_exploding) return 1; // Tier 1: Try reducing DT
        if (weighted_sign_flips > 100) {
            instability_counter++;
            if (instability_counter >= INSTABILITY_THRESHOLD) return 2; // Tier 2: Switch Method
            return 1; // Tier 1
        } else {
            instability_counter = std::max(0, instability_counter - 1);
        }

        return 0;
    }
};

class CircuitSim
{
    public:
        // ... (Constructor/SethMethod remain same) ...
        
        IntegrationType getIntegrationMethod() const { return integrator->getType(); }

// ... (Rest of class, adding arbiter member)
        CircuitSim() {
            // Default to Trapezoidal
            integrator = std::make_unique<TrapezoidalIntegration>();
        }
        
        // Allow moving (required for unique_ptr member)
        CircuitSim(CircuitSim&&) = default;
        CircuitSim& operator=(CircuitSim&&) = default;
        
        void setIntegrationMethod(IntegrationType type) {
            switch(type) {
                case IntegrationType::TRAPEZOIDAL:
                    integrator = std::make_unique<TrapezoidalIntegration>();
                    break;
                case IntegrationType::GEAR_1_EULER:
                    integrator = std::make_unique<Gear1Integration>();
                    break;
                case IntegrationType::GEAR_2:
                    integrator = std::make_unique<Gear2Integration>();
                    break;
            }
        }
        
        SimulationStep solveDC(TensorNetlist& netlist);
        SimulationStep stepTransient(TensorNetlist& netlist, double timeStep, double currentTime);

    private:
        MatrixConstructor matrixBuilder;
        std::vector<double> rhsVector;
        TensorScheduler scheduler;
        
        // NR Parameters
        const double RELTOL = 1e-3;
        const double VNTOL  = 1e-6;
        const int MAX_NR_ITER = 20;

        bool checkConvergence(const std::vector<double>& v_new, const std::vector<double>& v_old) {
            if (v_new.size() != v_old.size()) return false;
            for (size_t i = 0; i < v_new.size(); ++i) {
                double diff = std::abs(v_new[i] - v_old[i]);
                double limit = RELTOL * std::max(std::abs(v_new[i]), std::abs(v_old[i])) + VNTOL;
                if (diff > limit) return false;
            }
            return true;
        }
        
        // Phase 2: Numerical Integration Strategy
        std::unique_ptr<IIntegrationMethod> integrator;
        IntegratorArbiter arbiter; // The Arbiter instance
        
        // --- SOLID/NASA Helper Functions ---
        
        void stampGlobalDC(const TensorBlock& block) {
            for (const auto& r : block.resistors) {
                stampResistor(r.nodeTerminal1, r.nodeTerminal2, r.resistance_ohms, matrixBuilder);
            }
            for (const auto& v : block.voltageSources) {
                stampVoltageSourceAsNorton(v.nodePositive, v.nodeNegative, v.voltage_V, matrixBuilder, rhsVector);
            }
        }
        
        void stampGlobalTransient(TensorBlock& block, double timeStep) {
            for (const auto& r : block.resistors) {
                stampResistor(r.nodeTerminal1, r.nodeTerminal2, r.resistance_ohms, matrixBuilder);
            }
            for (auto& c : block.capacitors) {
                // Delegate to Strategy
                integrator->stampCapacitor(
                    c.nodePlate1, c.nodePlate2, c.capacitance_farads, timeStep,
                    c.voltage_history, c.current_history,
                    matrixBuilder, rhsVector
                );
            }
            for (const auto& v : block.voltageSources) {
                stampVoltageSourceAsNorton(v.nodePositive, v.nodeNegative, v.voltage_V, matrixBuilder, rhsVector);
            }
        }

        void stampBatchedJobs(const TensorNetlist& netlist, bool includeTransient, double timeStep = 0.0) {
            // ... (Same as before) ...
            for (const auto& job : scheduler.jobs) {
                const TensorBlock& block = *job.blockTmpl;
                for (int instanceIdx : job.instanceIds) {
                     const auto& instance = netlist.instances[instanceIdx];
                     auto mapNode = [&](int localNode) { return (localNode == 0) ? 0 : instance.nodeMapping[localNode - 1]; };
                     for (const auto& r : block.resistors) {
                         stampResistor(mapNode(r.nodeTerminal1), mapNode(r.nodeTerminal2), r.resistance_ohms, matrixBuilder);
                     }
                     // Instance Capacitors would go here (omitted for brevity as per previous logic)
                     for (const auto& m : block.mosfets) {
                         // ... (Mosfet logic same as before) ...
                         double W = m.channelWidth_m;
                         double L = m.channelLength_m;
                         std::string paramW = m.instanceName + ".W";
                         if (instance.parameters.count(paramW)) W = instance.parameters.at(paramW);
                         std::string paramL = m.instanceName + ".L";
                         if (instance.parameters.count(paramL)) L = instance.parameters.at(paramL);
                         
                         double Kp = 200e-6;
                         double Vov = 0.5;
                         double gm = Kp * (W/L) * Vov;
                         double gds = gm / 100.0;
                         stampMosfet(mapNode(m.nodeDrain), mapNode(m.nodeGate), mapNode(m.nodeSource), mapNode(m.nodeBody), gm, gds, matrixBuilder);
                     }
                }
            }
        }
        
        void addDiagonalConditioning(int n) {
            for (int i = 0; i < n; ++i) matrixBuilder.add(i, i, 1e-12);
        }

        void stampNonLinearDevices(TensorNetlist& netlist, const std::vector<double>& v_guess) 
        {
            // BLANK 1: Implement Non-Linear Stamping (Jacobian + Residual)
            // Iterate over netlist.globalBlock.diodes, mosfets, etc.
            // Use v_guess to calculate linearized equivalent conductance and current source.
            TensorBlock& globalBlock = netlist.globalBlock;
            
            
        }
};

// ... solveDC implementation ...

inline SimulationStep CircuitSim::solveDC(TensorNetlist& netlist)
{
    if (netlist.numGlobalNodes == 0) return {0.0, {}, {0, 0.0, true}};

    std::vector<double> v_guess(netlist.numGlobalNodes, 0.0);
    // BLANK 2: Initial Guess Heuristics (Cold start vs Warm start)
    
    bool converged = false;
    int total_iters = 0;

    for (int it = 0; it < MAX_NR_ITER; ++it) {
        matrixBuilder = MatrixConstructor();
        rhsVector.assign(netlist.numGlobalNodes, 0.0);
        
        stampGlobalDC(netlist.globalBlock);
        stampBatchedJobs(netlist, false);
        stampNonLinearDevices(netlist, v_guess); 
        addDiagonalConditioning(netlist.numGlobalNodes);

        Csr_matrix A = matrixBuilder.createCsr();
        SolverResult solverRes = solvePCG(A, rhsVector);
        
        std::vector<double> v_new = solverRes.solution;
        // BLANK 3: NR Damping / Limiting (e.g. pnjlim for diodes)
        
        if (checkConvergence(v_new, v_guess)) {
            v_guess = v_new;
            converged = true;
            total_iters = it + 1;
            break;
        }
        v_guess = v_new;
        total_iters = it + 1;
    }

    return {0.0, v_guess, {total_iters, 0.0, converged}};
}

inline SimulationStep CircuitSim::stepTransient(TensorNetlist& netlist, double timeStep, double currentTime)
{
    if (netlist.numGlobalNodes == 0) return {currentTime, {}, {0, 0.0, true}};

    static bool arbiter_initialized = false;
    if (!arbiter_initialized) {
        arbiter.initialize(netlist);
        arbiter_initialized = true;
    }

    double achieved_dt = timeStep;
    int retries = 0;
    const int MAX_RETRIES = 4;
    
    std::vector<double> v_final;
    SolverResult lastSolverRes;
    bool step_converged = false;

    while (retries < MAX_RETRIES) {
        // NR Loop for current time step
        std::vector<double> v_nr_guess = arbiter.getPrevVoltages(); // Start from last converged step
        if (v_nr_guess.empty()) v_nr_guess.assign(netlist.numGlobalNodes, 0.0);

        bool nr_converged = false;
        for (int it = 0; it < MAX_NR_ITER; ++it) {
            matrixBuilder = MatrixConstructor();
            rhsVector.assign(netlist.numGlobalNodes, 0.0);
            
            scheduler.schedule(netlist);
            stampGlobalTransient(netlist.globalBlock, achieved_dt);
            stampBatchedJobs(netlist, true, achieved_dt);
            stampNonLinearDevices(netlist, v_nr_guess);
            addDiagonalConditioning(netlist.numGlobalNodes);

            Csr_matrix A = matrixBuilder.createCsr();
            lastSolverRes = solvePCG(A, rhsVector);
            
            std::vector<double> v_new = lastSolverRes.solution;
            // BLANK 3: NR Damping / Limiting
            
            if (checkConvergence(v_new, v_nr_guess)) {
                v_nr_guess = v_new;
                nr_converged = true;
                break;
            }
            v_nr_guess = v_new;
        }

        if (!nr_converged) {
            // NR failed to converge at this DT, try Tier 1 reduction
            achieved_dt *= 0.5;
            retries++;
            continue;
        }

        // NR converged, now check Physics Stability (Arbiter)
        int suggestion = arbiter.analyzeStability(netlist, v_nr_guess, integrator.get());

        if (suggestion == 1) { 
            achieved_dt *= 0.5;
            retries++;
            continue; 
        } else if (suggestion == 2) { 
            if (integrator->getType() == IntegrationType::TRAPEZOIDAL) {
                setIntegrationMethod(IntegrationType::GEAR_2);
                continue; 
            }
        }
        
        v_final = v_nr_guess;
        step_converged = true;
        break;
    }

    // Update Global Capacitor History via Strategy
    for (auto& c : netlist.globalBlock.capacitors) {
        double v_new = 0.0;
        if (c.nodePlate1 != 0) v_new += v_final[c.nodePlate1 - 1];
        if (c.nodePlate2 != 0) v_new -= v_final[c.nodePlate2 - 1];

        double i_new = 0.0; 
        IntegrationType type = integrator->getType();
        
        if (type == IntegrationType::TRAPEZOIDAL) {
             double G_eq = (2.0 * c.capacitance_farads) / achieved_dt;
             i_new = G_eq * (v_new - c.voltage_history[0]) - c.current_history[0];
        } else if (type == IntegrationType::GEAR_1_EULER) {
             double G_eq = c.capacitance_farads / achieved_dt;
             i_new = G_eq * (v_new - c.voltage_history[0]);
        } else if (type == IntegrationType::GEAR_2) {
             if (c.voltage_history[1] == 0.0 && currentTime > achieved_dt) {
                  double G_eq = c.capacitance_farads / achieved_dt;
                  i_new = G_eq * (v_new - c.voltage_history[0]);
             } else {
                  i_new = (c.capacitance_farads / achieved_dt) * (1.5 * v_new - 2.0 * c.voltage_history[0] + 0.5 * c.voltage_history[1]);
             }
        }
        integrator->updateHistory(c.voltage_history, c.current_history, v_new, i_new);
    }

    return {currentTime + achieved_dt, v_final, {lastSolverRes.iterations, lastSolverRes.finalResidual, step_converged}};
}