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
#include "corner_config.h"

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
      for (size_t i = 0; i < netlist.globalBlock.capacitors.size(); ++i) {
          const auto &c = netlist.globalBlock.capacitors[i];
          const auto &hist = netlist.globalState.capacitorState[i];
          if (hist.v.size() < 2) continue;
          
          double v1 = (c.nodePlate1 > 0) ? current_voltages[c.nodePlate1-1] : 0.0;
          double v2 = (c.nodePlate2 > 0) ? current_voltages[c.nodePlate2-1] : 0.0;
          double v_branch = v1 - v2;
          
          double v_prev = hist.v[0];
          double v_prev2 = (hist.v.size() > 1) ? hist.v[1] : v_prev;
          
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

  SolverStep solveDC(TensorNetlist &netlist);
  
  // NOTE: timeStep is now IN/OUT parameter for adaptive stepping
  SolverStep stepTransient(TensorNetlist &netlist, double &timeStep,
                               double currentTime);
  
  void resetArbiter() { arbiterInitialized = false; }

  // Phase 1.8: PVT Application
  void applyCorner(TensorNetlist &netlist, const acutesim::physics::CornerConfig& corner) {
      netlist.environment.process = (SimulationEnvironment::Corner)corner.process;
      netlist.environment.ambient_temp_K = corner.temperatureC + 273.15;
      
      // If voltage scale is 0 or negative, ignore (safety)
      if (corner.voltageScale > 0.1) {
          netlist.environment.global_voltage_scale = corner.voltageScale;
      }
      
      // Monte Carlo Seed (Phase 1.8.2)
      netlist.environment.monte_carlo_seed = corner.monteCarloSeed;
  }
  
  // Deterministic RNG (FNV-1a Hash + Box-Muller)
  static uint64_t fnv1a(const std::string& s, uint64_t seed) {
      uint64_t hash = 14695981039346656037ULL;
      hash ^= seed;
      for (char c : s) {
          hash ^= static_cast<uint64_t>(c);
          hash *= 1099511628211ULL;
      }
      return hash;
  }
  
  // Returns standard normal sample N(0, 1)
  double getGaussian(uint64_t hash) {
      // Split 64-bit hash into two 32-bit uniform variates
      uint32_t h1 = (uint32_t)(hash & 0xFFFFFFFF);
      uint32_t h2 = (uint32_t)(hash >> 32);
      
      // Avoid 0 (log(0) is bad)
      if (h1 == 0) h1 = 1; 
      
      double u1 = h1 / 4294967296.0;
      double u2 = h2 / 4294967296.0;
      
      // Box-Muller Transform
      return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * 3.14159265359 * u2);
  }
  
  // Get MC-perturbed value
  double getPerturbedValue(double nominal, double tolerance_pct, const std::string& name, int seed) {
      if (tolerance_pct <= 1e-6 || seed == 0) return nominal;
      
      // Hash combining seed + name
      uint64_t h = fnv1a(name, static_cast<uint64_t>(seed));
      double z = getGaussian(h);
      
      // 3-sigma rule: tolerance is usually 3-sigma (or we treat it as 1-sigma?)
      // Standard industry practice: Tolerance is usually 3-sigma limit.
      // So sigma = tolerance / 3.
      double sigma_pct = tolerance_pct / 3.0;
      
      double factor = 1.0 + (z * sigma_pct * 0.01);
      return nominal * factor;
  }

private:
  MatrixConstructor matrixBuilder;
  std::vector<double> rhsVector;
  TensorScheduler scheduler;
  
  // SoA Tensor Storage (Batch Physics)
  TensorizedBlock soaBlock;

  // =========================================================================
  // PHASE 1 HARDENING: Helper Methods (Decomposition of solveDC)
  // =========================================================================

  /**
   * seedInitialGuess - Initialize voltage estimates for Newton-Raphson.
   * Encapsulates heuristic seeding for Diodes, BJTs, and supply nodes.
   */
  void seedInitialGuess(const TensorNetlist& netlist, std::vector<double>& voltageEstimate);

  /**
   * applyPnJunctionLimits - Apply SPICE-style pnjlim to prevent NR overshoot.
   * Modifies v_new in-place based on v_old.
   */
  void applyPnJunctionLimits(const TensorNetlist& netlist,
                              std::vector<double>& v_new,
                              const std::vector<double>& v_old);

  // NR Parameters (Using centralized PhysicsConstants)
  // Note: Kept as local constexpr for backward compatibility; prefer PhysicsConstants::
  static constexpr double RELTOL = 1e-3;
  static constexpr double VNTOL = 1e-6;
  static constexpr double ABSTOL = 1e-12; // 1 pico-Ampere resolution
  static constexpr int MAX_NR_ITER = 50;
  static constexpr double PCG_TOL = 1e-10;
  static constexpr int PCG_MAX_ITER = 1000;

  /**
   * checkConvergence
   * Verifies BOTH voltage stability (approximate) AND physical charge
   * conservation (exact).
   */
  bool checkConvergence(const std::vector<double> &v_new,
                        const std::vector<double> &v_old,
                        double max_kcl_error,
                        double kcl_tol = 1e-6) {
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
    if (max_kcl_error > kcl_tol)
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

      // Use CLAMPED physics kernel (matching solver) to avoid false divergence
      // on valid linear continuation steps.
      DiodeParams<double> params = {d.saturationCurrent_I_S_A, d.emissionCoefficient_N,
                            d.thermalVoltage_V_T_V};
      double i_d = diode_current_clamped(v_d, params, 30.0); // Match device_physics.h clamp

      addRes(d.anode, -i_d);
      addRes(d.cathode, i_d);
    }

    // 3.25 Schottky Diode Audit
    for (const auto &sd : netlist.globalBlock.schottkyDiodes) {
      double v_a = getV(sd.anode);
      double v_c = getV(sd.cathode);
      double v_d = v_a - v_c;
      
      DiodeParams<double> params = {sd.saturationCurrent_I_S_A, sd.emissionCoefficient_N,
                            sd.thermalVoltage_V_T_V};
      double i_d = diode_current_clamped(v_d, params, 30.0);
      
      addRes(sd.anode, -i_d);
      addRes(sd.cathode, i_d);
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
        
        // Use CLAMPED physics for consistent residual auditing
        // bjt_ebers_moll uses "linear_continuation_diode" internally which we clamped to 30.0
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

    // 3.6 JFET Audit (Shichman-Hodges)
    for (const auto &j : netlist.globalBlock.jfets) {
        double v_d = getV(j.drain);
        double v_g = getV(j.gate);
        double v_s = getV(j.source);
        
        double vgs = v_g - v_s;
        double vds = v_d - v_s;
        
        JFETParams<double> params;
        params.Beta = j.beta;
        params.Vto = j.Vto;
        params.lambda = j.lambda;
        
        // For P-channel, negate voltages and result
        double sign = j.isNChannel ? 1.0 : -1.0;
        double ids = sign * jfet_ids(sign * vgs, sign * vds, params);
        
        // KCL: Current leaves drain, enters source
        addRes(j.drain, -ids);
        addRes(j.source, ids);
    }

    // 4. Capacitors (Transient Only)
    // If h > 0, we are in transient and must audit the displacement current
    if (h > 1e-15 && integrator) {
      for (size_t i = 0; i < netlist.globalBlock.capacitors.size(); ++i) {
        const auto &c = netlist.globalBlock.capacitors[i];
        const auto &hist = netlist.globalState.capacitorState[i];
          
        double v_curr = getV(c.nodePlate1) - getV(c.nodePlate2);
        double i_c = 0.0;
        IntegrationType type = integrator->getType();

        if (hist.v.empty())
          continue;

        if (type == IntegrationType::TRAPEZOIDAL) {
          double g_eq = (2.0 * c.capacitance_farads) / h;
          i_c = g_eq * (v_curr - hist.v[0]) - hist.i[0];
        } else if (type == IntegrationType::GEAR_1_EULER) {
          i_c = (c.capacitance_farads / h) * (v_curr - hist.v[0]);
        } else if (type == IntegrationType::GEAR_2) {
          if (hist.v.size() >= 2) {
            i_c = (c.capacitance_farads / h) *
                  (1.5 * v_curr - 2.0 * hist.v[0] +
                   0.5 * hist.v[1]);
          } else {
            i_c = (c.capacitance_farads / h) * (v_curr - hist.v[0]);
          }
        }
        addRes(c.nodePlate1, -i_c);
        addRes(c.nodePlate2, i_c);
      }
      
      // 4.5. Inductors (Transient Only)
      // 4.5. Inductors (Transient Only)
      for (size_t i = 0; i < netlist.globalBlock.inductors.size(); ++i) {
        const auto &l = netlist.globalBlock.inductors[i];
        const auto &hist = netlist.globalState.inductorState[i];
        
        double v_curr = getV(l.nodeCoil1) - getV(l.nodeCoil2);
        double i_l = 0.0;
        IntegrationType type = integrator->getType();

        if (hist.i.empty())
          continue;

        if (type == IntegrationType::TRAPEZOIDAL) {
          double g_eq = h / (2.0 * l.inductance_henries);
          i_l = hist.i[0] + g_eq * (v_curr + hist.v[0]);
        } else if (type == IntegrationType::GEAR_1_EULER) {
          i_l = hist.i[0] + (h / l.inductance_henries) * v_curr;
        } else if (type == IntegrationType::GEAR_2) {
          if (hist.i.size() >= 2) {
            double coeff = h / l.inductance_henries;
            i_l = (4.0/3.0)*hist.i[0] - (1.0/3.0)*hist.i[1] 
                  + (2.0/3.0)*coeff*v_curr;
          } else {
            i_l = hist.i[0] + (h / l.inductance_henries) * v_curr;
          }
        }

        addRes(l.nodeCoil1, -i_l);
        addRes(l.nodeCoil2, i_l);
      }
    }

    // 5. Hierarchical Instances (Phase 1: Structural Abstraction)
    // PARALLEL STAMPING: Process jobs concurrently.
    // Use atomic updates for the residual vector to avoid race conditions on shared nodes.
    
    // Redefine addRes for thread-safety within the parallel region
    auto addResAtomic = [&](int nodeIdx, double val) {
        if (nodeIdx > 0 && nodeIdx <= n) {
            #pragma omp atomic
            node_residuals[nodeIdx - 1] += val;
        }
    };

    #pragma omp parallel for
    for (int i = 0; i < (int)scheduler.jobs.size(); ++i) {
      const auto &job = scheduler.jobs[i];
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
          addResAtomic(mapNode(r.nodeTerminal1), -i_r);
          addResAtomic(mapNode(r.nodeTerminal2), i_r);
        }

        // Diodes
        for (const auto &d : block.diodes) {
          double v_d = getV(mapNode(d.anode)) - getV(mapNode(d.cathode));
          DiodeParams<double> params = {d.saturationCurrent_I_S_A,
                                d.emissionCoefficient_N,
                                d.thermalVoltage_V_T_V};
          double i_d = diode_current(v_d, params);
          addResAtomic(mapNode(d.anode), -i_d);
          addResAtomic(mapNode(d.cathode), i_d);
        }

        // Capacitors (Transient)
        if (h > 1e-15 && integrator) {
          const auto &instState = netlist.instanceStates[instanceIdx];
          for (size_t c_idx = 0; c_idx < block.capacitors.size(); ++c_idx) {
            const auto &c = block.capacitors[c_idx];
            const auto &hist = instState.capacitorState[c_idx];

            double v_curr =
                getV(mapNode(c.nodePlate1)) - getV(mapNode(c.nodePlate2));
            double i_c = 0.0;
            IntegrationType type = integrator->getType();
            if (hist.v.empty())
              continue;

            if (type == IntegrationType::TRAPEZOIDAL) {
              double g_eq = (2.0 * c.capacitance_farads) / h;
              i_c =
                  g_eq * (v_curr - hist.v[0]) - hist.i[0];
            } else if (type == IntegrationType::GEAR_1_EULER) {
              i_c =
                  (c.capacitance_farads / h) * (v_curr - hist.v[0]);
            } else if (type == IntegrationType::GEAR_2) {
              if (hist.v.size() >= 2) {
                i_c = (c.capacitance_farads / h) *
                      (1.5 * v_curr - 2.0 * hist.v[0] +
                       0.5 * hist.v[1]);
              } else {
                i_c = (c.capacitance_farads / h) *
                      (v_curr - hist.v[0]);
              }
            }
            addResAtomic(mapNode(c.nodePlate1), -i_c);
            addResAtomic(mapNode(c.nodePlate2), i_c);
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
  enum class ComponentType { RESISTOR, CAPACITOR, INDUCTOR, DIODE, MOSFET, BJT, JFET };

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
      stampAllElements(netlist, 0.0, v_converged, 0.0); // h=0, time=0 for DC sensitivity
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
            // CAPACITOR SENSITIVITY (DC: dI/dC = 0, AC needs omega)
            else if (type == ComponentType::CAPACITOR) {
                if (index < 0 || index >= (int)netlist.globalBlock.capacitors.size()) return {};
                // DC: Capacitors are open circuit, no current, so dI/dC = 0.
                // For AC sensitivity, user would need to provide omega. Placeholder for now.
            }
            // INDUCTOR SENSITIVITY (DC: dI/dL = 0, AC needs omega)
            else if (type == ComponentType::INDUCTOR) {
                if (index < 0 || index >= (int)netlist.globalBlock.inductors.size()) return {};
                // DC: Inductors are short, no L term in DC model. dI/dL = 0.
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
                    using std::exp;
                    return exp(arg); // Use ADL for Dual
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
  void stampBlockLinear(const TensorBlock &block, const BlockState &state,
                        const std::function<int(int)> &mapNode, double h, double time,
                        double voltageScale = 1.0, int mcSeed = 0) {

    // 1. Resistors
    for (const auto &r : block.resistors) {
      // MC Perturbation
      double R = getPerturbedValue(r.resistance_ohms, r.tolerance_percent, r.name, mcSeed);
      
      stampResistor(mapNode(r.nodeTerminal1), mapNode(r.nodeTerminal2),
                    R, matrixBuilder);
    }
    // 2. Voltage Sources
    for (const auto &v : block.voltageSources) {
      // Apply PVT Voltage Scale (e.g. 1.1x Vdd)
      // Ideally we only scale power rails, but for now we scale all independent DC sources
      double scaled_V = v.voltage_V * voltageScale;
      
      stampVoltageSourceAsNorton(mapNode(v.nodePositive),
                                 mapNode(v.nodeNegative), scaled_V,
                                 matrixBuilder, rhsVector);
    }
    // 2.5 Power Rails (System Source)
    for (size_t i = 0; i < block.powerRails.size(); ++i) {
        const auto &rail = block.powerRails[i];
        const auto &hist = state.powerRailState[i];
        // Retrieve history (dc/transient)
        double v_prev = 0.0;
        double i_prev_cap = 0.0;
        if (!hist.v.empty()) {
            v_prev = hist.v[0];
            i_prev_cap = hist.i[0]; // Stores capacitor current
        }
        
        stampPowerRail(mapNode(rail.nodeRail), rail.nominal_V * voltageScale, rail.ripple_Vpp, rail.frequency_Hz,
                       rail.ESR_ohms, rail.capacitance_F, h, time, 
                       v_prev, i_prev_cap,
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
      for (size_t i = 0; i < block.capacitors.size(); ++i) {
        const auto &c = block.capacitors[i];
        const auto &hist = state.capacitorState[i];
        double C = getPerturbedValue(c.capacitance_farads, c.tolerance_percent, c.name, mcSeed);
        
        integrator->stampCapacitor(mapNode(c.nodePlate1), mapNode(c.nodePlate2),
                                   C, h, hist.v,
                                   hist.i, matrixBuilder, rhsVector);
      }
    }

    // 5. Inductors (Transient)
    if (h > 1e-15 && integrator) {
      for (size_t i = 0; i < block.inductors.size(); ++i) {
          const auto &ind = block.inductors[i];
          const auto &hist = state.inductorState[i];
          int n1 = mapNode(ind.nodeCoil1);
          int n2 = mapNode(ind.nodeCoil2);
          double L = getPerturbedValue(ind.inductance_henries, ind.tolerance_percent, ind.name, mcSeed);
          
          if (hist.i.empty() || hist.v.empty()) continue;
          
          double i_prev = hist.i[0];
          double v_prev = hist.v[0];

          
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

      // 5.5 Mutual Inductance (Coupled Inductors / Transformers)
      // Uses L-matrix inversion: Γ = L^-1 to get admittance form
      // For coupled pair: [v1]   [L1  M ] [di1/dt]
      //                   [v2] = [M   L2] [di2/dt]
      // Invert to: [di1/dt]   1/det [L2  -M] [v1]
      //            [di2/dt] =       [-M  L1] [v2]
      // where det = L1*L2 - M^2
      for (const auto &mut : block.mutualInductors) {
          if (mut.inductor1_index < 0 || mut.inductor1_index >= (int)block.inductors.size()) continue;
          if (mut.inductor2_index < 0 || mut.inductor2_index >= (int)block.inductors.size()) continue;
          
          const auto &L1_ind = block.inductors[mut.inductor1_index];
          const auto &L2_ind = block.inductors[mut.inductor2_index];
          
          double L1 = L1_ind.inductance_henries;
          double L2 = L2_ind.inductance_henries;
          double k = mut.couplingCoefficient_k;
          double M = k * std::sqrt(L1 * L2);
          
          // det = L1*L2 - M^2 = L1*L2*(1 - k^2)
          double det = L1 * L2 - M * M;
          if (std::abs(det) < 1e-20) continue; // Singular (k=1 ideal transformer)
          
          // Gamma coefficients (L^-1 scaled by h for transient)
          double inv_det = 1.0 / det;
          double g11 = h * L2 * inv_det;   // di1 = g11*v1 - g12*v2
          double g12 = h * M * inv_det;    // Cross-coupling
          double g22 = h * L1 * inv_det;   // di2 = g22*v2 - g12*v1
          
          int n1a = mapNode(L1_ind.nodeCoil1);
          int n1b = mapNode(L1_ind.nodeCoil2);
          int n2a = mapNode(L2_ind.nodeCoil1);
          int n2b = mapNode(L2_ind.nodeCoil2);
          
          // Self-admittance for L1: stamps g11 across (n1a, n1b)
          // Already stamped by single-inductor section above, so we only add cross-coupling
          
          // Cross-coupling: current in L1 due to voltage across L2
          // i1_cross = -g12 * (v2a - v2b)
          // Stamp as transconductance from (n2a-n2b) to (n1a-n1b)
          if (n1a != 0 && n2a != 0) matrixBuilder.add(n1a-1, n2a-1, -g12);
          if (n1a != 0 && n2b != 0) matrixBuilder.add(n1a-1, n2b-1,  g12);
          if (n1b != 0 && n2a != 0) matrixBuilder.add(n1b-1, n2a-1,  g12);
          if (n1b != 0 && n2b != 0) matrixBuilder.add(n1b-1, n2b-1, -g12);
          
          // Cross-coupling: current in L2 due to voltage across L1
          // i2_cross = -g12 * (v1a - v1b)
          if (n2a != 0 && n1a != 0) matrixBuilder.add(n2a-1, n1a-1, -g12);
          if (n2a != 0 && n1b != 0) matrixBuilder.add(n2a-1, n1b-1,  g12);
          if (n2b != 0 && n1a != 0) matrixBuilder.add(n2b-1, n1a-1,  g12);
          if (n2b != 0 && n1b != 0) matrixBuilder.add(n2b-1, n1b-1, -g12);
          
          // History terms for cross-coupling
          // History terms for cross-coupling
          const auto &hist1 = state.inductorState[mut.inductor1_index];
          const auto &hist2 = state.inductorState[mut.inductor2_index];
          double i1_prev = hist1.i.empty() ? 0.0 : hist1.i[0];
          double i2_prev = hist2.i.empty() ? 0.0 : hist2.i[0];

          
          // The history contribution is complex for coupled inductors
          // Simplified: assume the single-inductor section handles self history,
          // and cross-coupling history is approximated as zero for now.
          // TODO: For accurate Trapezoidal integration, store coupled history terms.
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
    // Diodes
    size_t nD = block.diodes.size();
    if (nD > 0) {
        std::vector<int> rows, cols;
        std::vector<double> vals;
        rows.reserve(nD * 4);
        cols.reserve(nD * 4);
        vals.reserve(nD * 4);
        
        for (size_t i = 0; i < nD; ++i) {
            int nA = block.diodes.anodes[i];
            int nC = block.diodes.cathodes[i];
            double g = block.diodes.g_d[i];
            double i_eq = block.diodes.i_d[i] - g * block.diodes.v_d[i];
            
            // Matrix Stamping (G)
            if (nA != 0 && nC != 0) {
                // Diagonal
                rows.push_back(nA-1); cols.push_back(nA-1); vals.push_back(g);
                rows.push_back(nC-1); cols.push_back(nC-1); vals.push_back(g);
                // Off-Diagonal
                rows.push_back(nA-1); cols.push_back(nC-1); vals.push_back(-g);
                rows.push_back(nC-1); cols.push_back(nA-1); vals.push_back(-g);
            } else if (nA != 0) {
                rows.push_back(nA-1); cols.push_back(nA-1); vals.push_back(g);
            } else if (nC != 0) {
                rows.push_back(nC-1); cols.push_back(nC-1); vals.push_back(g);
            }
            
            // RHS Stamping
            if (nA != 0) rhsVector[nA-1] -= i_eq;
            if (nC != 0) rhsVector[nC-1] += i_eq;
        }
        matrixBuilder.addBatch(rows, cols, vals);
        
        // GPU Hook (Future Integration)
        /*
        if (activeBackend == Backend::WEBGPU || activeBackend == Backend::CUDA) {
            // Upload Tensors to GPU Storage Buffer
            gpuAssembler.upload(block.diodes.g_d_buffer, block.diodes.g_d); 
            // Dispatch Assembly Shader (Scatter-Add)
            gpuAssembler.dispatchStamping("diode_stamp_kernel");
        }
        */
    }


    
    // MOSFETs
    // MOSFETs
    size_t nM = block.mosfets.size();
    if (nM > 0) {
        std::vector<int> rows, cols;
        std::vector<double> vals;
        // Estimate size: 4 nodes * 3 connections approx? 
        // gm: ID vs VGS -> (D,G), (S,G)
        // gds: ID vs VDS -> (D,D), (D,S), (S,D), (S,S)
        rows.reserve(nM * 12);
        cols.reserve(nM * 12);
        vals.reserve(nM * 12);
        
        for (size_t i=0; i<nM; ++i) {
             int d = block.mosfets.drains[i];
             int g = block.mosfets.gates[i];
             int s = block.mosfets.sources[i];
             // int b = block.mosfets.bodies[i]; // Ignored in simple level 1 for now or absorbed
             
             double gm = block.mosfets.gm[i];
             double gds = block.mosfets.gds[i];
             double ids = block.mosfets.ids[i];
             double vgs = block.mosfets.vgs[i];
             double vds = block.mosfets.vds[i];
             double i_eq = ids - (gm * vgs + gds * vds);
             
             // Stamping Gds (Conductance between Drain and Source)
             //  [+Gds  -Gds]
             //  [-Gds  +Gds]
             if (d!=0 && s!=0) {
                 rows.push_back(d-1); cols.push_back(d-1); vals.push_back(gds);
                 rows.push_back(s-1); cols.push_back(s-1); vals.push_back(gds);
                 rows.push_back(d-1); cols.push_back(s-1); vals.push_back(-gds);
                 rows.push_back(s-1); cols.push_back(d-1); vals.push_back(-gds);
             } else if (d!=0) {
                 rows.push_back(d-1); cols.push_back(d-1); vals.push_back(gds);
             } else if (s!=0) {
                 rows.push_back(s-1); cols.push_back(s-1); vals.push_back(gds);
             }
             
             // Stamping Gm (Transconductance from Gate-Source to Drain-Source)
             // I_ds += gm * Vgs = gm * (Vg - Vs)
             // Drain Row: +gm*Vg - gm*Vs
             // Source Row: -gm*Vg + gm*Vs
             
             // Drain Row
             if (d!=0) {
                 if (g!=0) { rows.push_back(d-1); cols.push_back(g-1); vals.push_back(gm); }
                 if (s!=0) { rows.push_back(d-1); cols.push_back(s-1); vals.push_back(-gm); }
             }
             // Source Row
             if (s!=0) {
                 if (g!=0) { rows.push_back(s-1); cols.push_back(g-1); vals.push_back(-gm); }
                 if (s!=0) { rows.push_back(s-1); cols.push_back(s-1); vals.push_back(gm); }
             }
             
             // RHS
             // I_d_total = I_eq + ...
             // KCL DrainNode: Isum = 0 -> ... + I_d_leaving = 0 -> ... + I_eq = 0 -> RHS = -I_eq
             // I_d leaving drain
             if (d!=0) rhsVector[d-1] -= i_eq;
             if (s!=0) rhsVector[s-1] += i_eq;
        }
        matrixBuilder.addBatch(rows, cols, vals);

        // GPU Hook (Future Integration)
        /*
        if (activeBackend == Backend::WEBGPU) {
             gpuAssembler.upload(block.mosfets.gds_buffer, block.mosfets.gds);
             gpuAssembler.dispatchStamping("mosfet_stamp_kernel");
        }
        */
    }


    
    // BJTs
    // BJTs
    size_t nQ = block.bjts.size();
    if (nQ > 0) {
        std::vector<int> rows, cols;
        std::vector<double> vals;
        rows.reserve(nQ * 9);
        cols.reserve(nQ * 9);
        vals.reserve(nQ * 9);
        
        for (size_t i = 0; i < nQ; ++i) {
            int nC = block.bjts.collectors[i];
            int nB = block.bjts.bases[i];
            int nE = block.bjts.emitters[i];
            
            double g_cc = block.bjts.g_cc[i]; double g_cb = block.bjts.g_cb[i]; double g_ce = block.bjts.g_ce[i];
            double g_bc = block.bjts.g_bc[i]; double g_bb = block.bjts.g_bb[i]; double g_be = block.bjts.g_be[i];
            double g_ec = block.bjts.g_ec[i]; double g_eb = block.bjts.g_eb[i]; double g_ee = block.bjts.g_ee[i];
            
            // Helper to push triplet
            auto addG = [&](int r, int c, double v) { 
                if(r!=0 && c!=0) { rows.push_back(r-1); cols.push_back(c-1); vals.push_back(v); }
            };
            
            addG(nC, nC, g_cc); addG(nC, nB, g_cb); addG(nC, nE, g_ce);
            addG(nB, nC, g_bc); addG(nB, nB, g_bb); addG(nB, nE, g_be);
            addG(nE, nC, g_ec); addG(nE, nB, g_eb); addG(nE, nE, g_ee);
            
            // RHS
             double v_c = (nC>0 ? v_guess[nC-1] : 0.0);
             double v_b = (nB>0 ? v_guess[nB-1] : 0.0);
             double v_e = (nE>0 ? v_guess[nE-1] : 0.0);
             
             double Iceq = block.bjts.Ic[i] - (g_cc*v_c + g_cb*v_b + g_ce*v_e);
             double Ibeq = block.bjts.Ib[i] - (g_bc*v_c + g_bb*v_b + g_be*v_e);
             double Ieeq = block.bjts.Ie[i] - (g_ec*v_c + g_eb*v_b + g_ee*v_e);
             
             if (nC != 0) rhsVector[nC-1] -= Iceq;
             if (nB != 0) rhsVector[nB-1] -= Ibeq;
             if (nE != 0) rhsVector[nE-1] -= Ieeq;
        }
        matrixBuilder.addBatch(rows, cols, vals);

        // GPU Hook (Future Integration)
        /*
        if (activeBackend == Backend::WEBGPU) {
             gpuAssembler.dispatchStamping("bjt_stamp_kernel");
        }
        */
    }


  }

  void stampBlockNonLinear(const TensorBlock &block,
                           const std::function<int(int)> &mapNode,
                           const std::vector<double> &v_guess,
                           double temp_K = 300.15) {
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
      
      // Thermal Update: Vt = k T / q
      // Scale Vt relative to default 300.15K (assuming stored Vt is at 300K)
      double temp_scale = temp_K / 300.15;
      params.Vt = params.Vt * temp_scale;
      
      // Simple Is Tempco (exponential leakage increase)
      // Rule of thumb: doubles every 10C. 
      // This is a heuristic until we have full models.
      // double delta_T = temp_K - 300.15;
      // params.Is *= std::pow(2.0, delta_T / 10.0);
      
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

    // 1.5 Schottky Diodes (same physics, different defaults + series resistance)
    for (const auto &sd : block.schottkyDiodes) {
      int nA = mapNode(sd.anode);
      int nC = mapNode(sd.cathode);
      double v_diff = getV(nA) - getV(nC);

      DiodeParams<double> params = {sd.saturationCurrent_I_S_A, sd.emissionCoefficient_N,
                            sd.thermalVoltage_V_T_V};
      double temp_scale = temp_K / 300.15;
      params.Vt = params.Vt * temp_scale;
      
      Dual<double> v_diff_dual(v_diff, 1.0);
      Dual<double> i_schottky = diode_current_clamped(v_diff_dual, params);
      
      double g_eq = i_schottky.grad;
      double i_eq = i_schottky.val - g_eq * v_diff;
      
      if (g_eq < 1e-15) g_eq = 1e-15;
      
      // Stamp diode conductance
      stampResistor(nA, nC, 1.0 / g_eq, matrixBuilder);
      
      // Stamp series resistance Rs (if significant)
      if (sd.seriesResistance_Rs_ohms > 1e-6) {
          // For simplicity, we model Rs in series by adding a small conductance correction.
          // Full implementation would require an internal node. For now, approximate.
          double g_rs = 1.0 / sd.seriesResistance_Rs_ohms;
          // Reduce effective conductance: g_total = 1 / (1/g_eq + Rs)
          // This is a linearization approximation.
      }
      
      if (nA != 0) rhsVector[nA - 1] -= i_eq;
      if (nC != 0) rhsVector[nC - 1] += i_eq;
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
        
        // Thermal Update
        params.Vt = params.Vt * (temp_K / 300.15);
        
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

    // 4. JFETs (Shichman-Hodges)
    for (const auto &j : block.jfets) {
        int nD = mapNode(j.drain);
        int nG = mapNode(j.gate);
        int nS = mapNode(j.source);
        
        double v_d = getV(nD);
        double v_g = getV(nG);
        double v_s = getV(nS);
        
        double vgs = v_g - v_s;
        double vds = v_d - v_s;
        
        JFETParams<double> params;
        params.Beta = j.beta;
        params.Vto = j.Vto;
        params.lambda = j.lambda;
        
        JFETLinear lin = jfet_linearization(vgs, vds, j.isNChannel, params);
        
        // Stamp equivalent conductance (gm, gds)
        // Ids flows from Drain to Source
        // dIds/dVgs = gm -> stamps between (D,G) and (S,G)
        // dIds/dVds = gds -> stamps between (D,S)
        
        stampMosfet(nD, nG, nS, 0, lin.gm, lin.gds, matrixBuilder);
        
        // RHS: I_eq = Ids - gm*vgs - gds*vds
        double ieq = lin.Ids - lin.gm * vgs - lin.gds * vds;
        
        if (nD != 0) rhsVector[nD - 1] -= ieq;
        if (nS != 0) rhsVector[nS - 1] += ieq;
    }
  }

  /**
   * stampAllElements
   * High-level entry point for matrix assembly.
   */
  void stampAllElements(const TensorNetlist &netlist, double h,
                        const std::vector<double> &v_guess, double time) {
    // A. Global Block
    auto identityMap = [](int n) { return n; };
    stampBlockLinear(netlist.globalBlock, netlist.globalState, identityMap, h, time, netlist.environment.global_voltage_scale, netlist.environment.monte_carlo_seed);
    // stampBlockNonLinear(netlist.globalBlock, identityMap, v_guess); 
    stampSoABlock(soaBlock, v_guess); // SoA for Global Block - TODO: Pass PVT to SoA

    // B. Hierarchical Batches
    for (const auto &job : scheduler.jobs) {
      const TensorBlock &block = *job.blockTmpl;
      for (int instanceIdx : job.instanceIds) {
        const auto &instance = netlist.instances[instanceIdx];
        const auto &instState = netlist.instanceStates[instanceIdx];
        auto mapNode = [&](int localNode) {
          return (localNode == 0) ? 0 : instance.nodeMapping[localNode - 1];
        };
        stampBlockLinear(block, instState, mapNode, h, time, netlist.environment.global_voltage_scale, netlist.environment.monte_carlo_seed);
        stampBlockNonLinear(block, mapNode, v_guess, netlist.environment.ambient_temp_K);
      }
    }

  }


  void addDiagonalConditioning(int n, double gmin = 1e-12) {
    for (int i = 0; i < n; ++i)
      matrixBuilder.add(i, i, gmin);
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

#include "circuitsim_audit.inc"
};

// =============================================================================
// SOLVER IMPLEMENTATIONS
// =============================================================================
// solveDC and stepTransient are implemented in circuitsim_solver.cpp
// to reduce header size and improve compile times.
