CuteApp Project Roadmap
📖 GLOSSARY & FUNDAMENTAL CONCEPTS
Term	Definition
Numerical Authority	cuteApp as the "Golden Reference" for open-source VLSI, verified to bit-exact parity with reference tools.
Trust Surface Layer	UI paradigm exposing mathematical proof of result validity (residuals, conservation laws).
Tensor / TensorNetlist	High-speed structural data format (SoA) enabling GPU-accelerated physics and structural hashing.
LDE	Layout-Dependent Effects; performance impact from physical proximity and stress.
M1-M5 Milestones	M1: Noise/Freq; M2: Power Integrity; M3: Event-Driven; M4: Stability Proofs; M5: Hierarchy.
BPTT	Backpropagation Through Time; adjoint sensitivity for transient optimization.
GMIN / Source Stepping	Robustness heuristics for find DC operating points in non-linear circuits.
Standard IR	Unified Intermediate Representations for Netlists, Timing (SDF), and Parasitics (SPEF).
LTE	Local Truncation Error; oversight of integration accuracy in transient analysis.

🏛 THE COGNITIVE FOUNDRY (VISION 2.0)
CuteApp Project Roadmap: Architecting the Cognitive Foundry
This roadmap evolves CuteApp from a standalone open-source simulator into an Agent-Centric Cognitive Foundry. It restructures the development path to prioritize System-Technology Co-Optimization (STCO), Neuro-Symbolic Verification, and Secure Silicon Enclaves, moving beyond simple tool automation to autonomous, physically-grounded design agents.
🧠 Cognitive Foundry Glossary Extensions
Term	Definition
STCO	System-Technology Co-Optimization; optimizing the entire vertical stack (die + package + software) rather than just the planar die (DTCO).
Neuro-Symbolic AI	A hybrid AI architecture combining neural networks (creative generation) with symbolic logic solvers (mathematical proof) to cure hallucinations.
TEE / Enclave	Trusted Execution Environment (e.g., Keystone, SGX); a hardware-encrypted memory region where open-source tools can securely process proprietary PDKs.
HBOM	Hardware Bill of Materials; a standardized digital ledger tracking IP provenance and compliance, critical for supply chain security (CISA framework).
STDF	Standard Test Data Format; the binary format for ATE (Automated Test Equipment) logs, used here to close the loop between silicon and simulation.
📅 Cognitive Milestone Calendar
    • Q1 (The Physics Core): Numerical authority, Tensor foundations, and WASM parity. (Phase 1)
    • Q2 (The Verifier Loop): Neuro-symbolic assurance, SVA synthesis, and "Chat-to-Chip" correctness. (Phase 2)
    • Q3 (Generative Analog): RL-based layout synthesis and migration agents. (Phase 3)
    • Q4 (The Cognitive System): STCO 3D partitioning, Thermal GNNs, and Secure Enclaves. (Phase 4-5)
⚖️ Cognitive Risk & Mitigation
Risk	Phase	Mitigation Plan
AI Hallucination	Phase 2	Adversarial Verification Loop: Generator (LLM) paired with Symbolic Verifier (SMT).
PDK IP Leakage	Phase 5	Secure Enclaves: Execution inside TEEs (Keystone/SGX) with no disk IO of decrypted data.
Thermal Runaway	Phase 4	Predictive GNNs: Thermal surrogates trained on low-fidelity FEM data for real-time floorplanning.

🏛 GLOBAL PROJECT GOVERNANCE
📅 Milestone Calendar (Target 2026)
    • Q1 (Stabilization): Complete Phases 0-2. Numerical authority established on x86/ARM/WASM.
    • Q2 (Design Flows): Complete Phases 3-5. Waveform 1.0, Structural Reuse, and Layout Prototype.
    • Q3 (Industry Grade): Complete Phases 6-9. PDK bindings, RF/Mixed-Sig, and Antigravity Deep Sync.
    • Q4 (Enterprise Orchestration): Complete Phases 10-14. Agentic EDA, VS Code UI, and Worker Drivers.
⚖️ Risk & Mitigation
Risk	Phase	Mitigation Plan
Numerical Divergence	Phase 0/2	Mandatory regression gates + M4 Stability Monitoring (Lyapunov).
PDK Encryption	Phase 6	"Ghost-mode" evaluate API; Sandbox model containers.
UI Fragmentation	Phase 11	VS Code–grade docking system serialization; Zero-jitter UI policy.
Orchestration Lag	Phase 12	gRPC-first worker drivers with local binary fallbacks.
🔗 Phase Dependencies
    • Phase 0 (Regression): Blocker for all solver modifications in Phase 1+.
    • Phase 1.1 (Tensor): Blocker for GPU acceleration in Phase 5.
    • Phase 11 (UI): Blocker for complex orchestration and marketplaces in Phase 12.

COMPLETED DEBUGGING WORK (FOUNDATION STABILIZATION)
These items are not cosmetic—they directly unblock Phases 0–3.
This section now includes explicit test cases, verification artifacts, and acceptance criteria.
Debug BJT Is Parameter Initialization
    •  Enforce explicit Is initialization during device instantiation
    •  Add sanity clamps for Is magnitude
    •  Verify stable DC operating point across temperature sweep Outcome: BJT convergence stabilized; Prevents silent divergence.
    •  Impact: Reduces simulation failure rate from ~15% to <1% in BJT-heavy analog designs.
    •  Acceptance Threshold: DC Node Voltage deviation < 1µV vs Xyce Golden Reference.
    •  Pass/Fail: Verdict FAIL if Residual Peak > 1e-12 or Iterations > 200.
BJT Stability Objectives
    •  Thermal Sweep Verification: Validated Is initialization at -55°C, 25°C, and 125°C to ensure zero-point safety.
    •  Property-Based Sensitivity: Built Fuzz-tests to verify $I_s$ stability across wide Beta parameter ranges.
    •  Operating Point Baseline: Established bit-exact DC benchmarks against NGSpice reference data.
How verified:
    • DC operating point verified via cross-comparing against NGSpice and Xyce across temperature range.
    • Numerical baseline outputs and tolerances defined in regression suite.
Acceptance criteria:
    • All runs must converge with residual < 1e-12 and stable Jacobian norms.
    • No silent divergence in 10,000 random test circuits.

Fix AI Assistant Context Error
    •  Correct serialization ordering of TensorNetlist state
    •  Ensure topology hash and node table included in every context packet
    •  Add version tag to AI context payloads Outcome: AI explanations align with simulation; Enables reliable future AI debugging.
AI Context Integrity Objectives
    •  Schema Semantic Versioning: Implemented versioned packets to prevent hallucination from format drift.
    •  Immutable State Hashing: Ensured every AI prompt is grounded in a signed topology hash for reproducibility.
    •  Context Serialization Audit: Verified ordering of TensorNetlist state to ensure parity between solver and assistant.
Verification:
    • Synthetic netlist sequences replayed to ensure context reconstruction yields identical topology hashes.
    • Integration tests validate AI explanations against solver logs.
Acceptance criteria:
    • No mismatches between context hash and solver event logs in >1,500 test circuits.

Build and Verify WASM Target
    •  Fix compiler flags for deterministic floating-point behavior
    •  Validate identical regression outputs between desktop and WASM
    •  Confirm solver, stamping, and probing correctness in browser Outcome: Trustworthy web frontend; Unblocks cloud execution.
Compiler & Platform Objectives
    •  FP Determinism Lockdown: Applied -ffloat-store and -fno-fast-math to guarantee cross-OS parity.
    •  Bit-Identical Regression: Validated 0-bit deviation between x86 desktop and WASM browser targets.
    •  SIMD Portability: Verified that solver stamping results are invariant to WASM SIMD vs fallback execution.
Verification:
    • Golden reference test runner for WASM includes CPU, GPU, SIMD variants.
    • WebAssembly System Interface (WASI) compliance tests.
Acceptance criteria:
    • Desktop and WASM regression suite have zero difference beyond 1 ULP.

Fix Darlington Pair Convergence (Physics Clamping)
    •  Implement physics-level voltage clamping (pnjlim-style)
    •  Add residual damping during Newton iterations
    •  Verify convergence across wide bias ranges Outcome: Complex multi-BJT structures converge reliably.
Physics Clamping Objectives
    •  PN-Junction Limiting (PNJLIM): Implemented standard physics-level voltage clamps to prevent numerical overflow in BJT junctions.
    •  Adaptive Residual Damping: Introduced Newton-Raphson damping that scales with the Jacobian condition number.
    •  Multi-Stage Bias Sweeping: Verified convergence across 10-decade current ranges for complex Darlington pairs.
Verification:
    • Coverage tests include common Darlington structures and worst-case bias networks.
Acceptance criteria:
    • All industry standard Darlington bias topologies converge in both DC and transient runs.

CONTEXT: PHASE GATES (SHIPMENT CRITERIA)
    • Phase 1 Ship: Bench + DC + Transient (No AI Autonomy).
    • Phase 2 Ship: AC + Noise + Corners + Trust Surface Layer.
    • Phase 3 Ship: Tapeout-Adjacent + PDK Containment.
🚦 PHASE GATES (SHIPMENT CRITERIA) Now with explicit exit conditions and quality gates:
    • Phase 1 Ship:
        ○ Exit conditions: DC, Transient, Bench results must pass regression suite (>99% tests) and real hardware benchmark simulations match within tolerance.
    • Phase 2 Ship:
        ○ Exit conditions: AC + Noise + Corners must converge without manual intervention and meets defined numerical authority thresholds.
    • Phase 3 Ship:
        ○ Exit conditions: Full tapeout-adjacent flows produce industry-compatible reports and model output matches reference tools.
📈 SHIPMENT QUALITY GATES (QUANTITATIVE)
Analysis Type	Test Count	Target Tool	Failure Category
DC Sweep	500+	Xyce/ngspice	BLOCKER: Any divergence > 1µV
Transient	200+	Spectre	BLOCKER: LTE violation > 1%
AC/Noise	150+	Eldo	WARNING: Mag drift > 0.1dB
Corners	100+	Golden DB	BLOCKER: Any Corner crash
🔬 Detailed Parametric Gates
    • AC/Noise Resolution: Tests must verify sweep density > 100 points/decade and verify Noise Spectral Density (NSD) integration bandwidth from 1Hz to 100GHz with <0.05% integration error.
    • Corner Definitions:
        ○ Temperature: Range $-40^\circ\text{C}$ to $+125^\circ\text{C}$ (Auto-grade).
        ○ Voltage: $V_{DD} \pm 10%$ (Digital), $V_{DD} \pm 5%$ (Analog Precision).
        ○ Process: TT, FF, SS, FS, SF (Global) + Monte Carlo $\sigma=3\dots 6$ (Local).

🧪 REVISED SHIPMENT GATES & QUALITY CRITERIA (COGNITIVE)
Phase	Gate / Criteria	New Requirement (Cognitive)
Phase 1	Numerical Authority	WASM Parity: 0-bit deviation between x86/ARM/WASM.
Phase 2	Verified AI	Formal Closure: 100% of AI-generated blocks pass auto-generated SVA checks.
Phase 3	Generative Analog	Migration Success: >90% of legacy 180nm IP ported to 130nm automatically.
Phase 4	STCO/3D	Thermal Safety: Partitioning agent predicts hotspots within 5% of FEM baseline.
Phase 5	Secure Bridge	PDK Isolation: Zero leakage of decrypted PDK data outside TEE RAM.

PHASE 0 — NUMERICAL FOUNDATION & REGRESSION AUTHORITY (IMMEDIATE)
Purpose: Lock numerical correctness, determinism, and regression safety.
0.1 Regression Suite Baseline (CRITICAL)
    •  Execute regression suite with update flag to generate initial .ref files (desktop)
    •  Expose update mechanism in WASM test runner
    •  Store Golden References in versioned regression_golden.db
    •  Add metadata: solver version, platform, precision mode (Implicit in DB signature & logs)
    •  Hash netlist + solver config → lookup expected results (Added signature field to DB)
    •  Enforce strict tolerances per analysis type (Implemented 1e-6 strict check)
    •  Fail build on any unapproved numerical drift (Exit code 1 on failure)
📊 REGRESSION AUTHORITY MATRIX
analysis	tolerance	Rationale
DC Volt	1µV	Precision needed for high-gain analog bias.
DC Curr	1pA	KCL residual floor for low-power leakage.
AC Mag	0.1dB	Dynamic range requirements for filter design.
Tran Peak	0.5%	1 ULP shift acceptable under Gear2 artifacts.
📊 Tolerance Industry Benchmarking
Analysis	cuteApp Goal	Spectre Equivalent	Rationale
Small-Signal AC	0.05dB Max Drift	reltol=1e-5	Filter passband ripple accuracy.
Transient Energy	< 0.1% Conservation Err	conservativ	Long-term oscillator stability.
Noise floor	< 1dB from theory	noise_reltol	LNA SNR estimation.
📂 regression_golden.db SCHEMA

{
  "traceId": "hash256",
  "metadata": {
    "solver_v": "0.4.2",
    "platform": "macos_arm64",
    "flags": "-O3 -ffp-contract=off",
    "timestamp": "2026-01-23T17:00:00Z"
  },
  "results": {
    "node_001": [0.707, 0.707, ...],
    "tolerances": {"absolute": 1e-6, "relative": 1e-4},
    "units": {"time_vector": "ns (double)", "voltage": "V (double)", "current": "A (double)"}
  },
  "analytics": {
      "trend_trigger": "Drift > 3-sigma over last 50 commits triggers email alert."
  }
}
0.1.1 Infrastructure Objectives
    •  Hierarchical Regression DB: Implementation of a structured database to store baseline results with metadata (compiler, flags, hardware).
    •  Cross-Platform Worker Agents: Deployment of automated regression compare nodes for x86_64, ARM64, and WASM targets.
    •  Trend Analytics: Capability to track numerical stability drift over multiple git commits with visual dash-plotting.
KPIs:
    • Maximum acceptable drift < 1e-6 per analysis type.
    • Daily regression runs with trend analysis.
Automated CI hooks:
    • Regression mismatch alerts with diff-based failure reports.

0.2 Backend: Sparse Linear Algebra Core (Foundations & Prototype)
Status: Locked Down
    •  CSR / CSC Sparse Matrix & Vector: Contiguous memory formats for AVX-512/GPU prefetching.
    •  Deterministic Ordering: Min-degree/Metis heuristics (no randomized pivoting).
    •  Numerical Kernel Contract: Allocation-free solve phase; Symbolic Factorization.
    •  M4: Formal Numerical Stability (Substrate)
    •  Energy-based Lyapunov monitors for nodal equations.
    •  Passivity checks for linearized device models.
📐 BACKEND PERFORMANCE BENCHMARKS
Metric	10k Node Target	100k Node Target	Context
Solve Time	< 100ms	< 2s	Single Newton iteration (Sparse LU).
Memory usage	< 200MB	< 1.5GB	Includes Matrix + SoA Tensors.
Throughput	1k iterations/s	50 iterations/s	Real-time interactive feedback target.
📐 Backend Definitions
    • Energy-based Lyapunov Monitoring: Calculation of $E(n) = \frac{1}{2} V^T C V$ at every step; check $E(n+1) - E(n) \leq \text{Source Power}$ to detect non-physical growth (instability).
    • Passivity Guarding: Local verification of $\operatorname{Re}{Y(j\omega)} \geq 0$ for linearized equivalents to prevent artificial gain-induced oscillation.
0.2.1 Core Kernel Objectives
    •  Granular Factorization Locking: Thread-safe symbolic factorization enabling multi-threaded matrix setup without global mutex stalls.
    •  IEEE-754 Rounding Lockdown: Enforcing strict round-to-nearest-even across all platforms to ensure bit-bit identity.
    •  Vectorized Kernel Dispatch: Logic to select between AVX-512, Neon, or generic C++ kernels at runtime.
    •  Future SIMD Architectures: Extension support for AVX2, AVX10-128, AVX10-256, and AVX10-512 instruction sets.
Verification:
    • Numerical stability tests including pivot threshold sweeps.
    • Benchmark: Solve times vs expected parity across distributed runs.

0.3 Backend: Flat MNA Prototype
Invariants
    •  Flat MNA Philosophy: Mathematically pure flattened representation.
    •  Invariant: Same netlist → same matrix → same result, bit-for-bit.
    •  Stateless Stamping: Parallelizable physics evaluation.
0.3.1 MNA Stamping Objectives
    •  Symbolic Elimination Pre-Sort: Deterministic node ordering to minimize fill-in before numeric stamping begins.
    •  Lock-Free Stamp Buffers: Implement thread-local SoA buffers for device stamping to eliminate write-contention.
    •  Stamping Checksum: Real-time hash verification of the Jacobian matrix to detect illegal concurrent writes.
Verification:
    • Benchmark against hierarchical MNA for consistency.
    • Inject synthetic noise and verify invariant identities hold.

0.4 GUI & Portability
    •  SimTrace Integration: GUI consumes stateless SimulationTrace API.
    •  macOS/Wasm Parity: Bit-exact math across GCC/Clang/x86/ARM.
    •  Introspection API: getCircuitState() → JSON with PDK-adaptable DRC/LVS rules.
🌐 getCircuitState() JSON SCHEMA (v1)

{
  "type": "object",
  "required": ["version", "topologyHash", "nodes"],
  "properties": {
    "version": {"type": "string", "pattern": "^\\d+\\.\\d+\\.\\d+$"},
    "topologyHash": {"type": "string", "description": "SHA-256 of canonical netlist"},
    "nodes": {
      "type": "array", 
      "items": {
        "id": {"type": "integer"},
        "v": {"type": "number", "unit": "Volts"}
      }
    }
  }
}
🧪 CROSS-PLATFORM TEST MATRIX
Facet	macOS (Clang)	Windows (MSVC)	WASM (Clang/JS)
DC Parity	Bit-Exact	Bit-Exact	Bit-Exact
Tran Parity	1e-12 Tol	1e-12 Tol	1e-10 Tol
SIMD Mode	Neon	AVX2	WASM SIMD128
0.4.1 Portability Objectives
    •  Parity Validation Harness: Automated tool to run same netlist on 3 platforms and verify 0-bit deviation.
    •  Versioning OpenAPI Spec: Formal definition of the getCircuitState() JSON schema, enabling external tool integration.
    •  State Snapshot / Restore: Ability to serialize the entire solver state (Jacobian + residuals) to disk for remote debugging.

PHASE 1 — TENSOR NETLIST & SCHEMATIC ENTRY (STRUCTURE AS DATA)
Purpose: Turn "a circuit" into a batchable, hashable, immutable object.
1.1 Backend: Tensor Architecture
    •  SoA (Structure of Arrays): Solver-integrated physics buffers (physics_tensors.h & circuitsim.h integration).
    •  Immutable Topology Hash: Structural sharing and isomorphism identification.
    •  Semantic Diffing: Connectivity-aware version comparison (components/netlist_diff.h).
1.1.1 Tensor Data Flow Objectives
    •  Index Map Optimization: Implement a static index mapping layer to route schematic IDs to tensor indices without lookups.
    •  Multi-GPU Context Manager: Logic for managing multiple GPU devices for massive multi-corner batching.
    •  Tensor Slicing Logic: Allow real-time inspection of device internals (Inner-Is, Vgs) via tensor slice views.
Architectural Theme: Standard IRs
    •  Unified Design Graph (UDG) IR: High-performance, versioned structural representation mapping schematic nodes ↔ netlist entries ↔ performance constraints.
1.2 GUI: Schematic Integrity & ERC
    •  Canonical Grid: Integer coordinate system for perfect alignment.
    •  Visual Terminal Snap: Guaranteeing no "near-miss" connections.
    •  Real-Time ERC: Visual blocking of simulation on floating gates or shorts.
1.2.1 Integrity Objectives
    •  Coordinate Space Formalization: Definition of EDA_UNIT to MICRO_INCH mapping for exact GDSII export.
    •  ERC DSL Runtime: A lightweight expression evaluator that runs the machine-readable rule definitions in real-time.
    •  Connectivity Graph Auditor: Post-edit background task verifying that every visual wire maps to exactly one netlist node.
Verification:
    • ERC rule coverage tests against known erroneous netlists.
1.3 AI API
    •  Placement API: placeComponent, moveComponent, rotateComponent.
    •  Routing API: placeWirePath(points[]).
1.3.1 Extended: Physics-Aware Routing & Optimization
    •  Placement Optimization: optimizePlacement(id) to reduce crossovers.
    •  Physics-Aware Routing: WirePhysics struct (current, freq, crosstalk).
    •  routeWithPhysics(): Routing with width/spacing constraints.
1.3.2 API Contract Objectives
    •  OpenRPC Schema Enforcement: Automatic generation of client/server glue from the central design intent JSON.
    •  Determinism Tokens: Every API call includes a sequence token to prevent race conditions during AI "Approval Loop".
    •  Error Propagation: Standardized 4xx/5xx style status codes for layout collisions or routing failures.
🛠 AI API Contract & Telemetry
Error Code	UI Feedback	Telemetry Metric
409_COLLISION	Red "bumping" animation on component	ui_layout_collision_count
422_ERC_FAIL	Modal toast with rule ID	erc_violation_rate
500_SOLVER_DIV	"Simulation Unstable" badge	solver_divergence_rate
🧬 Tensor Netlist Verification
    • Hashing Collision Resistance: Test suite processing 10M permutations of common sub-circuits must yield 0 SHA-256 collisions.
    • Semantic Diffing: API contract diff(netlistA, netlistB) -> {added: [], removed: [], modified_params: []} validated against 500 synthetic circuit pairs.
1.4 Graph Normalization & Canonicalization
Purpose: Ensure topology hashing is robust against node ordering and orientation.
    •  Canonical Node Relabeling: Stable node indexing based on topology traversal.
    •  Orientation-Invariant Subgraph Hashing: Detect functional blocks regardless of visual orientation.
    •  Explicit Ground Normalization: Rigid enforcement of Node 0 as the global reference.
🧬 TOPOLOGY CANONICALIZATION WORKFLOW
    1. GND Anchor: Start BFS traversal from Node 0.
    2. Distance Ranking: Assign rank $R = \operatorname{dist}(\text{Node}, 0)$.
    3. T-Sort: Sort nodes by $(R, \text{Connectivity Degree})$.
    4. Component Sequencing: Order components by their sorted terminal IDs.
    5. Hashing: Pipe bit-consistent component table to SHA-256.
🛠 AI API TRANSACTION MODEL
    • Transaction Lock: startTransaction(apiKey). No human edits allowed during AI run.
    • Atomicity: All placeComponent calls in a transaction are buffered; commit() runs ERC before stamping.
    • Rollback: rollback() restores the last valid topologyHash and MNA state.
    • Error Codes:
        ○ ERR_PDK_VIOLATION: Component parameters out of valid PDK range.
        ○ ERR_ERC_BLOCK: Suggestion creates a floating gate or VDD-VSS short.
        ○ ERR_COLLISION: Suggestion overlaps with existing visual obstruction.
1.4.1 Canonicalization Objectives
    •  BFS Traversal Proof: Formal verification that the relabeling algorithm is invariant to graph orientation.
    •  Persistent Hash Store: Schema for storing topology hashes as primary keys in the Simulation History DB.
    •  Sub-Graph Signature: Ability to hash nested sub-circuits independently for local reuse.
1.5 Simulation Modes (Three-Mode Architecture)
Purpose: Separate testing, encapsulation, and integration into enforceable simulation contracts.
Bench Mode
    •  Implement Bench Mode: Allows ideal V/I sources and dependent sources.
    •  Implicit Ground: Auto-inserted if missing.
    •  Use Case: Physics validation, unit testing, device experiments.
Block Mode
    •  Implement Block Mode: No independent sources at interface.
    •  Explicit PowerPort pins: Define requirements (VDD, GND, signals).
    •  Dependent sources: Allowed internally only.
    •  Use Case: Reusable subsystem design.
System Mode
    •  Implement System Mode: Only RailDrivers may inject net power.
    •  Explicit VDD/VSS symbols required.
    •  ERC enforces single-driver rails.
1.5.1 Mode Contract Objectives
    •  Behavior Matrix Auditor: Logic that prevents "Illegal Stamping" when a Bench-only source is used in System mode.
    •  Pre-condition Gates: Checklist-style gates that must pass (e.g., "Must have Ground if in System") before solve.
    •  Constraint Invariants: Mathematical invariants enforced per mode (e.g., $P_{net} = 0$ for Blocks).
Verification:
    • Mode compliance compliance tests with expected failure modes defined.
🔌 Mixed-Simulation Contracts
    • Bench ↔ System Interop: Explicit rule: Bench components (ideal sources) are auto-pruned when instantiated in System mode, replaced by RailDriver delivery.
    • PVT Sweep Behavior: In System Mode, PVT sweeps apply globally to all instanced blocks; local override parameters are ignored unless explicitly flagged lock_param.
Block Auto-Validation Harness
    •  Automatic Harness: Wrap Block with temporary ideal sources in Bench Mode.
    •  Sanity Checks: Run DC + Transient automatically.
    •  Strip harness before System integration.
    •  Automated Test Suite: tests/test_auto_harness.cpp (Analog/Digital/ERC coverage).
    •  DOM/Copilot Integration: /validate commands verified via tests/test_copilot_validate.cpp.
    •  Browser Verification: Launched WASM build and verified Copilot interaction via subagent.
1.6 Component Roles & Advanced ERC
Purpose: Prevent semantic leakage between modes.
Component Definitions
    •  PowerPort: Interface constraint only; never stamps MNA.
    •  RailDriver: System-only power injection abstraction (Battery/LDO).
    •  Ground Resolution: Bench (implicit) vs Block (explicit pin) vs System (VSS symbol).
ERC Enforcement Engine
    •  Rule: Independent sources forbidden in Block interfaces.
    •  Rule: PowerPorts cannot drive nets.
    •  Rule: System mode forbids internal power generation.
    •  Rule: Multiple rail drivers on same net is error.
    •  Feature: ERC severity levels (Error / Warning / Info).
    •  Feature: Visual highlighting of violations.
    •  Feature: Block simulation if ERC fails.
    •  Integrate ERC Unit Tests into suite.
    •  Implement Problems Panel UI (VS Code-style).
1.6.1 Enforcement Objectives
    •  Conflict Resolver: Logic to handle "Multiple Driver" errors with semantic ranking (e.g., VDD symbol beats Wire).
    •  Visual Annotation Layer: Dynamic overlay that colors nets based on their ERC status (Warning=Orange, Error=Red).
    •  Gate-Level Blocking: Prevention of the 'Simulation Start' signal if any 'Critical' ERC rules are violated.
1.7 Simulation Suite (VS Code-Grade, Cadence-Parity)
Purpose: Replace modal dialogs with workspace-oriented, profile-based simulation.
1.7.1 Simulation Workspace
    •  Create SimulationWorkspace dockable widget (replaces SimulatorWindow)
    •  Layout: Explorer (left), Profile Editor (center), Parameters (right), Results (bottom)
1.7.2 Simulation Profiles
    •  Define SimulationProfile struct (analysis type, mode, parameters, outputs)
    •  Hash-bind profiles to TensorBlock::topologyHash
    •  Default profiles: Quick DC, Transient, AC Sweep
1.7.3 Simulation Explorer
    •  Tree view: Quick Checks, Analyses, Sweeps, Corners, History
    •  Visual status badges (PASS/FAIL/STALE)
    •  Context menu: Run/Duplicate/Diff/Export
1.7.4 Mode-Aware Parameters
    •  Grey out invalid analyses based on current mode
    •  Block Run if ERC fails
    •  Context-aware parameter reduction (80% fewer fields by default)
1.7.5 Execution Layer
    •  Define SimulationRun struct (first-class run objects)
    •  Modes: Run (full), Quick (1 iteration), Resume, Incremental
1.7.6 Output Panel (Bottom Tabs)
    •  Tab: Problems (existing ERC panel)
    •  Tab: Output (console log, convergence trace)
    •  Tab: Streaming Log (real-time simulation progress)
    •  Tab: Results (named expressions tree)
    •  Tab: Waveforms (linked to probes)
1.7.7 Quick Preview
    •  Single Newton iteration with hard 100ms timeout
    •  Hover-triggered tooltip on probes (300ms delay)
    •  Yellow "≈" badge indicates approximate value
    •  Auto-invalidate on topology change
    •  Toggle: "Enable Quick Preview" in View menu
1.7.8 Schematic ↔ Simulation Coupling
    •  Emit topologyChanged(hash) signal on schematic edits
    •  Mark profiles STALE when hash changes
    •  Preserve "last valid run" state
1.7.9 Simulation Intent & Trust Surface (CRITICAL)
    •  Simulation Intent Model: Define Intent { Metric, Condition, Corner, Verdict } (e.g., "Gain > 60dB").
    •  Trust Surface Layer: UI explicitly shows why a result is trusted (e.g., "Converged via Newton-Raphson, Residual < 1e-12").
🛡 TRUST SURFACE CRITERIA THRESHOLDS
Result State	Criterion	Verdic
Trustworthy	Residual < 1e-12 AND Energy Delta < 1%	✅ PASS
Numerical Noise	1e-12 < Residual < 1e-9	⚠️ MARGINAL
Diverged	Residual > 1e-9 OR Singularity detected	❌ FAIL
    •  Method Disclosure: Log exactly which solver methods (Gear2, Trap) were chosen and why.
🗺 SIMULATION WORKSPACE WIREFRAME
    • Left Dock: Simulation Explorer (Run history + Profiles).
    • Center Top: Profile Configuration (Analysis specific params).
    • Center Bottom: Waveform Viewer (Hardware-accelerated plot).
    • Right Dock: Property Editor (Corner selection / Optimization goals).
🧩 USER FLOW: QUICK DC CHECK
    1. Hover over wire: Tooltip displays approx voltage ($300\text{ms}$ delay).
    2. Click Pin: Opens Transient Trace in side-tab (10 points preview).
    3. Right Click Results: "Export CSV" / "Send to Copilot" context menu.
1.7.10 AI Integration Hooks
    •  Expose getSimulationContext() in schema_api.h
    •  Include topology hash, failure reasons, convergence trace
    •  AI Constraint: "Raw Evidence First" — AI explains after solver proof is shown.
    •  AI Constraint: AI suggestions must cite specific log lines or data points.
    •  Status: 100% Complete. Build stabilized. Policy enforced in ContextEngine.
1.7.11 Telemetry & Outcome Objectives
    •  Stat Tagging Engine: Logic to label simulation results with descriptive performance tags (e.g., "Unstable", "High Gain").
    •  Solver Performance Telemetry: Tracking of Peak RAM, Iteration Counts, and LTE rollback frequency per run.
    •  Workspace Provenance: Linking every waveform back to the specific 

roadmap.md phase or commit hash that generated it.
1.8 Intrinsic Variation & Corner Architecture
Purpose: Build variation support into the core solver kernel immediately, not as a wrapper.
1.8.1 Foundation (Phase 1.7 Prerequisite)
    •  Define CornerConfig struct (temperature, voltage, process)
    •  Add corner selector to Simulation Toolbar
    •  Store corner sweeps as profile variants
1.8.2 Intrinsic Implementation (Active)
    •  Monte Carlo Hooks: Random seed injection at the solver level.
    •  PVT Tensor Architecture: Implemented applyCorner and environment-aware stamping in CircuitSim.
    •  Intrinsic Sensitivity: Extended calculateSensitivity with R/D/M/Q/C/L support.
1.8.3 Variation Scaling Objectives
    •  Weighted Corner Sampler: Implementation of biased sampling to test edges of the PVT space more frequently.
    •  Stress Mapping Schema: Correlation of corner configs to specific reliability risks (e.g., NBTI stress).
    •  Variant Persistence: Ability to DIFF two corner runs and see the numerical drift in the Results tab.

⚙️ PHASE 1 (EXT): THE PHYSICS CORE (DETERMINISTIC FOUNDATION)
Purpose: Establish the immutable "Ground Truth" for all future AI agents. Agents cannot reason without a physically accurate world model.
1.1 Numerical Authority & Regression (Cognitive Update)
    • Debug BJT Is Parameter Initialization: Enforce explicit $I_s$ initialization during device instantiation. (Metric: DC Node Voltage deviation < 1µV vs Xyce Golden Reference). Status: Stabilized.
    • Darlington Pair Physics Clamping: Implement PNJLIM (PN-Junction Limiting) to prevent overflow in high-gain stages. (Objective: Converge industry-standard Darlington bias topologies across 10-decade current ranges).
    • WASM Determinism Target: Compiler Flags: -ffloat-store and -fno-fast-math to guarantee x86/ARM/WASM bit-exact parity.
1.2 Backend: Sparse Linear Algebra & Tensor Architecture (Cognitive Update)
    • SoA (Structure of Arrays) TensorNetlist: Refactor circuitsim.h to use contiguous memory tensors.
    • GPU Readiness: Align data structures for coalesced memory access (128-byte transactions) to support future CUDA/WebGPU kernels.
    • Lyapunov Stability Monitors (M4): Calculate energy function $E(n)$ at every timestep. Check $E(n+1) - E(n) \le P_{in}$ to detect non-physical energy growth.
1.3 Intrinsic Variation & Corner Architecture (Cognitive Update)
    • Monte Carlo Hooks: Inject random seeds at the solver level for intrinsic variation.
    • PVT Tensor Architecture: Implement applyCorner logic to batch-process Voltage and Temperature vectors without rebuilding the matrix.

🤖 PHASE 1.25 & 1.26 — AI COPILOT & CONVERSATIONAL INTERFACE
Purpose: Turn the EDA tool into an AI-navigable partner.
1.25.1 Intelligence Layer
    •  Policy: AI is never the first explainer of a failure. Always paired with raw solver evidence. (Enforced in ContextEngine::applyPolicy)
    •  Contextual Awareness Engine: RAG system supplying LLMs with netlist state. (rag_context.h/cpp)
    •  Failure Taxonomy: Machine-readable failure types (FailureType, FailureLabel, FailureLabeler).
    •  Design Intent Recognition: GNN-based labeling of analog topologies.
    •  Copilot Panel: Integrated Chat Assistant (Completed).
    •  Structured Approval Loop: Review/Approve Change Requests with visual "Diffs".
    •  User Feedback Learning: Approval/rejection signals stored for model improvement (FeedbackStore).
    •  Confidence Explanation: AI shows why confidence < 100% (ConfidenceExplanation).
    •  NL-Command Mapping: Visual feedback for recognized design intents. (Confidence badge + explanation)
    •  AI-Driven Path Discovery: Copilot identifies signal chains (Input → Output) via /paths command.
    •  Intelligent Debug Suggestions: /debug command + DebugSuggestionsPanel widget for AI probe recommendations.
🧠 AI COPILOT ARCHITECTURE

ApproveRejectUser PromptCopilot EngineRAG Context LoaderTensor + Trace DBPrompt GeneratorPolicy ControllerLLM (Gemini)Approval Loop UITransaction CommitRollback

🛡️ AI GUARDRAILS & TOKEN STRATEGY
    • Token Chunking: Semantic splitting by Functional Block (e.g., "LNA Stage" as one chunk).
    • Priority Ranking: Signal Path Nodes > Bias Rails > Passive Parasitics.
    • Grounding Gate: LLM output must cite traceId and nodeId before displaying advice.
    • Hallucination Detection (External Benchmark): Use tests/ai/grounding_truth.json (500+ design edit cases) to validate AI grounding.
    • Explainability Requirement: Every AI suggestion MUST output a "Reasoning Chain" citing concrete solver evidence or log lines.
    • Strict Correctness Criteria: Topology modifications must maintain graph integrity (no dangling nets, zero illegal PDK parameters).
1.25.2 Grounding Objectives
    •  Grounding Token Validator: Logic to verify every AI design suggestion maps back to an existing schematic ID.
    •  Audit Trail Storage: Persistence of "Why AI Suggested This" reasoning linked to solver log timestamps.
    •  Context Sanitization: Filter out irrelevant netlist data to fit large circuits into finite LLM context windows.
1.26 AI + RAG + Copilot (Simulation Intelligence)
Step 1: Simulation Trace as Knowledge Object
    •  Define Schema (traceId, topologyHash, deviceMix, failureModes, waveforms, config).
    •  Chunking Strategy (Split trace into embeddable components) — TraceChunker with 4 strategies.
    •  Vector Store Stub (Interface for embedding interaction) — LocalVectorStore in-memory impl.
Step 2: Failure Taxonomy & Auto-Labeling
    •  Define Failure Vocabulary (Floating Node, Singular Jacobian, GMIN fallback) — FailureType enum.
    •  Automated Tagging Pipeline (Heuristic-based label injection) — FailureLabeler::labelFailures.
Step 3: Copilot "What-If" Engine
    •  Lightweight Speculative API (runSpeculative) — SpeculativeRunner class.
    •  Speculative Result Indexing (Store outcomes for RAG retrieval) — SpeculativeManager runs & chunks.
    •  "What-If" Prompt Engine (Implemented PromptBuilder & AlgoAnalysis) — PromptBuilder::buildWhatIfPrompt.
Step 3.4: Wasm Verification & Bug Fixes
    •  Fix Probe Bounding Box (VisualComponent).
    •  Implement Smart Probe Placement (Pathing Logic).
    •  Fix Wire Routing (Terminal-to-Terminal L-Shape).
    •  Update Darlington Template (Fix Biasing).
    •  Add /demo and /help commands.
Step 4: AI Context Representation Layer
    •  Serialize topology, device tables, and DC/Transient endpoints into structured chunks.
    •  Inject probe values and historical error metrics into prompt context.
    •  Incorporate solver decisions and flags for grounding.
Step 5: Topology-Aware Prompt Compression
    •  Functional Block Identification (Amplifier, Bias, Feedback stage).
    •  Pathway Summarization (Signal chain extraction).
Step 6: Evaluation, Reranking & Feedback Loop
    •  Cross-encoder reranking for context relevance.
    •  Reinforcement Learning (RL) integration: User feedback signals (helpful/not) used for discriminator fine-tuning.
    •  RAG context updates based on successful outcomes.

🧭 PHASE 1.3 — SPATIAL INTELLIGENCE & RELATIVE PLACEMENT
Purpose: Move from pixel-based placement to semantic and relational layout.
Backend: Geometric Reasoning
    •  Obstruction Awareness: API-level collision detection (collidesWithAny).
    •  Relational Positioning API: placeNextTo, alignWith, and findClearSpace.
    •  Semantic Context Generator: Describe spatial relationships in the AI Prompt.
    •  Spatial Placement Logic: Auto-relocate templates to clear space when origin is blocked.
1.3.1.1 Geometric Intelligence Objectives
    •  Semantic Collision Engine: Detect and score layout violations based on component type and net sensitivity (e.g., sensitive LNA inputs get higher penalty near high-swing nets).
    •  Weighted Proximity Graph: Dynamic graph where edges represent electrical connectivity and spatial distance for AI placement optimization.
    •  Congestion Analytics: Predict potential routing bottlenecks before wires are placed during the design loop.
Backend: Physics & Simulation
    •  BJT Implementation: Complete
    •  Investigate convergence failures (bridge_rectifier, darlington_pair) @1.4
    •  Verify clamp logic in device_physics.h @1.4
    •  Ebers-Moll Model in device_physics.h.
    •  Support for per-instance parameters (Beta, Is) in circuit.h.
    •  KCL Residual Audit (Physical Conservation).
    •  Sensitivity Analysis (dV/dIs, dV/dBeta).
    •  Newton-Raphson Voltage Limiting (pnjlim for BJT).
    •  LTE Monitoring for adaptive time-stepping.
    •  Inductor Implementation: Transient stamping + Mutual Inductance (L-matrix inversion).
    •  Schottky Diode: Backend struct + physics + solver integration.
    •  Standard Cell Library: Refactoring ICs to use PDK primitives (standard_cells.h).
GUI: Visual Feedback
    •  Visual Component Library: Improved painting for MOSFETs, BJTs (3-terminals with X indicators), Inductors, and Current Sources.
    •  Create components: Schottky Diode, Current Source.
    •  Placement Pre-Flight: Visual indicator of collisions during the "Approval Loop".
    •  Geometric Constraints: Sticky alignments and distribution helpers triggered by AI commands.

⚙️ PHASE 2 — NUMERICAL FOUNDATION & PROBE SYSTEM (SPICE-GRADE ACCURACY)
Purpose: Achieving commercial-grade robustness for nanometer differential equations.
Backend: Numerical Hardening & M1/M4
    •  Newton-Raphson Solver: Residual tracking and Jacobian conditioning.
    •  Automatic Differentiation: Code generation for Jacobians from device_physics.h.
    •  Dual Number Library (autodiff/dual.h).
    •  Templated Physics Kernels (physics/device_physics.h).
    •  Integration into Stamping (physics/circuitsim.h).
    •  Differentiable Physics: $\nabla_p V$ for gradient-based tuning.
    •  NASA Hardening: GMIN/Source stepping; Per-variable ABSTOL/RELTOL.
    •  M1: Noise Analysis (Frequency Domain): Thermal noise (kT/C), Shot noise, Flicker (1/f).
    •  Spectral density calculation via linearized MNA.
    •  M4: Numerical Stability Proofs: Automatic timestep rollback on energy growth detection.
    •  Convergence justification engine (Why it solved/failed).
2.0.1 Measurement Objectives
    •  Noise Confidence Sentinel: Calculation of statistical variance for noise results to ensure frequent domain convergence.
    •  Probe Latched Semantic Model: Logic that ensures probes "stick" to the logical center of wires even during drastic schematic reflows.
    •  Automatic Derivative Audit: CI task verifying Jacobian derivatives against finite-difference reference models.
🎯 PHASE 2 SUCCESS CRITERIA
    • Jacobian Accuracy: 100% of device models verified bit-exact against analytic derivatives using Dual numbers.
    • Timestep Integrity: LTE step rejection rate < 5% on industry-standard transient testbenches.
    • Measurement Authority: DC/Transient probe values match reference tools within 1e-6 relative tolerance.
GUI: Measurement & Bias Points
    •  Interactive Test Points: Dedicated "Probe" component for user-defined measurement nodes.
    •  Fixed: Probes now correctly latch to wires and participate in the netlist.
    •  Direct Annotation: Render DC Operating Points (Voltages/Currents) on the schematic.
    •  Dynamic Probe Sync: Waveform viewer and logs automatically update based on named probes in the schematic.
    •  M1: Noise Visualizer: Noise spectral plots and integrated noise dashboards.
Phase 2.5: Draggable Probe Tips (Oscilloscope Style)
    •  Architecture: Implement ProbeTip class as moving child items of VisualComponent.
    •  Visuals: Render virtual dashed wires between body and current tip positions.
    •  Superposition Law: Class SuperpositionSolver to decompose multi-source linear circuits.
    •  Source Decomposition: Validate V_total = Sum(V_i) for each independent source.
    •  Interaction: Ensure tips snap to grid and update visuals in real-time.
    •  Connectivity: Implement tip-to-wire intersection detection in drawForeground.
    •  Netlist: Probes now attach AFTER all components stamped, using wire endpoint node IDs.
    •  Verification: Probes no longer inflate node count or cause matrix crashes.
Phase 2.6: Debug Logging & AI Integration
    •  crash_report.h: Created with CrashReport, ProbeDebugInfo, ComponentDebugInfo structs.
    •  context_engine.cpp: Injects crash context into AI copilot prompts.
    •  Crash logs saved to: ~/.beaversim/crash_logs/.
AI API
    •  runNoise(fStart, fEnd, points) → trace.
🛠 PHASE 2.8–2.10 — ANALOG VLSI HARDENING & ANALYTICS
Phase 2.8: Analog VLSI Solver Hardening (Tensor-Ready)
    •  Linear Solver Policy Upgrade: Resolve application segfault (Qt conflict), Identify non-symmetry in solvePCG.
    •  Implement solveLU_Pivoted in linalg.h (Standard Doolittle/Crout).
    •  Integrate LU fallback in CircuitSim::stepTransient for small matrices.
    •  GPU-Ready Physics Kernels: Refactor bjt_ebers_moll for branchless Linear Continuation.
    •  Ensure Dual gradient stability across high-voltage regimes.
    •  Calibrate $V_t$ and $I_s$ precision for VLSI high-gain stages.
    •  Convergence Recovery (Damping): Implement pnjlim (PN-Junction Limiting) in Newton-Raphson loop.
    •  Implement "Tensor-Batch" Gmin-stepping for singularity recovery.
    •  Verification: Verify LU-PCG parity for linear circuits ($10^{-12}$ tolerance).
    •  Validate Active Darlington Pair convergence.
    •  Thermal Stability Sweep ($ -2mV/^\circ C$ validation).
    •  Thermal & SoA Refactor: Implemented DiodeTensor, MosfetTensor, and stampSoABlock.
    •  Reorganized all tests into a dedicated /tests folder.
Phase 2.9: Accuracy & Numerical Authority (Silent Failure Elimination)
    •  Residual Provenance Tracking: Implement ResidualContribution struct to track per-device KCL and Jacobian impact.
    •  Rank-Deficiency Detection: Symbolic and numeric detection of disconnected components or voltage source loops.
    •  Auto-Regularization: Intelligent local GMIN injection where stability is decayed.
    •  Device-Level Energy Accounting: Track instantaneous power and integrate energy per device to detect non-passive artifacts.
2.9.1 Calibration & Tolerance Objectives
    •  Per-Device Tolerance Pools: Ability to set independent RELTOL/ABSTOL values for critical analog sub-systems vs non-critical logic.
    •  Silicon Data Importer: Parser for standard silicon measurement vectors (CSV/VCD) to drive the calibration engine.
    •  Least-Squares Optimizer: Implementation of an automated loop to minimize model-vs-silicon deviations.
Phase 2.10: ASIC Simulation Analysis Suite (Tapeout-Grade)
Purpose: Complete set of SPICE analyses required for silicon-ready analog ASIC tapeout.
AC Analysis (Frequency Domain)
    •  Small-Signal AC Analysis: Linearize around DC operating point
    •  Frequency Sweep: Log/linear sweep from mHz to THz range
    •  Transfer Function: H(s) = Vout/Vin computation
    •  Bode Plots: Magnitude (dB) and Phase (degrees) visualization
    •  Stability Metrics: Gain margin, phase margin, crossover frequency
    •  Input/Output Impedance: Zin(f), Zout(f) calculation
    •  CMRR/PSRR: Common-mode/power-supply rejection ratio vs frequency
Noise Analysis
    •  Thermal Noise: 4kTR Johnson-Nyquist noise sources
    •  Shot Noise: 2qI for junction devices (diodes, BJTs)
    •  Flicker Noise (1/f): KF/AF parameters for MOSFETs
    •  Noise Spectral Density: V²/Hz and A²/Hz per node/branch
    •  Integrated Noise: RMS noise over bandwidth
    •  Noise Figure (NF): For RF amplifier chains
    •  Equivalent Input Noise: Referred to input for SNR calculation
Harmonic Distortion Analysis
    •  THD (Total Harmonic Distortion): Sum of harmonic power ratio
    •  HD2, HD3, ... HDn: Individual harmonic components
    •  Intermodulation Distortion (IMD): Two-tone IM3, IM5
    •  1dB Compression Point: P1dB for amplifier linearity
    •  IP3 (Third-Order Intercept): IIP3/OIP3 calculation
    •  FFT Engine: High-resolution spectral analysis
PVT Sweep Automation
    •  Process Corners: TT, FF, SS, SF, FS matrix generation
    •  Voltage Sweep: VDD ± 10% (or PDK-specific range)
    •  Temperature Sweep: -40°C to +125°C (mil-spec) or custom range
    •  PVT Matrix Runner: Cartesian product of P×V×T combinations
    •  Corner Summary Report: Min/Max/Typical across all corners
    •  Worst-Case Identification: Auto-detect limiting corner per metric
Pole-Zero Analysis
    •  Pole Extraction: Dominant and non-dominant poles
    •  Zero Extraction: RHP zeros (stability concern)
    •  Pole-Zero Plot: s-plane visualization
    •  Stability Assessment: Pole proximity to jω axis
    •  Root Locus: Parametric pole movement (optional)
Sensitivity Analysis
    •  CI/CD Regression: Automated sensitivity regression tests in test_sensitivity.
    •  DC Sensitivity: ∂V/∂R, ∂V/∂β, ∂V/∂Vth
    •  AC Sensitivity: ∂|H(jω)|/∂param, ∂∠H(jω)/∂param
    •  Transient Sensitivity: ∂waveform/∂param (adjoint method)
    •  Gradient Extraction: For optimization loops
    •  Sensitivity Ranking: Pareto of most impactful parameters
Device Mismatch Modeling
    •  σVth Mismatch: Threshold voltage variation for matched pairs
    •  σβ Mismatch: Current factor variation
    •  σR Mismatch: Resistor matching for ratio accuracy
    •  Layout-Dependent Effects: Proximity, STI stress
    •  Common-Centroid Analysis: Mismatch reduction validation
Reliability Simulation
    •  Electromigration (EM): Current density vs Black's equation limits
    •  Hot Carrier Injection (HCI): Degradation modeling for short-channel
    •  NBTI/PBTI Aging: Threshold shift over time
    •  Time-Dependent Dielectric Breakdown (TDDB): Gate oxide stress
    •  Self-Heating: Thermal resistance network, Rth extraction
    •  Mean Time To Failure (MTTF): Statistical reliability metrics
S-Parameter Analysis (RF)
    •  2-Port S-Parameters: S11, S12, S21, S22
    •  N-Port Extension: Multi-port networks
    •  Smith Chart Visualization: Impedance matching
    •  Touchstone Export: .s2p, .s4p file generation
    •  Transmission Line Models: T-line, coupled lines
AI API for Analysis
    •  runAC(fStart, fEnd, points, sweepType) → ACTrace
    •  runNoise(fStart, fEnd, points, outputNode) → NoiseTrace
    •  runDistortion(fundamentalFreq, harmonics) → THDReport
    •  runPVT(corners[], voltages[], temps[]) → PVTMatrix
    •  extractPoleZero(inputNode, outputNode) → PZResult
    •  runSensitivity(param, analysisType) → SensitivityReport
    •  runReliability(stressConditions, duration) → ReliabilityReport
🧪 ANALYSIS VERIFICATION TARGETS
analysis	Success Criterion	Reference Tool
Noise	Spectral density drift < 0.2dB	Spectre PNOISE
Distortion	HD2/HD3 within 1dB of theory	Matlab / Eldo
PVT	No Corner-Case crashes in 125 corner set	Industry LVS check

📊 PHASE 3 — TRANSIENT ANALYTICS & WAVEFORM VISUALIZATION
Purpose: Turn raw numbers into engineering insight.
Backend: LTE, BPTT & M3
    •  LTE Step Control (Research-Grade): Weighted RMS norm; per-device error attribution.
    •  Adaptive Timestepping: Per-corner $dt$ adaptation.
    •  Method Arbiter: Automatic Trap → Gear2 switching.
    •  BPTT: Adjoint sensitivity recording for transient optimization.
    •  M3: Event-Driven Simulation: Multi-rate timestep controller, Event-triggered relinearization.
GUI: Pro-Grade Analytics
    •  WebGPU Waveform Viewer: Hardware-accelerated rendering of millions of points.
    •  Log/Linear Axes: Professional engineering scale support.
    •  Stability Dashboard: Energy vs Time plots for numerical proof.
    •  AI-Driven Unit Testing: Generate unit tests for new components automatically.
3.1 Analytics View Objectives
    •  Multi-LOD Waveform Tiling: Tiled memory model for hardware-accelerated zoom across million-point datasets.
    •  Energy Stability Sentinel: Real-time monitor integrated into the waveform viewer showing energy conservation violations.
    •  Delta-Comparison Overlay: One-click UI for overlaying two different simulation results for drift analysis.
Phase 1.0.5: Analog Designer MVP (LNA / OpAmp / Bandgap Flow)
Focus: Reference Designs & Golden Verification
    •  Reference Design Library (components/reference_designs.h):
    •  LNA (Low Noise Amplifier): Cascode with inductive degeneration.
    •  OpAmp: Two-stage Miller-compensated OTA.
    •  Bandgap Reference: Brokaw cell.
    •  Simulation Profiles: Pre-configured testbenches (AC, Tran, Noise) for each design.
    •  Golden Verification (tests/golden_verification.py): Strict tolerances (DC 1µV, AC 0.1dB, Phase 0.5°), trust badges.
    •  AI Generation Hook (ai_api/context/rag_context.h): ReferenceDesignProvider for few-shot learning.
Phase 1.1: Frontend - "The Visual Workspace"
Focus: Statistical Analysis & Yield Optimization
1.1.1 Backend: Statistical Foundations
    •  Statistical Engine (physics/statistical_engine.h):
    •  Distributions: Gaussian, Uniform, Lognormal.
    •  Samplers: Random (Monte Carlo) and Latin Hypercube (LHS).
    •  MonteCarloRunner: Parallel batch execution.
    •  Sensitivity Engine (physics/sensitivity_analysis.h):
    •  Perturbation Logic: Finite Difference ($/ \Delta p$).
    •  Normalization: $S = \partial V / \partial p \cdot (p/V)$.
1.1.3 Refinements (Polishing)
    •  UI Integration:
    •  Dock StatisticalDashboard in SimulationWorkspace (Stack with Results/Waveforms).
    •  MonteCarloSetupDialog: Modal for N-runs, Strategy (LHS/Random), Metric.
    •  Performance & UI Flow:
    •  Threading: Run MonteCarloRunner::runBatch in background thread via QtConcurrent::run.
    •  Progress dialog integration (QProgressDialog).
    •  Dock Integration: Refactor SimulationWorkspace into a Sidebar Dock (VS Code style).
    •  Output Routing: Ensure logs/streaming route to bottom docks.
    •  Build Stabilization:
    •  Fix DebugSuggestionsPanel linker errors in all targets.
    •  Interactivity:
    •  Link SensitivityPareto click → SchematicEditor::highlightComponent.
    •  Data Export:
    •  CSV Export button in Dashboard.
    •  Monte Carlo Refinement:
    •  Advanced MonteCarloSetupDialog with per-component tolerances.
    •  Target metric selection (Probes/Nodes).
    •  Sampling strategy selection (Random vs LHS).
1.1.2 Frontend: Dashboards
    •  Statistical Dashboard (gui/analysis/statistical_dashboard.h):
    •  Yield Histogram: Binning, Gaussian fit, Pass/Fail zones.
    •  Sensitivity Pareto: Ranking bar chart with schematic linking.
    •  Monte Carlo Analysis: Distributed Sampling (Cloud Dispatcher) placeholder.

🧪 PHASE 4 — TOPOLOGY-HASH & STRUCTURE REUSE (GAME ADVANTAGE)
Purpose: Graph-aware performance gains and M5.
    •  Reduced Graph Hashing: Isomorphism identification across blocks.
    •  Factorization Cache: Shared symbolic plans across regions/sweeps.
    •  GNN Parasitic Estimation: Predicted $C_{\text{parasitic}}$ from topology.
    •  M5: Hierarchical & Incremental Re-Simulation: Block-level dependency tracking.
    •  Incremental Newton updates for local design edits.
🎯 PHASE 4 SUCCESS CRITERIA
    • Graph Isomorphism: 100% detection accuracy for identical sub-circuit topologies across disparate files.
    • Factorization Speed: >40% reduction in symbolic factorization time for incremental schematic edits via cache reuse.
    • Memory Efficiency: <10% overhead for hierarchical dependency tracking.
Backend: Hierarchical Serialization
    •  Next: Validating RTL Design Flow <>
    •  Verify Code Editor <-> Schematic sync.
    •  Test Synthesis placeholder stage.h abstraction level.

PHASE 5 — LAYOUT CO-DESIGN & GPU ACCELERATION
Purpose: Unify schematic, layout, and simulation on the GPU with M2.
Backend: Acceleration & M2
    •  Stateless Physics Kernels: Extract pure device physics into stateless, inline functions (device_physics.h).
    •  Tensor Data structures: Refactored circuitsim.h to use typed SoA tensors.
    •  GPU Physics Shaders: SIMT kernels in WGSL/CUDA.
    •  Deterministic Reductions: Colored coloring graph algorithms.
    •  M2: Power Integrity (PI) Engine: Multi-port power network impedance (Z(f)) extraction.
    •  Ldi/dt dynamic droop modeling for complex PDNs.
🎯 PHASE 5 SUCCESS CRITERIA
    • Numerical Parity: GPU vs CPU result deviation < 1e-12 across all precision modes.
    • Throughput KPI: >10x speedup for 1M node circuits on Metal/Vulkan/WebGPU vs single-threaded CPU.
    • Energy Audit: 100% conservation law compliance in all WGSL/CUDA physics kernels.
Architectural Theme: Hardware-Accelerated Solvers Beyond WASM
    •  Large-Scale GPU Solver Core: Dedicated CUDA/Metal/Vulkan/WebGPU kernels optimized for sparse matrix operations in 1M+ node problem sets.
    •  Automatic Solver Steering: Intelligent fallback logic that selects between WASM, Native CPU, and GPU based on circuit complexity and available hardware.
    •  WebGPU/WGSL Integration: Direct access to local GPU hardware from WASM for high-performance browser-based simulation.
GUI: Electro-Thermal & PI Visuals
    •  Multi-Physics Overlays: IR drop and PDN Droop (Time/Freq) overlays.
    •  Z(f) PDN Plots: Frequency-domain supply network impedance visualizations.
🔁 Phase 5.9: Physical Feedback Loop (M2/M5 Bridge)
Purpose: explicit feedback mechanism for Layout ↔ Simulation.
    •  Extraction Loop: OpenROAD/KLayout SPEF extraction automation.
    •  Tensor Mapping: SPEF → TensorNetlist mapping for parasitic injection.
    •  Re-Run Triggers: Auto-trigger IR/Noise/Delay analysis on layout update.
    •  Trust Surface Deltas: Visual diff of Pre-Layout vs Post-Layout simulation results.
    •  AI Physical Grounding: AI suggestions must reference physical coordinates (x,y) and extracted nets.

PHASE 5.5 — HDL & CUSTOM DEVICE DEFINITION
Purpose: Enable user-defined devices without recompilation.
Implementation
    •  Real-time Verilog-AMS parsing.
    •  Synthesize HDL into TensorNetlist blocks.
    •  Generate behavioral or physics-backed kernels.
Integration
    •  HDL blocks usable in Bench, Block, and System modes.
    •  Subject to same ERC and regression rules.
    •  HDL GUI Integration: Split-pane view (VS Code style) for side-by-side Schematic/HDL editing.
    •  Schematic-HDL Linker: "HDL Schematic Conversion" toolbar button triggering a detection popup to auto-link HDL files to schematic blocks (Test Bench/System) with bi-directional updates.

🏭 PHASE 6 — PDK INTEGRATION, INDUSTRY COMPLIANCE & RTL→GDS FLOW
Purpose: Generic foundry-agnostic PDK integration. Critical Goal: Ensure strict adherence to PDK specifications across all schematic types (Test Bench, Block, System Integration). Every project must define a valid Die Area / PCB constraints in System Mode to enable Yosys → OpenROAD → KLayout flow.
6.10 Supported PDKs (Reference Implementations)
PDK	Node	Status	Devices
SKY130	130nm	Reference	Full device set
GF180MCU	180nm	Planned	Core + I/O
IHP SG13G2	130nm BiCMOS	Planned	MOSFET + SiGe HBT
ASAP7	7nm (academic)	Planned	FinFET
Extensibility: New PDKs added by creating manifest + device mapping files.
Architectural Theme: Standard Intermediate Representations
    •  Physical IR Standardization: Implementation of shared schemas for Netlists, Parasitics (SPEF), Timing (SDF), and Physical geometry to allow seamless tool exchange.

📈 PHASE 7 — ADVANCED ANALYSIS & AUTOMATION (COMPETITIVE EDGE)
Purpose: Scripting, optimization, and specialized analyses.
7.1 Scripting & Automation
    •  Python API (PyAcute): Headless control of simulator.
    •  Tcl Command Interface: Legacy compatibility.
    •  Job Definitions: YAML/JSON multi-job orchestration.
    •  Automatic Reporting: PDF/HTML generation.
    •  Integrated Script Editor: Ability to open Python/Tcl automation scripts in tabs adjacent to schematics, with full File Explorer integration.
🛠 Script Engine Validation
    • Reference Parity: Python API calls sim.run_ac(...) must match GUI-triggered execution bit-for-bit.
    • Coverage: 100% of internal Solver:: public methods must be exposed to Pybind11.
    • Benchmark Suite: tests/scripting/bench_api.py measuring overhead per call (<10us).
7.2 Advanced Analyses
    •  Yield/Sensitivity: Monte Carlo with ML acceleration.
    •  Transfer Function: Pole-Zero extraction.
    •  RF Analysis: S-Parameters, Smith Charts, Stability.
    •  Periodic Steady State (PSS): RF/Switched-Cap analysis.
7.3 Design Optimization
    •  Optimizer Engine: Gradient-based and heuristic solvers.
    •  Goal-Based Tuning: "Maximize Gain s.t. Phase Margin > 60".
    •  Component Sizer: Automated transistor sizing.
🎯 PHASE 7 SUCCESS CRITERIA
    • API Coverage: 100% of simulator actions (Run/Probe/Export) accessible via headless Python/Tcl scripts.
    • Optimization KPI: Automated sizer achieves "Target Metric" within 5% of global optimum in < 50 iterations.
    • Automation Latency: Script execution overhead < 10ms per command.

PHASE 8 — MIXED-SIGNAL & MULTI-DOMAIN
Purpose: Co-simulation with digital logic and adjacent domains.
8.1 Mixed-Signal Co-Simulation
    •  Event-Driven Digital Engine: Logic primitives (NAND, DFF) solved efficiently.
    •  A/D Interface Elements: DAC/ADC bridges with impedance modeling.
    •  Verilog-AMS Support: Behavioral modeling for mixed domains.
    •  SystemVerilog DPI: Co-sim with external logic simulators (Verilator/Icarus).
    •  Hybrid Simulation Coordinator: Orchestration of sparse (analog) and dense (digital) solvers including Cloud + Local hardware distribution.
    •  Multi-Rate Synchronization: Advanced time-step control for synchronizing fast digital clocks with slow analog thermal time constants.
8.2 Interoperability
    •  Layout Integration: GDSII import for overlay/extraction.
    •  Standard Formats: DSPF (parasitics), Touchstone (RF), LEF/DEF.
    •  Tool Bridging: Export to Keysight ADS / Cadence Virtuoso formats.
Architectural Theme: Hierarchical Simulation Orchestration
    •  Simulation Meta-Engine: Central coordinator capable of driving multiple disparate simulators (e.g., Spectre, Xyce, and Digital Verilator) in a single hierarchical context.
    •  Hierarchical Signal Convergence: Algorithms for synchronizing analog and digital time-steps across domain boundaries.
🧩 8.1.1 Mixed-Signal Co-Simulation Semantics (Verilog-AMS)
    • Connect Modules: Automatic insertion of L2E (Logic-to-Electrical) and E2L (Electrical-to-Logic) interface elements with user-defined $V_{th}$ and $t_{rise/fall}$.
    • Scheduling Model:
        1. Analog Master: Sparse solver generally dictates time-step.
        2. Digital Slave: Verilog kernel (Verilator/Icarus) steps forward to match Analog $t_{now}$, processing delta-cycles.
        3. Backtrack: If Analog rolls back (LTE reject), Digital kernel must support state restoration (snapshotting).
    • Compliance Suite: Full pass required on Accellera Verilog-AMS 2.4 behavioral assertions (cross, timer, transition).
    • Scheduling & Event Ordering Verification:
        ○ Deterministic Sequencing: Test harness to verify event ordering across A/D boundaries is invariant.
        ○ Rollback Integrity: Verify state restoration (snapshotting) after LTE rejection is bit-exact.
        ○ Delta-Cycle Handling: Explicit verification of zero-delay discrete event propagation.
    • Mixed-Signal Benchmark Suite: Standard test cases (e.g., 8-bit SAR ADC + Digital Control) to verify accuracy/stability.
🎯 PHASE 8 SUCCESS CRITERIA
    • Sync Integrity: Zero synchronization-induced convergence failures in A/D bridge benchmarks.
    • Perf Ratio: Mixed-signal runtime < 2x of standalone digital simulation for typical SoC partition (10% analog, 90% digital).
    • Domain Parity: 100% matching of Verilog-AMS behavioral outputs vs structural analog equivalents.

PHASE 9 — ANTIGRAVITY COPILOT DEEP INTEGRATION
Purpose: Full-circle integration of the AI as a primary engineering partner.
Backend & Cognitive Layer
    •  High-Throughput State Bridge: Real-time JSON streaming of delta-changes.
    •  AI-Native Contextual Awareness (RAG): Ingest "Numerical Constitution" and physics kernels.
    •  AI Semantic Knowledge Base: Procedural rules for simulation.
    •  AI Tooling Extensions: @simulate, @compare, @sensitivity, @conditionEstimate, @python.
    •  Tool-Integrated Prompt Shortcuts: @netlist, @lastTrace, @failureTags, @deviceDocs.
    •  Closed-Loop Interaction & Agency: Secure "Command Execution" handshake.
🧠 Hallucination Detection & Fail Criteria
    • Reference Benchmark: tests/ai/grounding_truth.json containing 500 prompts with known-correct topological edits.
    • Fail Condition: >0% allowance for "phantom node" connections (connecting to non-existent ID).
    • Connectivity Audit: Post-generation graph traversal verifying all AI-touched nets have >1 terminal.
🎯 PHASE 9 SUCCESS CRITERIA
    • Hallucination Rate: <5% false connectivity reports (verified via grounded graph audit).
    • Context Latency: AI design intent recognition < 3s for 1k node circuits.
    • Approval Loop: 100% atomic rollback for design changes rejected by the user.
GUI: Antigravity Interaction
    •  Context Chips / Tagging UI.
    •  Copilot-Plan Previews: Side-by-side diff approval workflow.
    •  Connection Status: "Active/Syncing/Offline".
    •  Trace & Failure Citations.
    •  Predictive Visual Feedback (Ghosting).
    •  Relational Hinting: Draw arrows between nodes.
    •  One-Click Repair: Add a button for AI-identified convergence failures.
    •  AI Focus Mode: Visual "spotlight".
    •  Rich Thought Streaming: Markdown, KaTeX, Mermaid.
    •  Wait Indicators.
Phase 9.1: Connectivity & Layer
    •  Google OAuth2 Integration.
    •  Dynamic Gemini Client (Model Discovery, Pro Tiering).
    •  Google Search Grounding: "Web Search" tool for datasheets.
    •  Secure Token Vaulting.
    •  Antigravity Payload Injector.
Phase 9.2: AI Guardrails & Confidence
    •  Confidence Metrics: Quantitative "Certainty Score" for every AI suggestion.
    •  Formal Guardrails: Pre-computation checks (ERC, connectivity) before showing AI plans.
    •  Hallucination Detection: Cross-reference AI output with netlist ground truth.
Phase 9.3: AI Authority Levels
Purpose: Prevent unauthorized AI edits via tiered permissions.
    •  Level 0 (Explain Only): AI analyses traces/logs but CANNOT edit netlist. (Default for System Mode).
    •  Level 1 (Suggest): AI generates "Ghost" plans requiring explicit user Commit.
    •  Level 2 (Sandbox): AI can auto-edit inside a Bench Mode sandbox or temporary branch.
    •  Level 3 (Auto-Fix): AI permitted to fix ERC violations (e.g., floating pins) automatically.
    •  Governance: Tie authority levels to User Trust Settings and Phase Gates.
Phase 9.4: Automation & Scripting
    •  Python Bindings: pip install acutesim (using pybind11 or nanobind) for headless control.
    •  Structured CLI: JSON-in/JSON-out interface for robust CI/CD integration.
    •  Remote Execution Hooks: API for offloading heavy solves to cloud/cluster.

☁️ PHASE 10 — CLOUD & ENTERPRISE
Purpose: Transition from singleton desktop to multi-tenant cloud platform.
Backend: Headless & Multi-User
    •  Headless Solver Mode: Decouple GUI from CircuitSim.
    •  Dockerization: Production-grade Dockerfiles.
    •  Multi-tenant Session Manager.
    •  Transient Session Scratchpad.
    •  Model Redirection.
    •  Perm-Commit Logic.
    •  WebAssembly Frontend (Hosting) via CDN.
    •  Cloud Dispatcher V2: Google Pub/Sub integration.
Phase 10.1: Agentic Workflows
    •  Autonomous Debugging: AI loops: modify $\rightarrow$ simulate $\rightarrow$ analyze $\rightarrow$ repeat.
    •  Design Exploration: "Find the best topology for X spec".
    •  Root Cause Analyst: Deep learning on convergence traces.
    •  Auto-Scripting: AI generates Python verification scripts.

🧠 PHASE 2 (EXT): NEURO-SYMBOLIC ASSURANCE (VERIFIED AI)
Purpose: Move from "Generative AI" to "Correct-by-Verification" AI. Solve the hallucination problem by pairing LLMs with formal proofs.
2.1 The Generator-Verifier Adversarial Loop
    • Architecture: Implement a dual-agent system.
        ○ Agent A (Generator): LLM (e.g., Code Llama/DeepSeek) generates RTL/Netlist from prompts.
        ○ Agent B (Verifier): A Neuro-Symbolic agent that generates SystemVerilog Assertions (SVA) and runs formal checks.
    • Feedback Loop: If Formal Verification fails (Counter-Example found), the trace is fed back to Agent A as a "correction prompt".
    • Objective: Achieve 100% formal property closure on AI-generated blocks before human review.
2.2 Automated SVA Synthesis Agent
    • Task: Create an agent that scans RTL and natural language intent to synthesize SVA properties.
    • Liveness: assert property (@(posedge clk) req |-> s_eventually gnt)
    • Safety: assert property (@(posedge clk)!(gnt1 && gnt2))
    • Metric: < 5% syntax error rate in generated SVA; > 90% coverage of functional requirements.
2.3 Logical Equivalence Checking (LEC) for Optimization
    • Task: Integrate an open-source LEC tool (e.g., Yosys eqy or abc).
    • Workflow: When an AI agent "optimizes" a netlist (e.g., for area or power), automatically run LEC against the original netlist to prove $F(opt) \equiv F(orig)$.
    • Safety: Automatically reject any AI suggestion that fails the LEC check.

PHASE 11 — MODERN UI REARCHITECTURE (VS CODE–GRADE WORKSPACE)
Purpose: Redesign the Qt interface into a modern workspace.
11.1 UI Design Principles
(CRITICAL PRIORITY: IMMEDIATE START AFTER FUNDAMENTALS)
    •  Visual Language: Soft rounded corners, subtle elevation, dark-first.
    •  Typography: Inter/SF Pro + Monospace.
    •  Interaction: Zero visual jitter, consistent hit targets.
⚡ UI Performance Budgets
    • Redraw Latency: < 16ms (60 FPS) for schematics with < 5k components.
    • Waveform Plotting: < 50ms to render 1M points (using WebGPU downsampling).
    • Interaction: < 100ms response to "drag-and-drop" or "wire snap".
♿ Accessibility & Navigation
    • Keyboard Suite: 100% actionable GUI via keyboard (Tab traversal, Shortcut keys for all tools).
    • Contrast Audit: WCAG 2.1 AA compliance for dark/light themes.
11.2 VS Code–Style Docking System
    •  Workspace Model: Central editor, dockable sidebars, bottom panel.
    •  Docking Features: Drag-to-dock, split views, layout serialization.
    •  Qt Implementation: Custom docking framework.
11.3 VLSI-Specific Tab Semantics
    •  Schematic Tabs: Track netlist identity.
    •  Waveform Tabs: Result-bound, overlay comparison.
    •  Simulation Tabs: Grouped per run.
    •  Layout/PDN Tabs: Fixed zoom anchoring, layer toggle.
11.4 Scaling, DPI, and Platform Correctness
    •  Scaling System: DPI-aware layout units.
    •  Platform Handling: Native title bars, Windows scaling, WASM parity.
11.5 Visual Theming & Styling System
    •  Theme Engine: Centralized color tokens.
    •  Dark Theme: VS Code–inspired.
    •  Future Themes: Light mode, High-contrast.
11.6 AI & Debug UI Integration
    •  AI Panel: Dockable assistant, context chips, diff viewer.
    •  Debug Feedback: Non-modal error surfaces (Toasts).
    •  Ghosting: Advanced visual states and interactive acceptance.
11.7 Keyboard-First Workflow
    •  Command Palette: Central launcher (Ctrl+Shift+P).
    •  Keybinding System: Rebindable shortcuts.
    •  State Compatibility: Auto-migration.
11.9 UI Refinement Objectives
    •  Adaptive Workspace Layouts: Logic to save/load specific layouts for "Analog Debugging" vs "Digital RTL Flow".
    •  High-Density Sidebar Widgets: Minimized UI footprint for properties and explorer to maximize schematic canvas area.
    •  Ghosting Visualization Engine: UI layer for rendering speculative changes (AI suggestions) alongside current design state.
Architectural Theme: Unified Project Management Layer
    •  Unified Project Management Console: A single "Executive View" for managing multi-repo, versioned projects that bridge the analog-to-digital divide.
    •  Library Versioning & Artifact Sync: Real-time synchronization of PDKs and design libraries across all design domains.

🧠 PHASE 12 — THE ORCHESTRATION LAYER (THE "BRAIN" DRIVER)
Purpose: Transform CuteApp into the "Executive Function" for the entire open-source VLSI stack.
12.1 Worker Adapter Architecture (The "Thin Client" Strategy)
Goal: Unified Driver API for local, Docker, and Cloud resources.
Core Dependencies
    •  gRPC + Protobuf: Define WorkerService.proto.
    •  Docker SDK (C++): Manage local containers.
    •  WSL2 Interop Library: Drive Linux binaries inside WSL2.
    •  Yosys (Digital Verification): Integration for logic synthesis and formal verification.
    •  OpenROAD (Physical Implementation): Integration for pathing, IO placement, and 3D routing verification.
    •  KLayout (Signoff): GDSII export pipeline and final layout viewing.
Implementation Tasks
    •  Abstract Worker Interface: IWorkerDriver with submitJob, cancelJob.
    •  Client Implementation: grpc_worker_client.cpp.
    •  Yosys Driver: Generates TCL, parses logs.
    •  OpenROAD Driver: Translates positions to DEF.
    •  Cross-Platform Strategy: Auto-detect environment.
12.2 Competitive Advantage: Cross-Domain Intelligence
Innovation 1: AI-Driven Design Space Exploration
    •  Parallel Harness Orchestrator: Logic to manage 50+ concurrent Worker instances for massive Pareto frontier sweeps.
    •  Interactive Trade-off Navigator: A visualization tool allowing users to click design points on a curve to auto-load synthesized netlists.
    •  Constraints Optimizer: AI-driven adjustment of area/delay targets based on previous synthesis failures.
Innovation 2: Analog-Aware Digital Constraints
    •  Dynamic Keep-Out Injection: Automated tagging of schematic LNA/Ref nodes to force physical distance in placement.
    •  Differential Shielding Governor: Logic to prioritize routing for low-noise signals with automatic guard-ring insertion.
    •  Coupling Awareness Engine: Real-time estimation of digital-to-analog crosstalk during the P&R loop.
Innovation 3: Semantic Translation (Teacher Loop)
    •  Unified Trace Database: A persistent triplestore mapping RTL source lines to schematic nodes and layout geometry.
    •  Ghost-Error Visualizer: UI engine that parses backend log messages and renders them as interactive schematic overlays.
    •  One-Click ECO: Automation to apply layout-driven schematic fixes (e.g., swapping a finger count) without losing state.
Architectural Theme: Hierarchical Orchestration & Management
    •  Canonical Project Schema: A standardized project definition format that ensures all workers (Local/Docker/Cloud) operate on the same design truth.
    •  Inter-Tool Communication Bus: OpenRPC/GraphQL layer for data exchange between the orchestration layer and external EDA tools.
12.3 UI Adaptations: The "Worker Manager"
    •  Worker Status Panel: Monitor CPU/Queue depth.
    •  Dependency Wizard: Auto-detect backend strategy.
    •  Cost Estimator: Toggle for Cloud cost prediction.
12.4 Flow Architecture & Contracts (Fragility Prevention)
Purpose: Explicit data contracts to prevent toolchain degradation.
📦 Flow Contract Objects (Enforced)
    •  SynthesisContract:
        ○ Inputs: RTL + Constraints
        ○ Outputs: GateNetlistIR
        ○ Guarantees: No unbound ports, timing arcs preserved.
    •  PlaceRouteContract:
        ○ Inputs: GateNetlistIR + FloorplanIR
        ○ Outputs: DEF + SPEF
        ○ Guarantees: Legal placement, routing DRC clean.
    •  LayoutContract:
        ○ Inputs: GDS + ExtractionRules
        ○ Outputs: LVS/DRC Reports
        ○ Guarantees: LVS clean OR annotated violations.
🟠 Incremental Flow Invalidations
Purpose: Topological hashing for granular cache invalidation (Incremental EDA).
    •  Schematic → Synthesis: Topology change invalidates synthesis + downstream.
    •  Floorplan → Place/Route: Floorplan change invalidates OpenROAD only.
    •  Parasitics → Timing: Parasitic change invalidates PI + Timing (not Placement).
    •  Impact: Turns the system from Batch EDA to Incremental EDA.
🏗️ ORCHESTRATION SEQUENCE (RTL → GDS)

OpenROAD (P&R)cuteApp (Analog)Yosys (Digital)OrchestratorSchematic EditorOpenROAD (P&R)cuteApp (Analog)Yosys (Digital)OrchestratorSchematic EditorTrigger SynthesisGet RTL / NetlistLogical SynthesisAnalog Verify (Tensor MNA)Convergence OKPhysical LayoutGDSII / SPEF

🏭 PHASE 13 — PRODUCTION DEPLOYMENT & ENTERPRISE CI/CD
Purpose: Industrial-grade delivery pipeline.
13.1 The "Matrix Build" Pipeline
Goal: Automated generation of every platform target.
    •  Linux Agent: Compile headless, build Docker, WASM.
    •  macOS Agent: Sign & Notarize, validate Metal.
    •  Windows Agent: Compile .exe, test with WSL2.
13.2 Cloud Infrastructure (Hybrid Execution)
    •  Autoscaling Worker Cluster: KEDA scaling.
    •  Spot Instance Strategy: Reduce cost by 90%.
    •  Cost Model & Billing: Free, Pro, Enterprise tiers.
13.3 Telemetry & Observability
    •  OpenTelemetry C++ SDK: Instrument solver.
    •  "Black Box" Recorder: Crash reproduction buffer.

RELIABILITY HARDENING ROADMAP (NASA STANDARDS)
Purpose: Elevate codebase robustness to industrial/aerospace standards.
Phase 1: Core Solver Safety (Engine)
    •  Replace inline heuristics with seedInitialGuess().
    •  Replace inline guarding with applyPnJunctionLimits().
    •  Centralize magic numbers into physics_constants.h.
    •  Implement bounds-checked accessors safe_at / safe_idx.
Phase 2: Input/Output Sanctity (Airlock)
    •  Implement safe_io.h with Tiered Limits.
    •  Refactor rag_context.cpp to use SafeIo.
    •  Create 

tests/test_safe_io.cpp.
Phase 3: GUI State Safety (Cockpit)
    •  Refactor activeComponent to QPointer or weak_ptr.
    •  Upgrade VisualComponent to QGraphicsObject.
    •  Change 

MainWindow members to QPointer.
    •  Add null checks before all pointer dereferences.
Phase 4: Dynamic Debug Logging & Final Sweep
    •  Connect SchematicEditor::violationsChanged to ProblemsPanel::setViolations.
    •  Ensure 

SchematicEditor runs ERC on topology changes.
    •  Bug Sweep: Review simulation_workspace.cpp and 

gui_types.h.
    •  Verify dynamic updates manually.
    •  Close Audit.
Phase 5: System Verification
    •  Run existing unit tests (test_safe_io, test_ai_simulation_intelligence).
    •  Create 

tests/test_gui_logic.cpp to verify VisualComponent/VisualWire.
    •  Verify QGraphicsObject inheritance and casting.
    •  Verify QPointer safety mechanics.
    •  Verify 

SchematicEditor logic.
    •  Verify Build & Run.
Phase 6: WASM Build & Deployment
    •  Modify 

CMakeLists.txt for Emscripten compatibility.
    •  Build WASM target (make AcuteSim).
    •  Ensure serve.py serves latest build.
Phase 7: Simulation GUI Bug Fixes
    •  File Select Broken: Placeholder implementation.
    •  Unexitable Popup: Added DockWidgetClosable feature flag.
    •  Missing Status Feedback: Added QMessageBox popup on simulation completion.
    •  Bottom Panel Metrics: Auto-show resultsDock_.
    •  HDL Window Toggle: Added toggle action.
Phase 8: Browser Verification (Darlington Pair)
    •  Re-compile WASM with GUI fixes.
    •  Start serve.py.
    •  Open http://localhost:8000.
    •  Use Copilot to "design a darlington pair".
    •  Run simulation and verify popup/results.
    •  Verify GUI fixes (HDL toggle, closable dock).

🔥 CRITICAL ARCHITECTURAL EXPANSION (VIRTUOSO CONVERGENCE)
For this roadmap to map onto a realistic "Virtuoso + Spectre + OpenFlows" converged platform, the following sections address areas that were previously under-specified.
1) Schematic Capture & Design Entry
Your roadmap describes simulation engines and tensor netlists but lacks a unified schematic capture flow similar to Virtuoso’s design environment.
Missing details include:
    •  Hierarchical Schematic Editor: Connectivity rules, symbol libraries, and parameter entry.
    •  Library Management & Version Control: PDK support and symbol reuse.
    •  Cross-Probing: Interactive linking between Schematic ↔ Layout ↔ Simulation.
        ○ Requirement: Click Layout ID L123 $\rightarrow$ Highlight Schematic Node NET45.
        ○ Performance: < 100ms cross-tool sync delay.
📐 SIGN-OFF FLOW STANDARDS
    • Timing (STA): Support for .sdc (Constraints) and .lib (Power/Timing).
    • Parasitics: SPEF back-annotation with multi-source resistor reduction.
    • LVS: Standard Netgen integration with delta-reporting surface.
    • GDS: Layer-mapping specified in pdk_rules.json.
2) PDK Integration and Model Support (Expanded)
Foundry PDK support for advanced nodes (beyond SkyWater/GF180) typically involves encrypted SPICE models.
Aspect	Needed Expansion
Model Parser	Full .LIB / .MODEL parse, grammar compatibility with Spectre/HSPICE.
Parameter Fitting	Tools for measured vs simulated curve fitting (RMSE, max error).
LDE/Parasitics	MIG extraction with capacitance, resistance, substrate effects.
    •  Foundry PDK Abstraction: Support for encrypted models (Ghost-mode simulation).
    •  Layout-Dependent Effects (LDE): Essential for analog accuracy in advanced nodes.
    • Parameter Mapping Rules: Formal schema for mapping PDK SPICE models to simulator primitives with version tracking.
    • Encrypted Model Sandbox: Specification for isolated parsing and evaluation of proprietary BSIM/PSP models.
    • LDE Characterization Suite: Automated tests for STI stress, WPE (Well Proximity Effect), and OSE (OD Segmentation Effect).
3) Front-End Design & Verification Toolchain
Commercial analog flows include state-based verification and automated testbenches.
    •  Formal Verification: Analog property verification and equivalence checking.
        ○ Requirement: Verify unity-gain stability across all PVT corners using formal reachability analysis.
        ○ Outcome: Deterministic proof of non-oscillation for reference blocks (LNA/OpAmp).
    •  Assertion Frameworks: SystemVerilog (SV) or PSL assertions tied to simulation goals.
    •  Automated Regression Loop: Continuous integration for analog testbenches.
4) Static Timing Analysis (STA) & Signoff Flow Integration
Crucial for converged analog/digital flows.
    •  OpenSTA Integration: Generate timing constraints from RTL.
        ○ Performance: Capable of processing 100k gate designs in < 30s.
    •  P&R Integration: STA loops within OpenROAD.
    •  Signoff Reporting: Timing closure gates and violation reports.
    • STA Contract & SDC Support: Robust representation of .sdc and .lib for multi-corner timing.
    • STA Accuracy Target: Worst Negative Slack (WNS) must correlate within 10ps of Golden Reference for 99% of paths.
    • Timing Verification Benchmarks: Standard design suite (e.g., OpenTitan blocks) with known slack requirements.
5) Parasitic Extraction & Back-Annotation
Use KLayout/Netgen for DRC/LVS, but expand extraction capabilities.
    •  RC Extraction: Generate SPEF/DSPF formats.
    •  Back-Annotation: Feed parasitics back into corner analysis.
    •  Post-Layout Simulation: Critical for tight analog matching.
    • Extraction Accuracy Metrics: Automated comparison against reference 3D field solvers; define <2% tolerance for RC values.
    • Back-Annotation Feedback Loops: Formal mechanism to feed extracted SPEF data into IR-drop and elective-power analysis.
6) Signal Integrity & Power/IR Analysis
Critical for mixed-signal SoCs.
    •  Signal Integrity Engine: Crosstalk and reflection analysis.
    •  Power Integrity: Power grid modeling and IR drop analysis.
7) Post-Layout Optimization & Physical Synthesis
    •  Physical Synthesis: Timing/power-driven optimization.
    •  DFM (Design for Manufacturability): Advanced patterning and density checks.
    •  Physical Optimization Loops: Local re-routing/optimization after extraction.
8) Interoperability & Standard APIs
    •  Standardized Project Format: Unified workspace definition.
    •  Plugin API: External tool hooks (OpenSTA, Magic, Netgen).
    •  OpenRPC/GraphQL: Scripting and tool integration layer.
9) User Environment / Workflow Integration
    •  Integration Console: Unified project manager.
    •  Library Manager: Versioned artifact tracking.
    •  Interactive Debugging: Cross-tool trace browsing.
10) Documentation & Onboarding Infrastructure
    •  Help System: Integrated syntax guides, tutorials, and training examples.

🛠 TOOL INTEGRATION CONTRACTS (RTL → GDS)
1. Yosys Integration Requirements
    • HDL Dialects: Support Verilog-2005, SystemVerilog-2012 subset (synthesizable), and Liberty (.lib) timing models.
    • Synthesis Contract: synthesizeRTL(src[], lib) -> {netlist.v, output.sdc}.
        ○ Latency: < 30s for 100k gate blocks.
        ○ Validation: Equivalence Checking (LEC) against Golden Netlist must pass.
2. OpenROAD Orchestration Interfaces
    • Tcl-Python Bridge: Direct OpenRoad.tcl shell control via non-blocking pipes.
    • Fallback Strategy: If global routing fails (>1% congestion), auto-relax density target by 5% and retry (max 3 attempts).
    • Data Layout:
        ○ Input: Verilog Netlist, SDC Constraints, Lef/Def.
        ○ Output: GDSII, SPEF (Parasitics), DEF (Final Placement).
3. KLayout & Verification Roles
    • Rule Decks: Support KLDRC (KLayout DRC) and XML-LVS formats.
    • Layer Mapping: Rigid layer_map.json contract mapping OpenROAD GDS indices to PDK gds-layers.
    • Sign-off: LVS mismatch count must be 0 for GDS export to proceed.
4. Explicit Data Contracts & Tool Validation
    • SynthesizeRTL Contract: synthesizeRTL(src[], lib) -> {gate_netlist.v, post_synth.sdc, lib_out.lib}.
        ○ Guarantees: All sequential elements MUST have valid timing arcs; All clocks MUST be constrained in SDC.
    • Version Management: Strict checker for OpenROAD/Yosys version parity; flag errors if hash-mismatch detected in tool-specific binaries.
    • Serialization Rules: Explicit mapping of what remains in canonical memory (OpenDB) vs. what is flushed to scratchpad disk.
    • Equivalence Checking (LEC): Mandatory logical equivalence check (Yosys sat) after synthesis to verify netlist match vs RTL.
    • RC Correlation: Post-route parasitic extraction (SPEF) must correlate within 5% of known reference patterns.

🎹 ORCHESTRATION LAYER SPECIFICATION
2.1 Canonical Flow Graph (FSM)
    • Inputs: RTL/Schematic + PDK + Constraints.
    • Stages:
        1. Synthesis (Yosys)
        2. Floorplan (OpenROAD)
        3. Analog-Verify (cuteApp SPICE)
        4. Place & Route (OpenROAD)
        5. Signoff (Netgen/KLayout)
    • Atomic Transitions: Each stage produces immutable, hashed artifacts (e.g., synth_1a9b.v).
2.2 Orchestration API (gRPC/REST)

service Orchestrator {
  rpc SubmitJob (JobReq) returns (JobId);
  rpc GetStatus (JobId) returns (JobState); // RUNNING, FAILED, COMPLETED
  rpc GetArtifact (ArtifactReq) returns (FileStream);
}
3. Concurrency & Data Model
    • Task Granularity: Independent blocks parallelized; Top-level assembly serialized.
    • Conflict Detection: Merkle-tree hashing of all inputs; reused "Cache Hit" if Hash(Inputs) matches previous run.
4. Worker API & Caching Specifications
    • Worker API Schemas: Formal Protobuf definition for JobSubmission, ProgressEvent (entry/exit/error), and ArtifactStreaming.
    • Retry/Fallback Semantics: Standardized EXPONENTIAL_BACKOFF and VERSION_FALLBACK for failed worker tasks.
    • Cache Reproducibility Suite: Automated tests verifying that identical input hashes ALWAYS produce bit-identical binary artifacts.
    • Cache Eviction Policy: LRU-based governance for multi-GB layout artifact storage.

⚡ GPU PHYSICS STANDARDS
5.1 Numeric Precision
    • Tolerance: GPU_Double vs CPU_Double deviation < 1 ULP.
    • Fallback: Automatic retry on CPU if NaN detected in GPU kernel.
5.2 Memory Layout
    • SoA Tensors: DeviceType_Param (e.g., BJT_Is) stored as contiguous aligned float arrays.
    • Coalescing: Global Memory access patterns optimized for 128-byte transactions.
    • GPU Determinism Metrics: Iterative parity tests between CPU and GPU; NaN fallback policy enforced at the kernel level.
    • Workload Partitioning: Rules for partitioning large MNA matrices across multi-GPU nodes with semantic equivalence checks.
    • Reproducibility Guarantee: Batch runs of identical netlists on GPU must produce bit-exact identical trajectories.

🧪 FLOW-LEVEL REGRESSION SUITE
6.1 End-to-End Benchmarks
    • Designs: Inverter, OpAmp, PLL, RISC-V Core.
    • Metrics:
        ○ Timing Slack: Worst Negative Slack (WNS) match within 10ps of reference.
        ○ DRC Count: Must match Reference Run exactly.
6.2 Compliance Matrix
Standard	Support Level	Validation
LEF/DEF	5.8	Ref Parser Test
Liberty	2007.03	Synopsys Ref Check
SPEF	1.48	StarRC Correlation
Verilog-AMS	2.4	Accellera Suite
6.3 Combinatorial Flow & Compliance Matrix
    • End-to-End Flow Tests:
        ○ RISC-V Small Core: Full RTL -> GDS regression.
        ○ Analog PLL: Full Schematic -> LVS -> Post-Layout Sim.
        ○ Mixed-Sig ADC: Verilog-AMS + Digital Logic co-simulation loop.
    • Language Support Matrix:
        ○ SystemVerilog-2012: synthesizable subset validation.
        ○ SDC 2.1: Constraints coverage (false paths, multi-cycle).
        ○ Liberty: CCS/NLDM power and timing models.

🧠 HIGH-LEVEL MISSING ARCHITECTURAL THEMES
    1. Unified Project Management Layer: One canonical repo, schema, and versioned project definition bridging analog/digital flows.
    2. Hierarchical Simulation Orchestration: Meta-engine coordinating analog and digital simulators (e.g., Spectre and open SPICE variants).
    3. Standard Intermediate Representations: IRs for netlists, parasitics, timing, and physical data.
    4. Hardware-Accelerated Solvers Beyond WASM: GPU kernels for large problem sizes with fallback strategies.

📚 DOCUMENTATION, ONBOARDING, & USABILITY
Purpose: Ensure the tool is accessible to the community and industrial power users.
10.1 Example Designs & Tutorials
    •  RTL → GDS Walkthrough: Complete tutorial using a 32-bit counter and SkyWater 130nm.
    •  Analog Filter Masterclass: From poles/zeros to layout-extracted simulation.
    •  AMS Integration Guide: Implementing a simple DAC using Verilog-AMS.
10.2 Developer Guidelines
    •  Coding Standards: Enforced clang-format and tidy rules for solver safety.
    •  API Versioning Policy: SemVer requirements for getCircuitState() and Orchestration API.
    •  Backward Compatibility: Automated testing against 5 previous regression_golden.db snapshots.
10.3 Community & Support
    •  Issue Templates: Categorized templates for "Solver Divergence", "ERC False Positive", and "AI Hallucination".
    •  Public Test Repository: Open-source collection of "Analog Goldies" for benchmarking.
    •  Documentation Engine: Doxygen-generated API docs integrated with the web-app help layer.

🚀 STRATEGIC FRONTIERS: THE COGNITIVE FOUNDRY
Purpose: Transforming EDA from tool-centric to agent-centric for the Angstrom era.
I. The STCO Imperative (3D-IC & Chiplets)
    • 3D-STCO Partitioning Agent: Multi-Objective RL agent for hierarchical thermal-aware floorplanning ($J_{STCO}$ optimization).
    • Thermal-Aware Floorplanning: GNN-based thermal prediction engine integrated into the partitioning loop.
    • Interface Synthesis Agent: Automated generation of RTL wrappers and physical constraints for UCIe and BoW standards.
II. The Epistemology of Verified AI (Neuro-Symbolic)
    • Neuro-Symbolic Formal Verification Agent: AI that generates SVA properties from intent and validates them with symbolic solvers.
    • Automated SVA Synthesis: Transform natural language specifications into rigorous SystemVerilog Assertions.
    • Generator-Verifier Adversarial Loop: "Correct-by-Verification" workflow where the Generator (LLM) and Verifier (Solver) iterate until proof is found.
    • Logical Equivalence Checking (LEC): Mandatory formal check for any AI-optimized logic block to ensure boolean identity.
III. Analog Renaissance (Physics-Informed Synthesis)
    • Physics-Informed Analog Agent: RL-based Constraint Satisfaction Problem (CSP) solver for analog layout.
    • Constraint-Driven Layout Generation: Reward function ($R$) optimization including Gain, Phase Margin, and Parasitics.
    • Automated Legacy IP Migration: Agents to extract intent from legacy GDSII, resize for new PDKs, and re-layout automatically.
IV. The Cryptographic Bridge (Secure Data Enclaves)
    • Secure Silicon Enclave: Execution architecture where open-source tools run inside TEEs (Intel SGX/AMD SEV) to access encrypted PDKs.
    • Encrypted Execution: PDK decryption occurs only within volatile secure memory; user sees only abstract GDSII or signed hashes.
    • NDA Compliance Agent: Automated generation of Hardware Bill of Materials (HBOM) tracking IP/PDK usage for legal compliance.
V. The Cybernetic Loop (Post-Silicon Learning)
    • Silicon-Twin Feedback Pipeline: Closed-loop system feeding silicon test data back into the design agents.
    • STDF-to-Dataset Parsing: Convert binary ATE logs into structured ML datasets (Parquet/HDF5).
    • Yield Learning Agents: AI models that correlate layout patterns with yield failure modes and calibrate the PDK/Design rules.
VI. Zero-Trust Silicon (Security Sentinel)
    • Security Sentinel Agent: Zero-Trust architecture treating all artifacts as potentially malicious.
    • Trojan Detection: ML classifiers (Reference TrustHub) to scan RTL for trigger/payload signatures.
    • Side-Channel Analysis: Predictive power simulation to detect and mitigate side-channel leakage risks.

🎨 PHASE 3 (NEW): GENERATIVE ANALOG & PHYSICS-INFORMED SYNTHESIS
Purpose: Replace rigid PCell templates with fluid, physics-driven generative design.
3.1 Constraint-Driven Layout Synthesis (AnalogXpert)
    • Approach: Treat analog layout as a Constraint Satisfaction Problem (CSP) solvable via Reinforcement Learning (RL).
    • Reward Function: $R = \sum (w_i \cdot M_i) - P_{parasitic} - P_{DRC}$.
    • Implementation:
        ○ Placement Agent: RL agent places devices to minimize sensitive wire lengths while enforcing symmetry constraints.
        ○ Routing Agent: Obstacle-avoiding Steiner tree router optimized for electromigration and crosstalk.
3.2 Legacy IP Migration Agent
    • Gap: Migrating OpenAccess/legacy analog IP to modern open PDKs.
    • Workflow: Ingest legacy GDSII/Netlist -> Identify sub-circuits -> Resize for new PDK's $g_m/I_d$ -> Regenerate layout.
3.3 Analog Equivalence Checking
    • Task: Verify that the generated layout matches the schematic intent functionally.
    • Method: Compare graph embeddings of extracted vs schematic netlists using GNNs.
🧊 PHASE 4 (NEW): SYSTEM-TECHNOLOGY CO-OPTIMIZATION (STCO)
Purpose: Enable 3D-IC and Chiplet design by optimizing the entire stack.
4.1 3D Hierarchical Partitioning Agent
    • Solution: Multi-Objective RL agent for thermal-aware vertical partitioning.
    • Cost Function: Minimize $L_{wire} + w_t \cdot T_{junction} + w_v \cdot N_{TSV}$.
4.2 Thermal Predictive GNN
    • Objective: Train a GNN surrogate model on low-fidelity thermal data.
    • Performance: Inference in < 100ms allows real-time hotspot detection for the partitioning agent.
4.3 UCIe Interface Synthesis
    • Task: Automate generation of SoC Chiplet (UCIe) wrappers.
    • Capability: Synthesizes Controller/PHY RTL and generates micro-bump constraints based on bandwidth requirements.
🔐 PHASE 5 (NEW): THE SECURE BRIDGE (ENTERPRISE & FOUNDRY)
Purpose: Break the "PDK Wall" by running open tools on proprietary data inside Secure Enclaves.
5.1 Secure Silicon Enclave (TEE) Execution
    • Architecture: Use RISC-V Keystone or Intel SGX enclaves.
    • Workflow: Decrypt proprietary PDK only in enclave RAM; CuteApp/OpenROAD runs securely; only LEF/DEF/Reports leave the enclave.
5.2 Automated HBOM (Hardware Bill of Materials)
    • Compliance: Implement CISA-compliant digital ledger tracking IP provenance and compliance.
    • Zero Trust: Ensure no "unknown" IP blocks exist in the design.
5.3 Hardware Trojan Detection Agent
    • Gap: AI-generated code may contain hidden triggers.
    • Solution: ML classifier (GNN) to scan logic for triggers with near-zero transition probability.
🔄 PHASE 6 (NEW): CLOSED-LOOP SILICON LEARNING
Purpose: Use real manufacturing data to make the design agents smarter.
6.1 STDF Data Ingestion & Parsing
    • Task: Build high-performance parsers for Standard Test Data Format (STDF).
    • Schema: Map binary ATE logs (Fail/Pass) to physical die coordinates.
6.2 Yield Learning Agents
    • Failure Analysis: Correlate layout patterns to silicon failures using ATE data.
    • Calibration: Use silicon measurements to calibrate SPICE model parameters for the simulator.
