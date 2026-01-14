#include "physics_tensors.h"
#include "../components/circuit.h"

/*
 * physics_tensors.cpp - Implementation of AoS->SoA conversion.
 */

TensorizedBlock tensorizeBlock(const TensorBlock& block) {
    TensorizedBlock result;
    
    // Convert Resistors
    result.resistors.nodes1.reserve(block.resistors.size());
    result.resistors.nodes2.reserve(block.resistors.size());
    result.resistors.R.reserve(block.resistors.size());
    for (const auto& r : block.resistors) {
        result.resistors.push(r.nodeTerminal1, r.nodeTerminal2, r.resistance_ohms);
    }
    
    // Convert Capacitors
    for (const auto& c : block.capacitors) {
        result.capacitors.push(c.nodePlate1, c.nodePlate2, c.capacitance_farads);
    }
    
    // Convert Diodes
    result.diodes.reserve(block.diodes.size());
    for (const auto& d : block.diodes) {
        // Rs default 0.0, Temp default passed
        result.diodes.push(d.anode, d.cathode, 
                          d.saturationCurrent_I_S_A, 
                          d.emissionCoefficient_N, 
                          d.thermalVoltage_V_T_V,
                          0.0, // Rs
                          d.temperature_K);
    }
    
    // Convert MOSFETs
    result.mosfets.reserve(block.mosfets.size());
    for (const auto& m : block.mosfets) {
        // Defaults: Kp=200e-6, Vth=0.7, Labmda=0.02, Temp=passed
        result.mosfets.push(m.drain, m.gate, m.source, m.body, 
                           m.channelWidth_m, m.channelLength_m,
                           200e-6, 0.7, 0.02, 
                           m.temperature_K);
    }
    
    // Convert BJTs
    result.bjts.reserve(block.bjts.size());
    for (const auto& q : block.bjts) {
        result.bjts.push(q.nodeCollector, q.base, q.emitter,
                        q.saturationCurrent_I_S_A,
                        q.betaF, q.betaR,
                        q.thermalVoltage_V_T_V,
                        q.isNPN,
                        q.temperature_K);
    }
    
    return result;
}
