# Product Context

**Why it exists (the owner's direction — stated twice, do not drift):**

One application to design an SoC, connect RAM, run firmware on it, and see the
program's output — covering every step from schematic/RTL to sign-off.

- **Core bet: tensor contraction.** Simulating up to 10⁹ transistors on a local
  GPU. Superposition holds *inside each Newton step* (the linearised system);
  it does not parallelise across Newton iterations or timesteps.
- **Physics reality (measured, M4 Pro / 26 GB):** flat SPICE-accurate 10⁹
  devices does not fit (>150 GB). Billion-scale needs FastSPICE techniques on
  top of contraction: identical cells share tensors, activity bypass,
  GPU-batched fronts. Firmware runs on RTL/gate models; SPICE covers the blocks
  that need it, co-simulated.
- **No tech debt.** Delete dead/legacy code rather than keep it; shortest clean
  code. (This drove the ~40k-line legacy solver deletion, `b85e404`.)
- **Verify by running** (the ngspice harness), not by reading checklists.
- Focused, well-specified tasks may be delegated to subagents
  (`.clinerules/05-delegate-claude.md`, `.claude/commands/cline.md`).

History of how this direction was captured: `docs/claude-archive/memory/acutesim-engine-direction.md`.
