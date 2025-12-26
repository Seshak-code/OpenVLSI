#pragma once
#include "circuit.h"
#include <vector>
#include <map>
#include <string>

/**
 * @file tensorscheduler.h
 * @brief Tensor Scheduler - Manages batching and structural reuse.
 * 
 * ============================================================================
 * IMPLEMENTATION GUIDE: TENSOR SCHEDULING
 * ============================================================================
 * 
 * The scheduler is the "Brain" of the Tensor MNA engine. Its job is to:
 *   1. Group identical block instances into "Solver Jobs".
 *   2. Dispatch these jobs to the LA core for batched solving.
 *   3. (Future) Manage GPU kernel launches for parallel corner solving.
 * 
 * WHY THIS MATTERS:
 *   In VLSI, a memory array has millions of identical SRAM cells. Instead of
 *   solving each cell independently (N solves), we:
 *     a) Identify that all cells are instances of the same TensorBlock.
 *     b) Factorize the block's matrix ONCE.
 *     c) Apply the factorization to all N instances (Tensor operation).
 */

// ============================================================================
// TODO: IMPLEMENT SolverJob
// ============================================================================
/**
 * PSEUDO-CODE:
 * 
 * struct SolverJob {
 *     std::string blockName;       // The TensorBlock template being solved
 *     std::vector<int> instanceIds; // Indices of instances in TensorNetlist::instances
 *     
 *     // (Future) Cached symbolic factorization for this block type
 *     // SymbolicFactorization cachedFactorization;
 * };
 */

// ============================================================================
// TODO: IMPLEMENT TensorScheduler
// ============================================================================
/**
 * PSEUDO-CODE:
 * 
 * class TensorScheduler {
 * public:
 *     std::vector<SolverJob> jobs;
 *     
 *     // Analyzes the netlist and groups identical blocks into jobs
 *     void schedule(const TensorNetlist& netlist) {
 *         jobs.clear();
 *         std::map<std::string, std::vector<int>> groupedInstances;
 *         
 *         // Group instances by their block name
 *         for (int i = 0; i < netlist.instances.size(); ++i) {
 *             groupedInstances[netlist.instances[i].blockName].push_back(i);
 *         }
 *         
 *         // Create a SolverJob for each unique block type
 *         for (auto const& [name, instances] : groupedInstances) {
 *             jobs.push_back({name, instances});
 *         }
 *     }
 *     
 *     // (Future) Dispatches jobs to GPU or batched CPU solver
 *     // void dispatch(BatchedSolver& solver);
 * };
 */
