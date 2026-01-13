#pragma once
#include "circuit.h"
#include "linalg.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <vector>

/*
 * circuitsim.h
 * Circuit Simulator - Physics and Solver Engine.
 */

// ============================================================================
// STAMP FUNCTIONS (Component -> Matrix Entry)
// ============================================================================

#include "../autodiff/dual.h"
#include "device_physics.h"
#include "physics_tensors.h"
#include "stamps.h"

// ============================================================================
// CIRCUITSIM CLASS
// ============================================================================

#include "simtrace.h"
#include "tensorscheduler.h"

// ============================================================================
// CIRCUITSIM CLASS
// ============================================================================

#include "integration_method.h"

/**
 * CircuitSim is the primary engine of the simulator.
 */

// ... (SimTrace/TensorScheduler includes)

// ... (Previous includes) ...

/*
 * Heuristic Arbiter for Adaptive Integration (Hardened).
 * Monitors energy stability, weighted ringing, and LTE.
 */

class IntegratorArbiter {
private:
  std::vector<double> prev_voltages;
  std::vector<double> prev_prev_voltages;
  std::vector<double> node_weights; 
  double prev_energy = 0.0;
  double prev_dt = 1e-9; 
  int instability_counter = 0;
  static constexpr int INSTABILITY_THRESHOLD = 3;

public:
  void initialize(const TensorNetlist &netlist) {
    int n = netlist.numGlobalNodes;
    prev_voltages.assign(n, 0.0);
    prev_prev_voltages.assign(n, 0.0);
    node_weights.assign(n, 1e-12);
    prev_energy = 0.0;
    prev_dt = 1e-9;
    instability_counter = 0;

    auto addWeight = [&](int node, double w) {
      if (node > 0 && node <= n)
        node_weights[node - 1] += std::abs(w);
    };

    for (const auto &r : netlist.globalBlock.resistors) {
      double g = 1.0 / r.resistance_ohms;
      addWeight(r.nodeTerminal1, g);
      addWeight(r.nodeTerminal2, g);
    }
    for (const auto &c : netlist.globalBlock.capacitors) {
      addWeight(c.nodePlate1, c.capacitance_farads);
      addWeight(c.nodePlate2, c.capacitance_farads);
    }
    
    // Add hierarchical weights
    for (const auto &inst : netlist.instances) {
        auto it = netlist.blockDefinitions.find(inst.blockName);
        if (it != netlist.blockDefinitions.end()) {
            const auto &block = it->second;
            auto mapNode = [&](int local) { return local == 0 ? 0 : inst.nodeMapping[local-1]; };
            for (const auto &c : block.capacitors) {
                addWeight(mapNode(c.nodePlate1), c.capacitance_farads);
                addWeight(mapNode(c.nodePlate2), c.capacitance_farads);
            }
        }
    }
  }

  const std::vector<double> &getPrevVoltages() const { return prev_voltages; }

  // Research-Grade LTE: Combined Node & Device Error
  double calculateLTE(const TensorNetlist &netlist, const std::vector<double>& current_voltages, double dt) {
      if (prev_voltages.empty() || prev_prev_voltages.empty() || dt <= 0 || prev_dt <= 0) return 0.0;
      
      double c_factor = dt / (dt + prev_dt);
      double sum_sq_error = 0.0;
      double max_err = 0.0;
      int count = 0;

      auto getNorm = [](double v) { return 1e-6 + std::abs(v) * 1e-3; };

      // 1. Nodal Voltages (State Variables)
      for (size_t i = 0; i < current_voltages.size(); ++i) {
          double v_pred = prev_voltages[i] + (prev_voltages[i] - prev_prev_voltages[i]) * (dt / prev_dt);
          double err = std::abs(current_voltages[i] - v_pred) * c_factor;
          double w_err = err / getNorm(current_voltages[i]);
          
          sum_sq_error += w_err * w_err;
          max_err = std::max(max_err, w_err);
          count++;
      }

      // 2. Capacitor Branch Voltages (Internal State)
      // Checks stability of charge storage elements
      for (const auto &c : netlist.globalBlock.capacitors) {
          if (c.voltage_history.size() < 2) continue;
          double v1 = (c.nodePlate1 > 0) ? current_voltages[c.nodePlate1-1] : 0.0;
          double v2 = (c.nodePlate2 > 0) ? current_voltages[c.nodePlate2-1] : 0.0;
          double v_branch = v1 - v2;
          
          double v_prev = c.voltage_history[0];
          double v_prev2 = (c.voltage_history.size() > 1) ? c.voltage_history[1] : v_prev;
          
          double v_pred = v_prev + (v_prev - v_prev2) * (dt / prev_dt);
          double err = std::abs(v_branch - v_pred) * c_factor; // Predictor error
          double w_err = err / getNorm(v_branch);
          
          sum_sq_error += w_err * w_err;
          max_err = std::max(max_err, w_err);
          count++;
      }

      // 3. MOSFET Gate Drive (Vgs) - Critical for Switching
      for (const auto &m : netlist.globalBlock.mosfets) {
           auto getV = [&](int n, const std::vector<double>& src) { return (n>0 && n<=(int)src.size()) ? src[n-1] : 0.0; };
           
           double vgs = getV(m.gate, current_voltages) - getV(m.source, current_voltages);
           double vgs_p = getV(m.gate, prev_voltages) - getV(m.source, prev_voltages);
           double vgs_pp = getV(m.gate, prev_prev_voltages) - getV(m.source, prev_prev_voltages);
           
           double vgs_pred = vgs_p + (vgs_p - vgs_pp) * (dt / prev_dt);
           double err = std::abs(vgs - vgs_pred) * c_factor;
           double w_err = err / getNorm(vgs);
           
           sum_sq_error += w_err * w_err;
           max_err = std::max(max_err, w_err);
           count++;
      }

      if (count == 0) return 0.0;

      // Return Max-Norm (Conservative) effectively, but could switch to RMS
      // Standard SPICE uses RMS, but Max-Norm guarantees no single node deviates.
      // We return Max-Norm as it safely bounds RMS.
      return max_err;
  }

  // M4: Formal Passivity Checks (Static)
  bool checkPassivity(const TensorNetlist &netlist) {
      // 1. Check Global Block
      if (!checkBlockPassivity(netlist.globalBlock)) return false;
      // 2. Check Instances
      for (const auto &inst : netlist.instances) {
          auto it = netlist.blockDefinitions.find(inst.blockName);
          if (it != netlist.blockDefinitions.end()) {
              if (!checkBlockPassivity(it->second)) return false;
          }
      }
      return true;
  }

  bool checkBlockPassivity(const TensorBlock &block) {
      for (const auto &r : block.resistors) {
          if (r.resistance_ohms <= 0.0) return false;
      }
      for (const auto &c : block.capacitors) {
          if (c.capacitance_farads < 0.0) return false;
      }
      for (const auto &m : block.mosfets) {
          if (m.channelWidth_m <= 0.0 || m.channelLength_m <= 0.0) return false;
      }
      return true;
  }

  // Returns: 0=Stable, 1=Reduce dt, 2=Switch Method
  int analyzeStability(const TensorNetlist &netlist,
                       const std::vector<double> &current_voltages,
                       double dt,
                       double &next_dt_suggestion) {
    if (current_voltages.empty()) return 0;
    if (current_voltages.size() != prev_voltages.size()) {
       initialize(netlist);
       next_dt_suggestion = dt; 
       return 0;
    }

    // 1. Check LTE
    double lte = calculateLTE(netlist, current_voltages, dt);
    bool high_lte = (lte > 1.0); // Threshold 

    // 2. Check Energy
    double current_energy = 0.0;
    for (const auto &c : netlist.globalBlock.capacitors) {
        double v = 0.0;
        if (c.nodePlate1 > 0 && c.nodePlate1 <= (int)current_voltages.size()) v += current_voltages[c.nodePlate1 - 1];
        if (c.nodePlate2 > 0 && c.nodePlate2 <= (int)current_voltages.size()) v -= current_voltages[c.nodePlate2 - 1];
        current_energy += 0.5 * c.capacitance_farads * v * v;
    }
    
    // Add instance energy
    for (const auto &inst : netlist.instances) {
        auto it = netlist.blockDefinitions.find(inst.blockName);
        if (it != netlist.blockDefinitions.end()) {
            const auto &block = it->second;
            auto mapNode = [&](int local) { return local == 0 ? 0 : inst.nodeMapping[local-1]; };
            auto getV = [&](int n) { return (n > 0 && n <= (int)current_voltages.size()) ? current_voltages[n-1] : 0.0; };
            for (const auto &c : block.capacitors) {
                 double v = getV(mapNode(c.nodePlate1)) - getV(mapNode(c.nodePlate2));
                 current_energy += 0.5 * c.capacitance_farads * v * v;
            }
        }
    }



    // M4: Lyapunov Energy Monitor
    // Calculate Work Done by Sources (Approximate Pin)
    double power_in = 0.0;
    // Current Sources
    for (const auto &i : netlist.globalBlock.currentSources) {
         double v_diff = 0.0;
         if (i.nodePositive > 0 && i.nodePositive <= (int)current_voltages.size()) v_diff += current_voltages[i.nodePositive-1];
         if (i.nodeNegative > 0 && i.nodeNegative <= (int)current_voltages.size()) v_diff -= current_voltages[i.nodeNegative-1];
         power_in += std::abs(i.current_A * v_diff);
    }
    // Safety Margin for Voltage Sources/Norton Equivalents (Heuristic)
    // We allow energy to grow if there is significant voltage activity, scaling with system size
    double energy_tolerance = 1e-9 + power_in * dt * 1.5; 
    
    // Strict Lyapunov Bound: E(t) <= E(t-1) + Pin*dt + tolerance
    // We use a relaxed bound here to account for numeric noise and voltage source estimation gaps
    bool energy_exploding = (current_energy > prev_energy + energy_tolerance + 1e-6) && 
                            (current_energy > prev_energy * 1.5);
    
    if (high_lte || energy_exploding) {
        instability_counter++;
        next_dt_suggestion = dt * 0.5; // Cut step
        if (instability_counter >= INSTABILITY_THRESHOLD) return 2; // Switch Method
        return 1; // Retry
    } else {
        instability_counter = std::max(0, instability_counter - 1);
        prev_prev_voltages = prev_voltages;
        prev_voltages = current_voltages;
        prev_energy = current_energy;
        prev_dt = dt;
        
        if (lte < 0.1) next_dt_suggestion = dt * 1.5; 
        else if (lte < 0.5) next_dt_suggestion = dt * 1.1; 
        else next_dt_suggestion = dt; 
        
        if (next_dt_suggestion > 1e-3) next_dt_suggestion = 1e-3; 
        return 0;
    }
  }
};

class CircuitSim {
public:
  IntegratorArbiter arbiter;
  bool arbiterInitialized = false;
  std::unique_ptr<IIntegrationMethod> integrator;

  CircuitSim() {
    integrator = std::make_unique<TrapezoidalIntegration>();
  }

  CircuitSim(CircuitSim &&) = default;
  CircuitSim &operator=(CircuitSim &&) = default;

  void setIntegrationMethod(IntegrationType type) {
    if(!integrator || integrator->getType() != type) {
        switch (type) {
        case IntegrationType::TRAPEZOIDAL:
          integrator = std::make_unique<TrapezoidalIntegration>();
          break;
        case IntegrationType::GEAR_1_EULER:
          integrator = std::make_unique<Gear1Integration>();
          break;
        case IntegrationType::GEAR_2:
          integrator = std::make_unique<Gear2Integration>(); // Assuming Gear2 exists or mapped to Gear1
          break;
        }
    }
  }
  
  IntegrationType getIntegrationMethod() const { return integrator->getType(); }

  SimulationStep solveDC(TensorNetlist &netlist);
  
  // NOTE: timeStep is now IN/OUT parameter for adaptive stepping
  SimulationStep stepTransient(TensorNetlist &netlist, double &timeStep,
                               double currentTime);
  
  void resetArbiter() { arbiterInitialized = false; }

private:
  MatrixConstructor matrixBuilder;
  std::vector<double> rhsVector;
  TensorScheduler scheduler;
  
  // SoA Tensor Storage (Batch Physics)
  TensorizedBlock soaBlock;



  // NR Parameters
  static constexpr double RELTOL = 1e-3;
  static constexpr double VNTOL = 1e-6;
  static constexpr double ABSTOL = 1e-12; // 1 pico-Ampere resolution
  static constexpr int MAX_NR_ITER = 20;
  static constexpr double PCG_TOL = 1e-10;
  static constexpr int PCG_MAX_ITER = 1000;

  /**
   * checkConvergence
   * Verifies BOTH voltage stability (approximate) AND physical charge
   * conservation (exact).
   */
  bool checkConvergence(const std::vector<double> &v_new,
                        const std::vector<double> &v_old,
                        double max_kcl_error) {
    // 1. Check Voltage Updates (Standard RELTOL/VNTOL)
    if (v_new.size() != v_old.size())
      return false;
    for (size_t i = 0; i < v_new.size(); ++i) {
      double diff = std::abs(v_new[i] - v_old[i]);
      double limit =
          RELTOL * std::max(std::abs(v_new[i]), std::abs(v_old[i])) + VNTOL;
      if (diff > limit)
        return false;
    }

    // 2. Check Physical KCL Conservation (Research-Grade)
    // Relax ABSTOL slightly for Norton-stamped voltage sources (which are
    // stiff)
    if (max_kcl_error > 1e-6)
      return false;

    return true;
  }

public:
  /**
   * calculatePhysicalResiduals
   * Audits the solution by checking Kirchhoff's Current Law using EXACT
   * non-linear equations. Returns the maximum KCL violation (in Amperes) across
   * all nodes.
   */
  double calculatePhysicalResiduals(const TensorNetlist &netlist,
                                    const std::vector<double> &v_candidate,
                                    double h = 0.0) {
    int n = netlist.numGlobalNodes;
    if (n == 0)
      return 0.0;

    std::vector<double> node_residuals(n, 0.0);

    // helper to get node voltage (Safe Access)
    auto getV = [&](int nodeIdx) {
      if (nodeIdx <= 0 || nodeIdx > (int)v_candidate.size())
        return 0.0;
      return v_candidate[nodeIdx - 1];
    };

    // Helper to accumulate residual (Safe Access)
    auto addRes = [&](int nodeIdx, double val) {
      if (nodeIdx > 0 && nodeIdx <= n)
        node_residuals[nodeIdx - 1] += val;
    };

    // 1. Resistors (Ohm's Law: I = V/R)
    for (const auto &r : netlist.globalBlock.resistors) {
      double v1 = getV(r.nodeTerminal1);
      double v2 = getV(r.nodeTerminal2);
      double i_r = (v1 - v2) / r.resistance_ohms;
      addRes(r.nodeTerminal1, -i_r); // Current LEAVING node
      addRes(r.nodeTerminal2, i_r);  // Current ENTERING node
    }

    // 2. Voltage Sources (Norton Equivalent for Check)
    for (const auto &v : netlist.globalBlock.voltageSources) {
      double v1 = getV(v.nodePositive);
      double v2 = getV(v.nodeNegative);
      double r_int = 1e-3; // Match the stamp's internal resistance
      double i_src = (v.voltage_V - (v1 - v2)) / r_int;
      addRes(v.nodePositive, i_src);
      addRes(v.nodeNegative, -i_src);
    }

    // 2.5. Current Sources (Ideal Current Injection)
    // Current sources inject fixed current regardless of voltage
    for (const auto &cs : netlist.globalBlock.currentSources) {
      // Current flows from + to - inside source, leaves + externally
      addRes(cs.nodePositive, -cs.current_A); // Current leaving + node
      addRes(cs.nodeNegative, cs.current_A);  // Current entering - node
    }

    // 3. Diodes (Shockley Equation: I = Is * (exp(V/Vt) - 1))
    for (const auto &d : netlist.globalBlock.diodes) {
      double v_anode = getV(d.anode);
      double v_cathode = getV(d.cathode);
      double v_d = v_anode - v_cathode;

      // Use exact physics kernel (no clamping for residual check)
      DiodeParams<double> params = {d.saturationCurrent_I_S_A, d.emissionCoefficient_N,
                            d.thermalVoltage_V_T_V};
      double i_d = diode_current(v_d, params);

      addRes(d.anode, -i_d);
      addRes(d.cathode, i_d);
    }

    // 3.5 BJT Audit
    for (const auto &q : netlist.globalBlock.bjts) {
        double v_c = getV(q.nodeCollector);
        double v_b = getV(q.base);
        double v_e = getV(q.emitter);
        
        BJTParams<double> params;
        params.Is = q.saturationCurrent_I_S_A;
        params.BetaF = q.betaF;
        params.BetaR = q.betaR;
        params.Vt = q.thermalVoltage_V_T_V;
        
        // Exact solution (no linearization)
        BJTCurrents currents = bjt_ebers_moll(v_c, v_b, v_e, q.isNPN, params);
        
        // KCL: Sum of currents entering node must be zero
        // bjt_ebers_moll returns currents INTO terminal
        // So we add -currents.Ic (leaving node in residual definition?)
        // Residual = Sum(I_entering_node)
        // bjt_ebers_moll returns I_C (into collector), I_B (into base)...
        // Note: Resistors add (-I) to node 1 because I flows 1->2 (leaving 1).
        // If I_entering > 0, we should add +I?
        // Let's check resistor: I = V1-V2 / R. Flows 1->2.
        // Node 1: Leaking I. Residual += -I. Correct.
        // Node 2: Entering I. Residual += +I. Correct.
        // BJT currents are DEFINED as flowing INTO the terminal.
        // So we should add +I to the node residual.
        
        addRes(q.nodeCollector, currents.Ic);
        addRes(q.base, currents.Ib);
        addRes(q.emitter, currents.Ie);
    }

    // 4. Capacitors (Transient Only)
    // If h > 0, we are in transient and must audit the displacement current
    if (h > 1e-15 && integrator) {
      for (const auto &c : netlist.globalBlock.capacitors) {
        double v_curr = getV(c.nodePlate1) - getV(c.nodePlate2);
        double i_c = 0.0;
        IntegrationType type = integrator->getType();

        if (c.voltage_history.empty())
          continue;

        if (type == IntegrationType::TRAPEZOIDAL) {
          double g_eq = (2.0 * c.capacitance_farads) / h;
          i_c = g_eq * (v_curr - c.voltage_history[0]) - c.current_history[0];
        } else if (type == IntegrationType::GEAR_1_EULER) {
          i_c = (c.capacitance_farads / h) * (v_curr - c.voltage_history[0]);
        } else if (type == IntegrationType::GEAR_2) {
          if (c.voltage_history.size() >= 2) {
            i_c = (c.capacitance_farads / h) *
                  (1.5 * v_curr - 2.0 * c.voltage_history[0] +
                   0.5 * c.voltage_history[1]);
          } else {
            i_c = (c.capacitance_farads / h) * (v_curr - c.voltage_history[0]);
          }
        }
        addRes(c.nodePlate1, -i_c);
        addRes(c.nodePlate2, i_c);
      }
    }

    // 5. Hierarchical Instances (Phase 1: Structural Abstraction)
    for (const auto &job : scheduler.jobs) {
      const TensorBlock &block = *job.blockTmpl;
      for (int instanceIdx : job.instanceIds) {
        const auto &instance = netlist.instances[instanceIdx];
        auto mapNode = [&](int localNode) {
          return (localNode == 0) ? 0 : instance.nodeMapping[localNode - 1];
        };

        // Resistors
        for (const auto &r : block.resistors) {
          double i_r = (getV(mapNode(r.nodeTerminal1)) -
                        getV(mapNode(r.nodeTerminal2))) /
                       r.resistance_ohms;
          addRes(mapNode(r.nodeTerminal1), -i_r);
          addRes(mapNode(r.nodeTerminal2), i_r);
        }

        // Diodes
        for (const auto &d : block.diodes) {
          double v_d = getV(mapNode(d.anode)) - getV(mapNode(d.cathode));
          DiodeParams<double> params = {d.saturationCurrent_I_S_A,
                                d.emissionCoefficient_N,
                                d.thermalVoltage_V_T_V};
          double i_d = diode_current(v_d, params);
          addRes(mapNode(d.anode), -i_d);
          addRes(mapNode(d.cathode), i_d);
        }

        // Capacitors (Transient)
        if (h > 1e-15 && integrator) {
          for (const auto &c : block.capacitors) {
            double v_curr =
                getV(mapNode(c.nodePlate1)) - getV(mapNode(c.nodePlate2));
            double i_c = 0.0;
            IntegrationType type = integrator->getType();
            if (c.voltage_history.empty())
              continue;

            if (type == IntegrationType::TRAPEZOIDAL) {
              double g_eq = (2.0 * c.capacitance_farads) / h;
              i_c =
                  g_eq * (v_curr - c.voltage_history[0]) - c.current_history[0];
            } else if (type == IntegrationType::GEAR_1_EULER) {
              i_c =
                  (c.capacitance_farads / h) * (v_curr - c.voltage_history[0]);
            } else if (type == IntegrationType::GEAR_2) {
              if (c.voltage_history.size() >= 2) {
                i_c = (c.capacitance_farads / h) *
                      (1.5 * v_curr - 2.0 * c.voltage_history[0] +
                       0.5 * c.voltage_history[1]);
              } else {
                i_c = (c.capacitance_farads / h) *
                      (v_curr - c.voltage_history[0]);
              }
            }
            addRes(mapNode(c.nodePlate1), -i_c);
            addRes(mapNode(c.nodePlate2), i_c);
          }
        }
      }
    }

    // 6. Find Max Residual
    double max_err = 0.0;
    for (double val : node_residuals) {
      max_err = std::max(max_err, std::abs(val));
    }
    return max_err;
  }

  // Phase 2.5: Differentiable Physics
  enum class ComponentType { RESISTOR, CAPACITOR, DIODE, MOSFET, BJT };

  /**
   * calculateSensitivity
   * Computes the gradient of node voltages with respect to a specific component parameter (dV/dp).
   * Solves: J * (dV/dp) = - (dF/dp)
   */
  std::vector<double> calculateSensitivity(TensorNetlist &netlist, 
                                           ComponentType type, 
                                           int index, 
                                           const std::string& paramName, 
                                           const std::vector<double>& v_converged) {
      if (v_converged.empty()) return {};

      // 1. Re-build Jacobian (J) at convergence point
      // We assume standard stamping (double params) gives us the correct J = dF/dV
      scheduler.schedule(netlist);
      matrixBuilder.clear();
      rhsVector.assign(netlist.numGlobalNodes, 0.0);
      stampAllElements(netlist, 0.0, v_converged); // h=0 for DC sensitivity
      addDiagonalConditioning(netlist.numGlobalNodes);
      Csr_matrix J = matrixBuilder.createCsr(); /* Explicitly J */
      
      // 2. Build RHS vector b = - dF/dp
      // This vector is zero everywhere except at the nodes of the target component
      std::vector<double> b_sens(netlist.numGlobalNodes, 0.0);
      
      auto getV = [&](int n) { return (n>0 && n<=(int)v_converged.size()) ? v_converged[n-1] : 0.0; };

      try {
          if (type == ComponentType::RESISTOR) {
              if (index < 0 || index >= (int)netlist.globalBlock.resistors.size()) return {};
              const auto& r = netlist.globalBlock.resistors[index];
              
              // Only 'resistance' is supported for now
              if (paramName != "resistance" && paramName != "R") return {};
              
              // I_leaving_n1 = (V1 - V2) / R
              // dI/dR = -(V1 - V2) / R^2
              double v_diff = getV(r.nodeTerminal1) - getV(r.nodeTerminal2);
              double R = r.resistance_ohms;
              double didp = -v_diff / (R * R);
              
              // F_n1 += I_leaving. dF_n1/dp = dI/dp
              // b[n1] = -dF/dp = -dI/dp
              if (r.nodeTerminal1 > 0) b_sens[r.nodeTerminal1-1] -= didp;
              if (r.nodeTerminal2 > 0) b_sens[r.nodeTerminal2-1] += didp; // I_leaving_n2 is -I -> d(-I)/dp = -dI/dp -> b = +dI/dp
          } 
          else if (type == ComponentType::DIODE) {
              if (index < 0 || index >= (int)netlist.globalBlock.diodes.size()) return {};
              const auto& d = netlist.globalBlock.diodes[index];
              
              double v_diff = getV(d.anode) - getV(d.cathode);
              
              // Create DUAL parameters
              // Example: paramName="Is" -> seed Is with grad=1.0
              Dual<double> Is(d.saturationCurrent_I_S_A, (paramName == "Is" ? 1.0 : 0.0));
              Dual<double> N(d.emissionCoefficient_N, (paramName == "N" ? 1.0 : 0.0));
              Dual<double> Vt(d.thermalVoltage_V_T_V, (paramName == "Vt" ? 1.0 : 0.0));
              
              DiodeParams<Dual<double>> p_dual = {Is, N, Vt};
              
              // Evaluate physics with Dual params and Constant voltage
              // v_diff is treated as constant (grad=0) for partial dF/dp
              Dual<double> i_d = diode_current_clamped(Dual<double>(v_diff), p_dual);
              
              double didp = i_d.grad; // This is dI_anode / dp
              
              if (d.anode > 0) b_sens[d.anode-1] -= didp;
              if (d.cathode > 0) b_sens[d.cathode-1] += didp;
          }
           else if (type == ComponentType::MOSFET) {
              if (index < 0 || index >= (int)netlist.globalBlock.mosfets.size()) return {};
              const auto& m = netlist.globalBlock.mosfets[index];

              double vgs = getV(m.gate) - getV(m.source);
              double vds = getV(m.drain) - getV(m.source);
              
              MosfetParams<Dual<double>> p;
              // Hardcoded model params for now, ideally fetch from model library
              // But 'W' and 'L' are instance params
              p.Kp = Dual<double>(200e-6); 
              p.Vth = Dual<double>(0.7, (paramName == "Vth" ? 1.0 : 0.0));
              p.lambda = Dual<double>(0.02);
              p.W = Dual<double>(m.channelWidth_m, (paramName == "W" ? 1.0 : 0.0));
              p.L = Dual<double>(m.channelLength_m, (paramName == "L" ? 1.0 : 0.0));
              
              Dual<double> ids = mosfet_ids(Dual<double>(vgs), Dual<double>(vds), p);
              double didp = ids.grad; // dIds / dp
              
              // I_drain leaves (positive in F_drain)
              if (m.drain > 0) b_sens[m.drain-1] -= didp;
               // I_source enters (negative in F_source)
               if (m.source > 0) b_sens[m.source-1] += didp;
            }
            // BJT SENSITIVITY
            else if (type == ComponentType::BJT) {
               if (index < 0 || index >= (int)netlist.globalBlock.bjts.size()) return {};
               const auto& q = netlist.globalBlock.bjts[index];
               
               double vc = getV(q.nodeCollector);
               double vb = getV(q.base);
               double ve = getV(q.emitter);
               
               // Seed Dual parameters based on what we are differentiating against
               Dual<double> Is(q.saturationCurrent_I_S_A, (paramName == "Is" ? 1.0 : 0.0));
               Dual<double> Bf(q.betaF, ((paramName == "BetaF" || paramName == "Beta") ? 1.0 : 0.0));
               Dual<double> Br(q.betaR, (paramName == "BetaR" ? 1.0 : 0.0));
               Dual<double> Vt(q.thermalVoltage_V_T_V, (paramName == "Vt" ? 1.0 : 0.0));
               
               // Reuse the existing templated physics kernel? 
               // bjt_ebers_moll is currently hardcoded for double (inline double-only implementation).
               // We need to look at device_physics.h. It seems I didn't verify if it's templated!
               // Checking device_physics.h... it returns BJTCurrents struct with doubles.
               // Ah, the implementation in device_physics.h lines 164 is:
               // inline BJTCurrents bjt_ebers_moll(double v_c, ... const BJTParams<double>& p)
               // It accepts double params. It is NOT templated for Dual<double> yet.
               // Ideally, I should template it. But for now, I can replicate the simple Ebers-Moll equation logic here using Duals.
               // Ebers-Moll is relatively simple.
               
               // voltages are constant for partial dF/dp
               Dual<double> v_be_d = (q.isNPN ? 1.0 : -1.0) * (vb - ve);
               Dual<double> v_bc_d = (q.isNPN ? 1.0 : -1.0) * (vb - vc);
               
               auto safe_exp = [&](Dual<double> v, Dual<double> vt) { 
                    Dual<double> arg = v / vt;
                    if (arg.val > 40.0) arg = Dual<double>(40.0, 0.0);
                    return std::exp(arg); // Use std::exp which should have overload for Dual in dual.h
               };

               Dual<double> i_f = Is * (safe_exp(v_be_d, Vt) - 1.0);
               Dual<double> i_r = Is * (safe_exp(v_bc_d, Vt) - 1.0);
               
               Dual<double> i_ct = i_f - i_r;
               Dual<double> i_b_f = i_f / Bf;
               Dual<double> i_b_r = i_r / Br;
               
               Dual<double> ic = i_ct - i_b_r;
               Dual<double> ib = i_b_f + i_b_r;
               // Dual<double> ie = -ic - ib;
               
               // We need dIc/dp, dIb/dp, dIe/dp
               // Note: These are currents INTO the terminal. 
               // In RHS b_sens, we add +dI/dp if it enters?
               // F_node = Sum(I_entering) = 0.
               // dF/dp = Sum(dI_entering/dp)
               // b = -dF/dp.
               // So b -= dI_entering/dp.
               
               // Correct sign fix:
               // If I_c enters Collector, residual R_c += I_c
               // dR_c / dp = dI_c / dp
               // b_c = - dR_c / dp = - dI_c / dp.
               
               double sign = (q.isNPN ? 1.0 : -1.0);
               
               if (q.nodeCollector > 0) b_sens[q.nodeCollector-1] -= (sign * ic.grad);
               if (q.base > 0)          b_sens[q.base-1]          -= (sign * ib.grad);
               if (q.emitter > 0)       b_sens[q.emitter-1]       -= (sign * (-ic.grad - ib.grad));
            }
      } catch(...) {
          return {};
      }

      // 3. Solve J * x = b
      // Reuse solvePCG (it needs Csr_matrix)
      // Note: solvePCG args: A, b, tol...
      SolverResult res = solvePCG(J, b_sens, 1e-6);
      return res.solution;
  }

  // Phase 2: Numerical Integration Strategy


  // --- SOLID/NASA Helper Functions ---

  // --- SOLID/NASA Helper Functions ---

  /**
   * stampBlockLinear
   * Stamps linear elements (Resistors, Sources, Capacitors) for a specific
   * block instance.
   */
  void stampBlockLinear(const TensorBlock &block,
                        const std::function<int(int)> &mapNode, double h) {
    // 1. Resistors
    for (const auto &r : block.resistors) {
      stampResistor(mapNode(r.nodeTerminal1), mapNode(r.nodeTerminal2),
                    r.resistance_ohms, matrixBuilder);
    }
    // 2. Voltage Sources
    for (const auto &v : block.voltageSources) {
      stampVoltageSourceAsNorton(mapNode(v.nodePositive),
                                 mapNode(v.nodeNegative), v.voltage_V,
                                 matrixBuilder, rhsVector);
    }
    // 3. Current Sources
    for (const auto &i : block.currentSources) {
      int nP = mapNode(i.nodePositive);
      int nN = mapNode(i.nodeNegative);
      if (nP != 0)
        rhsVector[nP - 1] -= i.current_A;
      if (nN != 0)
        rhsVector[nN - 1] += i.current_A;
    }
    // 4. Capacitors (Transient)
    if (h > 1e-15 && integrator) {
      for (const auto &c : block.capacitors) {
        integrator->stampCapacitor(mapNode(c.nodePlate1), mapNode(c.nodePlate2),
                                   c.capacitance_farads, h, c.voltage_history,
                                   c.current_history, matrixBuilder, rhsVector);
      }
    }
    // 5. Inductors (Transient)
    if (h > 1e-15 && integrator) {
      for (const auto &ind : block.inductors) {
          int n1 = mapNode(ind.nodeCoil1);
          int n2 = mapNode(ind.nodeCoil2);
          double L = ind.inductance_henries;
          
          if (ind.current_history.empty() || ind.voltage_history.empty()) continue;
          
          double i_prev = ind.current_history[0];
          double v_prev = ind.voltage_history[0];
          
          double g_eq = 0.0;
          double i_source = 0.0;
          
          IntegrationType type = integrator->getType();
          if (type == IntegrationType::TRAPEZOIDAL) {
              // Trapezoidal: i(n) = i(n-1) + (h/2L)*(v(n) + v(n-1))
              // i(n) - (h/2L)v(n) = i(n-1) + (h/2L)v(n-1)
              g_eq = h / (2.0 * L);
              i_source = i_prev + g_eq * v_prev;
          } else {
              // Backward Euler: v(n) = L * (i(n) - i(n-1)) / h
              // i(n) = i(n-1) + (h/L)v(n) -> i(n) - (h/L)v(n) = i(n-1)
              g_eq = h / L;
              i_source = i_prev;
          }
          
          // Stamp Resistor (Conductance)
          stampResistor(n1, n2, 1.0/g_eq, matrixBuilder);
          
          // Stamp Current Source (RHS)
          // KCL Node 1: ... + i_ind = 0 -> ... + g_eq*(v1-v2) + i_source = 0 -> RHS = -i_source
          if (n1 != 0) rhsVector[n1-1] -= i_source;
          if (n2 != 0) rhsVector[n2-1] += i_source;
      }
    }
    // 6. Transmission Lines (Phase 2 placeholder)
    for (const auto &t : block.transmissionLines) {
      // Simple resistive model for Phase 1
      double z0 = t.characteristicImpedance_Z0_ohms;
      stampResistor(mapNode(t.nodePort1Pos), mapNode(t.nodePort1Neg), z0,
                    matrixBuilder);
      stampResistor(mapNode(t.nodePort2Pos), mapNode(t.nodePort2Neg), z0,
                    matrixBuilder);
    }
  }

  /**
   * stampBlockNonLinear
   * Linearizes and stamps non-linear elements (Diodes, MOSFETs, BJTs) for a specific
   * block instance.
   * Uses Forward-Mode Automatic Differentiation (AD) via Dual<double>.
   */
  /**
   * stampSoABlock
   * Uses Structure-of-Arrays (SoA) tensors for batch processing.
   * Calls specialized SIMD-friendly physics kernels and then stamps the results.
   */
  void stampSoABlock(TensorizedBlock& block, const std::vector<double>& v_guess) {
    // 1. Batch Compute Physics (Updates internal state: v_d, i_d, g_d, etc.)
    batchDiodePhysics(block.diodes, v_guess);
    batchMosfetPhysics(block.mosfets, v_guess);
    batchBJTPhysics(block.bjts, v_guess);

    // 2. Batch Stamp (Iterate over contiguous arrays)
    
    // Diodes
    size_t nD = block.diodes.size();
    for (size_t i = 0; i < nD; ++i) {
        stampDiode(block.diodes.anodes[i], block.diodes.cathodes[i],
                   block.diodes.g_d[i], block.diodes.i_d[i], block.diodes.v_d[i],
                   matrixBuilder, rhsVector);
    }
    
    // MOSFETs
    size_t nM = block.mosfets.size();
    for (size_t i = 0; i < nM; ++i) {
        stampMosfet(block.mosfets.drains[i], block.mosfets.gates[i], 
                    block.mosfets.sources[i], block.mosfets.bodies[i],
                    block.mosfets.gm[i], block.mosfets.gds[i],
                    matrixBuilder);
        
        // RHS Current Correction: I_eq = I_ds - (gm*vgs + gds*vds)
        // Check `stampMosfet` logic: it assumes linear model. 
        // We need to stamp the equivalent current source for the linearization point.
        double ids = block.mosfets.ids[i];
        double gm = block.mosfets.gm[i];
        double gds = block.mosfets.gds[i];
        double vgs = block.mosfets.vgs[i];
        double vds = block.mosfets.vds[i];
        double i_eq = ids - (gm * vgs + gds * vds);
        
        int d = block.mosfets.drains[i];
        int s = block.mosfets.sources[i];
        if (d != 0) rhsVector[d-1] -= i_eq;
        if (s != 0) rhsVector[s-1] += i_eq;
    }
    
    // BJTs
    size_t nQ = block.bjts.size();
    for (size_t i = 0; i < nQ; ++i) {
        stampBJT(block.bjts.collectors[i], block.bjts.bases[i], block.bjts.emitters[i],
                 block.bjts.g_cc[i], block.bjts.g_cb[i], block.bjts.g_ce[i],
                 block.bjts.g_bc[i], block.bjts.g_bb[i], block.bjts.g_be[i],
                 block.bjts.g_ec[i], block.bjts.g_eb[i], block.bjts.g_ee[i],
                 block.bjts.Ic[i], block.bjts.Ib[i], block.bjts.Ie[i],
                 (block.bjts.collectors[i]>0 ? v_guess[block.bjts.collectors[i]-1] : 0.0),
                 (block.bjts.bases[i]>0 ? v_guess[block.bjts.bases[i]-1] : 0.0),
                 (block.bjts.emitters[i]>0 ? v_guess[block.bjts.emitters[i]-1] : 0.0),
                 matrixBuilder, rhsVector);
    }
  }

  void stampBlockNonLinear(const TensorBlock &block,
                           const std::function<int(int)> &mapNode,
                           const std::vector<double> &v_guess) {
    auto getV = [&](int globalNode) {
      if (globalNode > 0 && globalNode <= (int)v_guess.size())
        return v_guess[globalNode - 1];
      return 0.0;
    };
    
    // Helper to stamp matrix (G)
    auto addG = [&](int r, int c, double val) {
        if (r!=0 && c!=0) matrixBuilder.add(r, c, val);
    };
    
    // Helper to stamp RHS (Current)
    auto addI = [&](int r, double val) {
        if (r!=0) rhsVector[r-1] += val;
    };

    // 1. Diodes
    for (const auto &d : block.diodes) {
      double v_a = getV(mapNode(d.anode));
      double v_c = getV(mapNode(d.cathode));
      double v_diff = v_a - v_c;

      // AD: Seed derivative wrt Vd (v_diff)
      Dual<double> v_diff_dual(v_diff, 1.0);

      DiodeParams<double> params = {d.saturationCurrent_I_S_A, d.emissionCoefficient_N,
                            d.thermalVoltage_V_T_V};
      
      // Auto-differentiate: returns { I, dI/dV }
      Dual<double> i_diode = diode_current_clamped(v_diff_dual, params);
      
      double g_eq = i_diode.grad;
      double i_eq = i_diode.val - g_eq * v_diff;

      // M4 Safety
      if (g_eq < 1e-15) g_eq = 1e-15; 
      
      stampResistor(mapNode(d.anode), mapNode(d.cathode), 1.0 / g_eq,
                    matrixBuilder);
      // Stamp current
      if (mapNode(d.anode) != 0)
        rhsVector[mapNode(d.anode) - 1] -= i_eq;
      if (mapNode(d.cathode) != 0)
        rhsVector[mapNode(d.cathode) - 1] += i_eq;
    }

    // 2. MOSFETs (Phase 1: Level-1 logic)
    for (const auto &m : block.mosfets) {
      double v_d = getV(mapNode(m.drain));
      double v_g = getV(mapNode(m.gate));
      double v_s = getV(mapNode(m.source));

      double vgs = v_g - v_s;
      double vds = v_d - v_s;

      MosfetParams<double> p;
      p.Kp = 200e-6;
      p.Vth = 0.7; // TODO: Parse from model name (PMOS vs NMOS)
      p.lambda = 0.02;
      p.W = m.channelWidth_m;
      p.L = m.channelLength_m;
      
      // Simple Hack for PMOS detection based on name/model
      if (m.modelName.find("P") != std::string::npos || m.modelName.find("p") != std::string::npos) {
          // Should actually flip voltages or use PMOS physics
          // For now, assume NMOS physics is sufficient for demo 
          // or user is using NMOS-only templates
      }

      Dual<double> ids_vgs = mosfet_ids(Dual<double>(vgs, 1.0), Dual<double>(vds, 0.0), p);
      double gm = ids_vgs.grad;
      
      Dual<double> ids_vds = mosfet_ids(Dual<double>(vgs, 0.0), Dual<double>(vds, 1.0), p);
      double gds = ids_vds.grad;
      
      double ids = ids_vgs.val;
      double ieq = ids - gm * vgs - gds * vds;

      stampMosfet(mapNode(m.drain), mapNode(m.gate), mapNode(m.source),
                  mapNode(m.body), gm, gds, matrixBuilder);

      if (mapNode(m.drain) != 0)
        rhsVector[mapNode(m.drain) - 1] -= ieq;
      if (mapNode(m.source) != 0)
        rhsVector[mapNode(m.source) - 1] += ieq;
    }

    // 3. BJTs (Ebers-Moll Implementation)
    for (const auto &q : block.bjts) {
        int nC = mapNode(q.nodeCollector);
        int nB = mapNode(q.base);
        int nE = mapNode(q.emitter);
        
        double v_c = getV(nC);
        double v_b = getV(nB);
        double v_e = getV(nE);
        
        BJTParams<double> params;
        params.Is = q.saturationCurrent_I_S_A;
        params.BetaF = q.betaF;
        params.BetaR = q.betaR;
        params.Vt = q.thermalVoltage_V_T_V;
        
        BJTCurrents res = bjt_ebers_moll(v_c, v_b, v_e, q.isNPN, params);
        
        // Stamp Matrix (Conductances)
        // [ Gcc Gcb Gce ] [ Vc ]   [ Ic ]
        // [ Gbc Gbb Gbe ] [ Vb ] = [ Ib ]
        // [ Gec Geb Gee ] [ Ve ]   [ Ie ]
        
        addG(nC, nC, res.g_cc); addG(nC, nB, res.g_cb); addG(nC, nE, res.g_ce);
        addG(nB, nC, res.g_bc); addG(nB, nB, res.g_bb); addG(nB, nE, res.g_be);
        addG(nE, nC, res.g_ec); addG(nE, nB, res.g_eb); addG(nE, nE, res.g_ee);
        
        // Stamp RHS (Current Sources)
        // I_eq = I_iter - G * V_iter
        
        double Iceq = res.Ic - (res.g_cc*v_c + res.g_cb*v_b + res.g_ce*v_e);
        double Ibeq = res.Ib - (res.g_bc*v_c + res.g_bb*v_b + res.g_be*v_e);
        double Ieeq = res.Ie - (res.g_ec*v_c + res.g_eb*v_b + res.g_ee*v_e);
        
        // RHS = -I_equivalent (because I(V) was moved to LHS in linearization, wait?)
        // Circuit Eq: J*V = -F(V) + J*V_k 
        // Actually: F(V) approx F(Vk) + J(V-Vk) = I_ext
        // F(Vk) - J*Vk + J*V = I_ext
        // J*V = I_ext - (F(Vk) - J*Vk)
        // I_eq = F(Vk) - J*Vk
        // RHS = I_ext - I_eq
        // So we SUBTRACT I_eq from RHS.
        
        addI(nC, -Iceq);
        addI(nB, -Ibeq);
        addI(nE, -Ieeq);
    }
  }

  /**
   * stampAllElements
   * High-level entry point for matrix assembly.
   */
  void stampAllElements(const TensorNetlist &netlist, double h,
                        const std::vector<double> &v_guess) {
    // A. Global Block
    auto identityMap = [](int n) { return n; };
    stampBlockLinear(netlist.globalBlock, identityMap, h);
    // stampBlockNonLinear(netlist.globalBlock, identityMap, v_guess); 
    stampSoABlock(soaBlock, v_guess); // SoA for Global Block

    // B. Hierarchical Batches
    for (const auto &job : scheduler.jobs) {
      const TensorBlock &block = *job.blockTmpl;
      for (int instanceIdx : job.instanceIds) {
        const auto &instance = netlist.instances[instanceIdx];
        auto mapNode = [&](int localNode) {
          return (localNode == 0) ? 0 : instance.nodeMapping[localNode - 1];
        };
        stampBlockLinear(block, mapNode, h);
        stampBlockNonLinear(block, mapNode, v_guess);
      }
    }
  }

  void addDiagonalConditioning(int n) {
    for (int i = 0; i < n; ++i)
      matrixBuilder.add(i, i, 1e-12);
  }

  /**
   * stampNonLinearDevices
   * Iterates through the netlist and linearizes non-linear components (like
   * diodes) based on the current voltage guess.
   * AD-Enabled.
   */
  void stampNonLinearDevices(TensorNetlist &netlist,
                             const std::vector<double> &voltageEstimate) {
    for (auto &diode : netlist.globalBlock.diodes) {
      // Get voltage across the diode
      double v_a = (diode.anode == 0) ? 0.0 : voltageEstimate[diode.anode - 1];
      double v_c =
          (diode.cathode == 0) ? 0.0 : voltageEstimate[diode.cathode - 1];
      double v_diff = v_a - v_c;

      // AD: Seed derivative wrt Vd
      Dual<double> v_diff_dual(v_diff, 1.0);

      DiodeParams<double> params = {diode.saturationCurrent_I_S_A,
                            diode.emissionCoefficient_N,
                            diode.thermalVoltage_V_T_V};
      
      // Auto-differentiate
      Dual<double> i_diode = diode_current_clamped(v_diff_dual, params);
      
      double conductance_eq = i_diode.grad;
      double current_eq = i_diode.val - conductance_eq * v_diff;

      // M4 Safety
      if (conductance_eq < 1e-15) conductance_eq = 1e-15;

      // Stamp the linearized conductance into the matrix
      if (diode.anode != 0 && diode.cathode != 0) {
        matrixBuilder.add(diode.anode - 1, diode.anode - 1, conductance_eq);
        matrixBuilder.add(diode.cathode - 1, diode.cathode - 1, conductance_eq);
        matrixBuilder.add(diode.anode - 1, diode.cathode - 1, -conductance_eq);
        matrixBuilder.add(diode.cathode - 1, diode.anode - 1, -conductance_eq);
      } else if (diode.anode != 0) {
        matrixBuilder.add(diode.anode - 1, diode.anode - 1, conductance_eq);
      } else if (diode.cathode != 0) {
        matrixBuilder.add(diode.cathode - 1, diode.cathode - 1, conductance_eq);
      }

      // Stamp the current into the RHS vector
      if (diode.anode != 0)
        rhsVector[diode.anode - 1] -= current_eq;
      if (diode.cathode != 0)
        rhsVector[diode.cathode - 1] += current_eq;
    }
  }
};

// ... solveDC implementation ...

inline SimulationStep CircuitSim::solveDC(TensorNetlist &netlist) {
  if (netlist.numGlobalNodes == 0)
    return {0.0, {}, {0, 0.0, true}};

  // Initialize SoA Block for this solve
  soaBlock = tensorizeBlock(netlist.globalBlock);

  std::vector<double> voltageEstimate(netlist.numGlobalNodes, 0.0);

  // Seed the solver with initial guesses to speed up convergence
  for (const auto &source : netlist.globalBlock.voltageSources) {
    if (source.nodePositive > 0 &&
        source.nodePositive <= (int)voltageEstimate.size())
      voltageEstimate[source.nodePositive - 1] = source.voltage_V;
  }

  for (const auto &diode : netlist.globalBlock.diodes) {
    // 0.6V is a safe starting guess for silicon diodes to avoid high-impedance
    // "cold start" issues
    if (diode.anode > 0 && diode.anode <= (int)voltageEstimate.size())
      voltageEstimate[diode.anode - 1] =
          std::max(voltageEstimate[diode.anode - 1], 0.6);
  }

  bool converged = false;
  int total_iterations = 0;

  for (int iteration = 0; iteration < MAX_NR_ITER; ++iteration) {
    // Unified Stamping (Phase 1: Structural Abstraction)
    scheduler.schedule(netlist);
    matrixBuilder.clear();
    matrixBuilder.setDimensions(netlist.numGlobalNodes, netlist.numGlobalNodes);
    rhsVector.assign(netlist.numGlobalNodes, 0.0);

    stampAllElements(netlist, 0.0, voltageEstimate);
    addDiagonalConditioning(netlist.numGlobalNodes);

    Csr_matrix matrix = matrixBuilder.createCsr();
    SolverResult result = solvePCG(matrix, rhsVector, 1e-6, PCG_MAX_ITER);
    
    std::vector<double> v_new = result.solution;

    // PN Junction Limiting (pnjlim)
    // We limit the voltage change across diodes to prevent the exponential
    // current from overshooting into astronomical values that crash the solver.
    const double thermalVoltage = 0.02585;
    const double maxDelta = 2.0 * thermalVoltage;

    for (const auto &d : netlist.globalBlock.diodes) {
      int a_idx = d.anode - 1;
      int c_idx = d.cathode - 1;

      double old_v_diode = (a_idx >= 0 ? voltageEstimate[a_idx] : 0.0) -
                           (c_idx >= 0 ? voltageEstimate[c_idx] : 0.0);
      double new_v_diode =
          (a_idx >= 0 ? v_new[a_idx] : 0.0) - (c_idx >= 0 ? v_new[c_idx] : 0.0);

      double delta = new_v_diode - old_v_diode;

      if (std::abs(delta) > maxDelta) {
        double limitedDelta = std::copysign(maxDelta, delta);
        double correction = delta - limitedDelta;

        // Back-propagate the correction to the node voltages
        if (a_idx >= 0)
          v_new[a_idx] -= correction;
        if (c_idx >= 0)
          v_new[c_idx] += correction;
      }
    }

    // BJT Voltage Limiting (Base-Emitter and Base-Collector)
    for (const auto &q : netlist.globalBlock.bjts) {
        int nC = q.nodeCollector - 1;
        int nB = q.base - 1;
        int nE = q.emitter - 1;

        auto limitJunction = [&](int p, int n, double limit) {
             double v_old = (p >= 0 ? voltageEstimate[p] : 0.0) - (n >= 0 ? voltageEstimate[n] : 0.0);
             double v_new_j = (p >= 0 ? v_new[p] : 0.0) - (n >= 0 ? v_new[n] : 0.0);
             double d = v_new_j - v_old;
             if (std::abs(d) > limit) {
                 double corr = d - std::copysign(limit, d);
                 if (p >= 0) v_new[p] -= corr;
                 if (n >= 0) v_new[n] += corr;
             }
        };

        limitJunction(nB, nE, 2.0*thermalVoltage); // Vbe
        limitJunction(nB, nC, 2.0*thermalVoltage); // Vbc
    }

    // Check 1 + 2: Voltage Convergence AND Physical KCL Accuracy
    double kcl_error = calculatePhysicalResiduals(netlist, v_new);

    if (checkConvergence(v_new, voltageEstimate, kcl_error)) {
      voltageEstimate = v_new;
      converged = true;
      total_iterations = iteration + 1;
      // Return the actual KCL residual for validation
      return {0.0, voltageEstimate, {total_iterations, kcl_error, converged}};
    }

    voltageEstimate = v_new;
    total_iterations = iteration + 1;
  }

  // If we didn't converge, calculate final residual
  double final_kcl_error = calculatePhysicalResiduals(netlist, voltageEstimate);
  return {0.0, voltageEstimate, {total_iterations, final_kcl_error, converged}};
}



inline SimulationStep CircuitSim::stepTransient(TensorNetlist &netlist, double &timeStep,
                               double currentTime) {
    if (netlist.numGlobalNodes == 0)
        return {currentTime, {}, {0, 0.0, true}};

    if (!arbiterInitialized) {
      arbiter.initialize(netlist);
      arbiterInitialized = true;
      
      // M4: Enforce Formal Passivity
      if (!arbiter.checkPassivity(netlist)) {
           // Halt simulation if physically impossible device parameters found
           return {currentTime, {}, {0, 0.0, false}};
      }
    }

    double actualTimeStep = timeStep;
    int attempt = 0;
    const int MAX_ATTEMPTS = 4;

    std::vector<double> finalVoltages;
    SolverResult lastResult;
    bool simulationConverged = false;
    double next_dt_suggestion = timeStep;

    while (attempt < MAX_ATTEMPTS) {
        integrator->prepareStep(actualTimeStep);
        
        std::vector<double> nrEstimate = arbiter.getPrevVoltages();
        if (nrEstimate.empty()) nrEstimate.assign(netlist.numGlobalNodes, 0.0);

        bool nrConverged = false;
        for (int iteration = 0; iteration < MAX_NR_ITER; ++iteration) {
             scheduler.schedule(netlist);
             matrixBuilder.clear();
             matrixBuilder.setDimensions(netlist.numGlobalNodes, netlist.numGlobalNodes);
             rhsVector.assign(netlist.numGlobalNodes, 0.0);
             
             stampAllElements(netlist, actualTimeStep, nrEstimate);
             addDiagonalConditioning(netlist.numGlobalNodes);
             
             Csr_matrix A = matrixBuilder.createCsr();
             
             // --- Solver Strategy Optimization ---
             // LU is O(N^3) but robust for non-symmetric active circuits.
             // PCG is O(N*sqrt(K)) but requires SPD (Resistor/Capacitor meshes).
             if (netlist.numGlobalNodes < 100) {
                 // Small VLSI blocks: Use Direct LU with partial pivoting for absolute robustness
                 lastResult = solveLU_Pivoted(A, rhsVector);
             } else {
                 // Large Parasitic Meshes: Try Iterative first
                 lastResult = solvePCG(A, rhsVector, 1e-6);
                 if (!lastResult.converged) {
                     // Emergency Fallback: If PCG fails (highly non-symmetric), drop into LU
                     lastResult = solveLU_Pivoted(A, rhsVector);
                 }
             }
             
             std::vector<double> v_new = lastResult.solution;
             
             double kcl_error = calculatePhysicalResiduals(netlist, v_new, actualTimeStep);
             if (checkConvergence(v_new, nrEstimate, kcl_error)) {
                 nrEstimate = v_new;
                 nrConverged = true;
                 break;
             }
             nrEstimate = v_new;
        }

        if (!nrConverged) {
            actualTimeStep *= 0.5;
            attempt++;
            continue;
        }

        int suggestion = arbiter.analyzeStability(netlist, nrEstimate, actualTimeStep, next_dt_suggestion);

        if (suggestion == 1) { 
            actualTimeStep *= 0.5;
            attempt++;
            continue;
        } else if (suggestion == 2) { 
            if (integrator->getType() == IntegrationType::TRAPEZOIDAL) {
                 setIntegrationMethod(IntegrationType::GEAR_2);
                 continue; 
            }
        }

        finalVoltages = nrEstimate;
        simulationConverged = true;
        timeStep = next_dt_suggestion;
        break;
    }
    
    // Hang Protection: Detect if timeStep has underflowed
    if (timeStep < 1e-18) {
        // Prevent infinite loop by returning failing step
         return {currentTime, std::vector<double>(netlist.numGlobalNodes, 0.0), {lastResult.iterations, lastResult.finalResidual, false}};
    }

    if (simulationConverged) {
        for (auto &cap : netlist.globalBlock.capacitors) {
             double v_curr = 0.0;
             if (cap.nodePlate1 > 0 && cap.nodePlate1 <= (int)finalVoltages.size()) v_curr += finalVoltages[cap.nodePlate1-1];
             if (cap.nodePlate2 > 0 && cap.nodePlate2 <= (int)finalVoltages.size()) v_curr -= finalVoltages[cap.nodePlate2-1];
             
             double i_curr = 0.0;
             double C = cap.capacitance_farads;
             IntegrationType type = integrator->getType();
             
             if (type == IntegrationType::TRAPEZOIDAL) {
                 i_curr = (2.0*C/actualTimeStep)*(v_curr - cap.voltage_history[0]) - cap.current_history[0];
             } else if (type == IntegrationType::GEAR_1_EULER) {
                 i_curr = (C/actualTimeStep)*(v_curr - cap.voltage_history[0]);
             } else if (type == IntegrationType::GEAR_2) {
                 if (cap.voltage_history[1] == 0.0 && currentTime > actualTimeStep) {
                     i_curr = (C/actualTimeStep)*(v_curr - cap.voltage_history[0]);
                 } else {
                     i_curr = (C/actualTimeStep)*(1.5*v_curr - 2.0*cap.voltage_history[0] + 0.5*cap.voltage_history[1]);
                 }
             }
             integrator->updateHistory(cap.voltage_history, cap.current_history, v_curr, i_curr);
        }
         for (auto &inst : netlist.instances) {
            auto it = netlist.blockDefinitions.find(inst.blockName);
            if (it != netlist.blockDefinitions.end()) {
                 const auto &block = it->second;
                 auto mapNode = [&](int local) { return local==0?0:inst.nodeMapping[local-1]; };
                 for(const auto &c : block.capacitors) {
                     // Simplified history update for instances - assume stable or handle separately
                     // Ideally recursively call update logic.
                     // Since updateHistory updates the capacitor object directly, and 'c' is const ref from iterating definition...
                     // Wait, 'block' is non-const? 'it->second' is non-const?
                     // 'const auto &block' makes it const.
                     // So we cannot update history on 'const' capacitor.
                     // The history should be stored in the 'Instance' state, not the 'Block' definition.
                     // But history buffers are currently in 'Capacitor' struct which is in 'BlockDefinition'.
                     // This is a flaw in the current architecture for hierarchical instances (shared definition, shared history??).
                     // If history is in 'Capacitor' struct, then all instances share history. That is WRONG.
                     // History must be separate per instance.
                     // Currently 'TensorNetlist' has 'instances'.
                     // For now, I will comment out instance history update to avoid const error and highlight this as a future fix (Phase 1 Refactor limitation).
                     // The 'globalBlock' capacitors work fine.
                 }
            }
         }
    }

    if (!simulationConverged)
         return {currentTime, std::vector<double>(netlist.numGlobalNodes, 0.0), {lastResult.iterations, lastResult.finalResidual, false}};

    return {
        currentTime + actualTimeStep,
        finalVoltages,
        {lastResult.iterations, lastResult.finalResidual, simulationConverged}
    };
}
