# Cross-agent collaboration protocol (Claude Code ⇄ Cline)

This repo is worked by BOTH Claude Code and Cline. The other agent may have run
before you, after you, or may run CONCURRENTLY with you. Everything below exists
because of real collisions (see `docs/claude-archive/session-log.md`).

## Session start

1. Do the sweep in `00-authoritative-source.md`.
2. Read `ENGINE_HANDOFF.md` §0 and `memory-bank/activeContext.md`.

## Session end (no exceptions)

1. Update `ENGINE_HANDOFF.md` §0 if engine state, branch, or next steps changed.
2. Update `memory-bank/activeContext.md`: what was done, commits made, what's next.
3. **Stamp the agent and date** in both (e.g. "Last session: Cline, 2026-10-12")
   so the other agent knows who did what.
4. Commit (conventional prefix, explain *why*) and push. Uncommitted/unpushed
   work is invisible to the other agent and can be lost.

## Merges and branches

- Before merging any branch or worktree, sweep for unmerged commits.
- Record every merge in `ENGINE_HANDOFF.md` §0: what was merged, commit hash,
  which agent did it.
- Never force-push or rewrite history the other agent may have built on.

## File ownership

- `ENGINE_HANDOFF.md` — engine workstream (Karam / `acutesim_engine`). §0 is the
  living state; append updates, do not rewrite history.
- `SESSION_HANDOFF.md` — web-UI workstream. Never overwrite it.
- `CLAUDE.md` — shared instructions; edit **additively** (both agents read it).
- `memory-bank/activeContext.md` — live cross-agent state; Claude Code
  auto-imports it at every session start via the import line in `CLAUDE.md`.
- `docs/claude-archive/` — preserved machine-local memory/session history;
  read-only reference, never edit.

## Coordination with the ngspice harness

Verify by RUNNING things (`tests/ngspice_diff/run_diff.py` — expect 84/84 since the
junction-gmin fix; also `tests/ibmpg_diff.py` 6/6 and `tests/xyce_diff.py` 0 FAIL), not by reading checklists (owner's standing rule).
