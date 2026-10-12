---
name: acutesim-engine-direction
description: "Owner's vision for the AcuteSim/Karam engine (tensor contraction to 1e9 transistors, SoC+RAM+firmware app) and where the handoff lives"
metadata:
  node_type: memory
  type: project
  originSessionId: ba58adfd-5fa4-47e2-b29d-7c25874908c2
  modified: 2026-10-12T15:33:42.142Z
---

The goal for the Karam engine (CuteApp/acutesim_engine) is one app that runs the whole flow: design an SoC, connect RAM, run firmware, see the output. The scale goal is simulating about 10⁹ transistors on a local GPU. The owner's core bet is **tensor contraction**, with parallelism from superposition (child fronts' Schur complements add into the parent). The solver must stay a contraction design. They rejected a plain minimum-degree ordering approach.

**Why:** this was stated twice by the owner. When the earlier proposal drifted toward a generic sparse LU, they redirected it.

**How to apply:** start at `CuteApp/ENGINE_HANDOFF.md` (branch `engine/spice-correctness`, as of 2026-10-12). As of the 2026-10-12 third session, Task 1 and all of Phase 2 are done (uncommitted) and the legacy solver stack is deleted. The owner wants no tech debt and the shortest clean code: delete dead or legacy code rather than keep it. Next: junction gmin (aac_10), then golden datasets (IBM power grid plus a Xyce subset; the owner approved downloading after Phase 2), then the GUI Verilog-A transport (PULSE and AC). The handoff's section 0 has the details. Verify by running tests and the ngspice harness (`tests/ngspice_diff/run_diff.py`). Focused subtasks may go to Sonnet subagents. Don't overwrite `SESSION_HANDOFF.md`; it belongs to the web-UI work.
