# Project Brief

**AcuteSim / CuteApp — "Cognitive Foundry"**: an AI-agent ASIC design IDE.

- **AcuteSim** (`acutesim_engine/`, aka Karam): the physics simulation kernel —
  SPICE parsing → MNA stamping → Newton loop → **tensor-contraction direct
  solver** (the owner's core bet, targeting ~10⁹ transistors on a local GPU via
  superposition parallelism: child fronts' Schur complements add into the
  parent, inside each Newton step).
- **CuteApp**: the AI cognitive stack (6-layer) + Qt6 desktop GUI IDE.

Deeper detail: `CLAUDE.md` (shared authoritative instructions — read it first),
`Roadmap.md` (phases, gates, verified feature-status table),
`acutesim_engine/ENGINE_HANDOFF.md` (engine state, architecture, known issues, gotchas).

This brief is intentionally a pointer — do not duplicate content here.
