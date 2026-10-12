# Hard-won gotchas

## Environment

- The shell's `grep` wrapper honours `.gitignore` and **silently skips
  `tests/test_*.cpp`** (tracked but ignored). For exhaustive searches use:
  `command grep -r --exclude-dir=build --exclude-dir=_deps --exclude-dir=graphify-out ...`
- macOS `sed` has no `\b` — use `perl -pi -e`.
- WASM serve port is **8080** (`scripts/serve.py`), not 8000.
- Stale binaries lie: always `cmake --build build --target <x>` before trusting a
  result; a stale CLI once showed the pre-fix dense solver.

## C++ / Qt

- Never name engine identifiers `slots`, `signals`, `emit`, `foreach` — Qt
  macros; engine headers compile into Qt targets and break (`test_std_cells`).
- SPICE line 1 is the **title** — test netlists need a title line or the first
  element vanishes.
- No `-ffast-math` (deletes NaN checks; CLAUDE.md requires `-fno-fast-math`).
- Absolute tolerances lie at stiff nodes — use the scale-aware metrics
  (`kclExcess`, backward error) and justify any tolerance change in the commit.

## Owner direction (do not drift — stated twice, redirected when drifted)

- **Tensor contraction is THE solver design** (goal: ~10⁹ transistors on a local
  GPU; parallelism from superposition: child fronts' Schur complements add into
  the parent, inside each Newton step). A plain min-degree/sparse-LU approach
  was explicitly rejected.
- **No tech debt**: delete dead/legacy code rather than keep it; shortest clean
  code.
- Verify by running tests and the ngspice harness, not by reading checklists.
- Focused, well-specified tasks may be delegated (Claude Code subagents, or the
  headless-agent commands — see `.clinerules/05-delegate-claude.md` and
  `.claude/commands/cline.md`).
