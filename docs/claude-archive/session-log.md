# Claude Code Session Log (distilled 2026-10-12)

Raw transcripts are machine-local at
`~/.claude/projects/-Users-seshak-Documents-Projects-Coding-Projects/*.jsonl`.
This file is the durable distillation so the history survives new machines and is
readable by any agent. Session work NOT yet reflected in git when a session ended
is marked ⚠.

## 2026-10-12 (branch `engine/spice-correctness`)

- **ba58adfd (10:48)** — full Karam engine review (graphify code-graph first);
  wrote `ENGINE_HANDOFF.md`; saved the owner-direction memory note (tensor
  contraction to 10⁹ transistors; one app: SoC → RAM → firmware → output).
- **469834da (14:16)** — "read handoff, fix GUI compile slowdown, then phase 2";
  started Phase 2; asked the owner to approve commit granularity, legacy-stack
  deletion and dataset download timing (all approved in later sessions).
- **8837b5bf (15:26)** — Verilog-A transport investigation: the GUI generator
  emits PULSE parameters and AC magnitudes that the parser never read back, so
  GUI transients saw DC sources and AC/noise got zero excitation. Split the fix
  into Qt-independent (flattenDto) and Qt-only parts.
- **f87499b9 (15:31)** — fixed two pre-existing failing tests (PowerPort interface
  pin, BatchRunner). **Discovery: the background "DesignDb::toNetlist" task
  (task_f1c1dab5) was editing the SAME checkout, not its own worktree** — Claude
  background tasks advertised as isolated were not.
- **1a09c854 (15:35)** — `DesignDb::toNetlist` stub fix (the bg task's work,
  committed by the owner as `b1d9fd2` + `3453182`). **Second discovery: a foreign
  concurrent edit appeared in `design_db.cpp` (`%.17g` change) while this session
  worked — proven same-checkout session collision.**
- **c649c845 (15:43)** — Verilog-A transport implemented (owner: "Qt is desktop
  only, Vercel is web; continue"); PULSE/SIN/AC round-trip with per-field
  fail-on-regression checks in `test_veriloga_roundtrip`; fixed `1MHz`/`2meg`
  number parsing.
- **75afa6b2 (15:47)** — the "last stretch" ⚠ (was uncommitted at session end):
  stricter fail-closed parser (unknown dot-cards, unimplemented model params,
  behavioural/POLY sources, expressions → hard errors; `.global` and `R=0` work;
  auto `.tran 0 <stop>` step), IBM power-grid datasets downloaded to
  `datasets/` with new harnesses `tests/ibmpg_diff.py` + `tests/xyce_diff.py`;
  ngspice 82/83 (only `aac_10_bridge` fails). **Committed 2026-10-12 by Cline as
  `b85e404`…`f98f835` — see `ENGINE_HANDOFF.md` §0.**

## 2026-10-11

- **263a0930** — cxrun privacy-form wording (not CuteApp work).

## 2026-01 (Desktop path era, `~/Desktop/CuteApp`)

- **84334eb1 / 93fc590e** — original CuteApp VLSI simulator roadmap overview
  sessions (see `memory/MEMORY-desktop-cuteapp.md` for the phase history).

## Hazard record (why `.clinerules/01-collab-handoff.md` exists)

1. Claude background "worktree" tasks edited the main checkout (f87499b9).
2. Two concurrent Claude sessions collided in `design_db.cpp` (1a09c854).
3. `~/.claude/jobs/75afa6b2` was rate-limit-blocked with intent "read handoff
   file, fix GUI compile slowdown, then phase 2" — if it respawns, its premise is
   stale: that work is done and committed. The session-start sweep
   (`.clinerules/00-authoritative-source.md`) catches this class of hazard.
