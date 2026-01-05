#pragma once
#include "circuit.h"
#include "linalg.h"
#include <algorithm>
#include <cassert> // NASA Rule #5
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

#include "device_physics.h"
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
  std::vector<double> node_weights; // Physical significance (sum of C and G)
  double prev_energy = 0.0;
  int instability_counter = 0;
  static constexpr int INSTABILITY_THRESHOLD = 3;

public:
  void initialize(const TensorNetlist &netlist) {
    int n = netlist.numGlobalNodes;
    prev_voltages.assign(n, 0.0);
    prev_prev_voltages.assign(n, 0.0);
    node_weights.assign(n, 1e-12); // Noise floor
    prev_energy = 0.0;
    instability_counter = 0;

    // Calculate Physical Weights: nodes with high C/G are more significant
    auto addWeight = [&](int node, double w) {
      if (node > 0 && node <= n)
        node_weights[node - 1] += std::abs(w);
    };

    // Scan Global Block
    for (const auto &r : netlist.globalBlock.resistors) {
      double g = 1.0 / r.resistance_ohms;
      addWeight(r.nodeTerminal1, g);
      addWeight(r.nodeTerminal2, g);
    }
    for (const auto &c : netlist.globalBlock.capacitors) {
      addWeight(c.nodePlate1, c.capacitance_farads);
      addWeight(c.nodePlate2, c.capacitance_farads);
    }

    // Scan Instances (Tensors)
    for (const auto &inst : netlist.instances) {
      auto it = netlist.blockDefinitions.find(inst.blockName);
      if (it == netlist.blockDefinitions.end())
        continue;
      const auto &block = it->second;

      auto mapNode = [&](int localNode) {
        return (localNode == 0) ? 0 : inst.nodeMapping[localNode - 1];
      };

      for (const auto &r : block.resistors) {
        double g = 1.0 / r.resistance_ohms;
        addWeight(mapNode(r.nodeTerminal1), g);
        addWeight(mapNode(r.nodeTerminal2), g);
      }
      for (const auto &c : block.capacitors) {
        addWeight(mapNode(c.nodePlate1), c.capacitance_farads);
        addWeight(mapNode(c.nodePlate2), c.capacitance_farads);
      }
    }
  }

  const std::vector<double> &getPrevVoltages() const { return prev_voltages; }

  /**
   * analyzeStability
   * Monitors the simulation health and recommends if we need to cut the
   * time-step (Tier 1) or switch integration methods entirely (Tier 2) to
   * maintain convergence.
   *
   * Returns: 0 = Stable, 1 = Needs smaller dt, 2 = Needs more robust method
   * (Gear)
   */

  int analyzeStability(const TensorNetlist &netlist,
                       const std::vector<double> &current_voltages,
                       IIntegrationMethod *integrator) {
    if (current_voltages.empty())
      return 0;
    if (current_voltages.size() != prev_voltages.size()) {
      initialize(netlist);
      return 0;
    }

    // 1. Calculate Circuit Energy: E = 0.5 * sum(C * v^2)
    double current_energy = 0.0;
    for (const auto &c : netlist.globalBlock.capacitors) {
      double v = 0.0;
      if (c.nodePlate1 > 0 && c.nodePlate1 <= (int)current_voltages.size())
        v += current_voltages[c.nodePlate1 - 1];
      if (c.nodePlate2 > 0 && c.nodePlate2 <= (int)current_voltages.size())
        v -= current_voltages[c.nodePlate2 - 1];
      current_energy += 0.5 * c.capacitance_farads * v * v;
    }

    // Include hierarchical instances energy
    for (const auto &inst : netlist.instances) {
      auto it = netlist.blockDefinitions.find(inst.blockName);
      if (it == netlist.blockDefinitions.end())
        continue;
      const auto &block = it->second;
      auto mapNode = [&](int localNode) {
        return (localNode == 0) ? 0 : inst.nodeMapping[localNode - 1];
      };
      auto getV = [&](int node) {
        if (node > 0 && node <= (int)current_voltages.size())
          return current_voltages[node - 1];
        return 0.0;
      };

      for (const auto &c : block.capacitors) {
        double v = getV(mapNode(c.nodePlate1)) - getV(mapNode(c.nodePlate2));
        current_energy += 0.5 * c.capacitance_farads * v * v;
      }
    }

    // 2. Weighted Ringing & LTE
    double total_err = 0.0;
    double weight_sum = 0.0;
    int weighted_sign_flips = 0;

    for (size_t i = 0; i < current_voltages.size(); ++i) {
      double v = current_voltages[i];
      double v_prev = prev_voltages[i];
      double v_prev2 = prev_prev_voltages[i];
      double w = node_weights[i];

      // LTE: Distance from predictor (Omitted as it's currently unused and
      // buggy) Better: use specific node history if available? Simple approach:
      // calculate weighted absolute sign flip delta
      double dv = v - v_prev;
      double dv_prev = v_prev - v_prev2;

      if (std::abs(dv) > 1e-6 && (dv * dv_prev < 0))
        weighted_sign_flips += (int)(w * 1e9); // Scale weighting
    }

    bool energy_exploding =
        (current_energy > prev_energy * 1.5 + 1e-9 && prev_energy > 1e-12);

    // Update history
    prev_prev_voltages = prev_voltages;
    prev_voltages = current_voltages;
    prev_energy = current_energy;

    // If energy jumped by more than 50%, the solver is likely oscillating
    if (energy_exploding)
      return 1; // Tier 1: Try a smaller time step (dt)

    if (weighted_sign_flips > 100) {
      instability_counter++;

      // If we've been unstable for a few steps, Trapezoidal is struggling.
      // Switch to a more damped method like Gear.
      if (instability_counter >= INSTABILITY_THRESHOLD)
        return 2; // Tier 2: Switch Method entirely

      return 1; // Tier 1: Try smaller dt first
    } else {
      // Recover stability slowly
      instability_counter = std::max(0, instability_counter - 1);
    }

    return 0;
  }
};

class CircuitSim {
public:
  // ... (Constructor/SethMethod remain same) ...

  IntegrationType getIntegrationMethod() const { return integrator->getType(); }

  // ... (Rest of class, adding arbiter member)
  CircuitSim() {
    // Default to Trapezoidal
    integrator = std::make_unique<TrapezoidalIntegration>();
  }

  // Allow moving (required for unique_ptr member)
  CircuitSim(CircuitSim &&) = default;
  CircuitSim &operator=(CircuitSim &&) = default;

  void setIntegrationMethod(IntegrationType type) {
    switch (type) {
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

  SimulationStep solveDC(TensorNetlist &netlist);
  SimulationStep stepTransient(TensorNetlist &netlist, double timeStep,
                               double currentTime);

  void resetArbiter() { arbiterInitialized = false; }

private:
  MatrixConstructor matrixBuilder;
  std::vector<double> rhsVector;
  TensorScheduler scheduler;
  bool arbiterInitialized = false;

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

    // 3. Diodes (Shockley Equation: I = Is * (exp(V/Vt) - 1))
    for (const auto &d : netlist.globalBlock.diodes) {
      double v_anode = getV(d.anode);
      double v_cathode = getV(d.cathode);
      double v_d = v_anode - v_cathode;

      // Use exact physics kernel (no clamping for residual check)
      DiodeParams params = {d.saturationCurrent_I_S_A, d.emissionCoefficient_N,
                            d.thermalVoltage_V_T_V};
      double i_d = diode_current(v_d, params);

      addRes(d.anode, -i_d);
      addRes(d.cathode, i_d);
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
          DiodeParams params = {d.saturationCurrent_I_S_A,
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

  // Phase 2: Numerical Integration Strategy
  std::unique_ptr<IIntegrationMethod> integrator;
  IntegratorArbiter arbiter; // The Arbiter instance

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
      // Inductors would normally require auxiliary current variables,
      // but our simplified Inductor model currently mirrors C for
      // proof-of-concept. (Real L support would be added in Phase 2)
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
   * Linearizes and stamps non-linear elements (Diodes, MOSFETs) for a specific
   * block instance.
   */
  void stampBlockNonLinear(const TensorBlock &block,
                           const std::function<int(int)> &mapNode,
                           const std::vector<double> &v_guess) {
    auto getV = [&](int globalNode) {
      if (globalNode > 0 && globalNode <= (int)v_guess.size())
        return v_guess[globalNode - 1];
      return 0.0;
    };

    // 1. Diodes
    for (const auto &d : block.diodes) {
      double v_a = getV(mapNode(d.anode));
      double v_c = getV(mapNode(d.cathode));
      double v_diff = v_a - v_c;

      DiodeParams params = {d.saturationCurrent_I_S_A, d.emissionCoefficient_N,
                            d.thermalVoltage_V_T_V};
      double i_diode = diode_current_clamped(v_diff, params);
      double g_eq = diode_conductance_clamped(v_diff, params);
      double i_eq = i_diode - g_eq * v_diff;

      // Stamp conductance (stamps.h helper)
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
      // Get terminal voltages
      double v_d = getV(mapNode(m.drain));
      double v_g = getV(mapNode(m.gate));
      double v_s = getV(mapNode(m.source));
      double v_b = getV(mapNode(m.body));

      double vgs = v_g - v_s;
      double vds = v_d - v_s;

      // Level 1 Model
      MosfetParams p;
      p.Kp = 200e-6;
      p.Vth = 0.7;
      p.lambda = 0.02;
      p.W = m.channelWidth_m;
      p.L = m.channelLength_m;

      double gm = mosfet_gm(vgs, vds, p);
      double gds = mosfet_gds(vgs, vds, p);
      double ids = mosfet_ids(vgs, vds, p);

      // Companion model current: Ieq = Ids - gm*Vgs - gds*Vds
      double ieq = ids - gm * vgs - gds * vds;

      stampMosfet(mapNode(m.drain), mapNode(m.gate), mapNode(m.source),
                  mapNode(m.body), gm, gds, matrixBuilder);

      // Stamp Ieq
      if (mapNode(m.drain) != 0)
        rhsVector[mapNode(m.drain) - 1] -= ieq;
      if (mapNode(m.source) != 0)
        rhsVector[mapNode(m.source) - 1] += ieq;
    }

    // 3. BJTs (Phase 2 placeholder)
    for (const auto &q : block.bjts) {
      // Simple gm model for Phase 1
      double gm = 0.04; // 40mS fixed gm
      stampMosfet(mapNode(q.nodeCollector), mapNode(q.base), mapNode(q.emitter),
                  mapNode(q.emitter), gm, gm / 100.0, matrixBuilder);
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
    stampBlockNonLinear(netlist.globalBlock, identityMap, v_guess);

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
   */
  void stampNonLinearDevices(TensorNetlist &netlist,
                             const std::vector<double> &voltageEstimate) {
    for (auto &diode : netlist.globalBlock.diodes) {
      // Get voltage across the diode
      double v_a = (diode.anode == 0) ? 0.0 : voltageEstimate[diode.anode - 1];
      double v_c =
          (diode.cathode == 0) ? 0.0 : voltageEstimate[diode.cathode - 1];
      double v_diff = v_a - v_c;

      // Use clamped physics kernels for NR stability
      DiodeParams params = {diode.saturationCurrent_I_S_A,
                            diode.emissionCoefficient_N,
                            diode.thermalVoltage_V_T_V};
      double i_diode = diode_current_clamped(v_diff, params);
      double conductance_eq = diode_conductance_clamped(v_diff, params);

      // Equivalent current source for companion model
      double current_eq = i_diode - conductance_eq * v_diff;

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

    // Check 1 + 2: Voltage Convergence AND Physical KCL Accuracy
    double kcl_error = calculatePhysicalResiduals(netlist, v_new);

    if (checkConvergence(v_new, voltageEstimate, kcl_error)) {
      voltageEstimate = v_new;
      converged = true;
      total_iterations = iteration + 1;
      break;
    }

    voltageEstimate = v_new;
    total_iterations = iteration + 1;
  }

  return {0.0, voltageEstimate, {total_iterations, 0.0, converged}};
}

inline SimulationStep CircuitSim::stepTransient(TensorNetlist &netlist,
                                                double timeStep,
                                                double currentTime) {
  if (netlist.numGlobalNodes == 0)
    return {currentTime, {}, {0, 0.0, true}};

  // Initialize the stability arbiter on the first run
  if (!arbiterInitialized) {
    arbiter.initialize(netlist);
    arbiterInitialized = true;
  }

  double actualTimeStep = timeStep;
  int attempt = 0;
  const int MAX_ATTEMPTS = 4;

  std::vector<double> finalVoltages;
  SolverResult lastResult;
  bool simulationConverged = false;

  // The Time-Stepping Loop: We try to solve the circuit at 'actualTimeStep'.
  // If the physics become unstable or the math fails, we shrink the step and
  // try again.
  while (attempt < MAX_ATTEMPTS) {
    // Newton-Raphson Loop for the current time point
    std::vector<double> nrEstimate = arbiter.getPrevVoltages();
    if (nrEstimate.empty())
      nrEstimate.assign(netlist.numGlobalNodes, 0.0);

    bool nrConverged = false;
    for (int iteration = 0; iteration < MAX_NR_ITER; ++iteration) {
      // Build the MNA Matrix (Phase 1: Unified Hierarchical Stamping)
      scheduler.schedule(netlist);
      matrixBuilder.clear();
      rhsVector.assign(netlist.numGlobalNodes, 0.0);

      stampAllElements(netlist, actualTimeStep, nrEstimate);
      addDiagonalConditioning(netlist.numGlobalNodes);

      Csr_matrix A = matrixBuilder.createCsr();
      lastResult = solvePCG(A, rhsVector, 1e-6);

      std::vector<double> v_new = lastResult.solution;

      // Note: We could apply pnjlim damping here as well for even higher
      // stability

      // Verify Physical Laws
      double kcl_error =
          calculatePhysicalResiduals(netlist, v_new, actualTimeStep);

      if (checkConvergence(v_new, nrEstimate, kcl_error)) {
        nrEstimate = v_new;
        nrConverged = true;
        break;
      }
      nrEstimate = v_new;
    }

    if (!nrConverged) {
      // If the math failed to converge, cut the time step in half and retry
      actualTimeStep *= 0.5;
      attempt++;
      continue;
    }

    // The math converged, but is it physically stable?
    int suggestion =
        arbiter.analyzeStability(netlist, nrEstimate, integrator.get());

    if (suggestion == 1) {
      // Tier 1: Physics are getting 'noisy', shrink the time step
      actualTimeStep *= 0.5;
      attempt++;
      continue;
    } else if (suggestion == 2) {
      // Tier 2: Trapezoidal is oscillating (ringing). Switch to a more stable
      // BDF/Gear method.
      if (integrator->getType() == IntegrationType::TRAPEZOIDAL) {
        setIntegrationMethod(IntegrationType::GEAR_2);
        continue;
      }
    }

    // Success! We found a stable, converged solution.
    finalVoltages = nrEstimate;
    simulationConverged = true;
    break;
  }

  // Now that the step is accepted, we update the integration history
  // (voltages/currents) so the next step has the proper context.
  if (simulationConverged) {
    for (auto &cap : netlist.globalBlock.capacitors) {
      double v_current = 0.0;
      auto getFinalV = [&](int nodeIdx) {
        if (nodeIdx <= 0 || nodeIdx > (int)finalVoltages.size())
          return 0.0;
        return finalVoltages[nodeIdx - 1];
      };

      v_current = getFinalV(cap.nodePlate1) - getFinalV(cap.nodePlate2);

      double i_current = 0.0;
      IntegrationType currentType = integrator->getType();

      // Calculate the branch current based on the specific integration formula
      // used
      if (currentType == IntegrationType::TRAPEZOIDAL) {
        double conductance = (2.0 * cap.capacitance_farads) / actualTimeStep;
        i_current = conductance * (v_current - cap.voltage_history[0]) -
                    cap.current_history[0];
      } else if (currentType == IntegrationType::GEAR_1_EULER) {
        double conductance = cap.capacitance_farads / actualTimeStep;
        i_current = conductance * (v_current - cap.voltage_history[0]);
      } else if (currentType == IntegrationType::GEAR_2) {
        // Special case: Gear 2 needs two points of history. If we don't have
        // them yet, we bootstrap with Euler.
        if (cap.voltage_history[1] == 0.0 && currentTime > actualTimeStep) {
          double conductance = cap.capacitance_farads / actualTimeStep;
          i_current = conductance * (v_current - cap.voltage_history[0]);
        } else {
          double factor = cap.capacitance_farads / actualTimeStep;
          i_current = factor * (1.5 * v_current - 2.0 * cap.voltage_history[0] +
                                0.5 * cap.voltage_history[1]);
        }
      }

      integrator->updateHistory(cap.voltage_history, cap.current_history,
                                v_current, i_current);
    }
  }

  if (!simulationConverged)
    return {currentTime,
            std::vector<double>(netlist.numGlobalNodes, 0.0),
            {lastResult.iterations, lastResult.finalResidual, false}};

  return {
      currentTime + actualTimeStep,
      finalVoltages,
      {lastResult.iterations, lastResult.finalResidual, simulationConverged}};
}
