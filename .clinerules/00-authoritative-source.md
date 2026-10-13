# Authoritative sources and session-start sweep

Before ANY task in this repo:

1. **Read `CLAUDE.md` (repo root)** — it is the authoritative project
   instruction file, shared by Claude Code and Cline. Its content is NOT
   duplicated in `.clinerules/`; do not assume you know it.
2. **Read `acutesim_engine/ENGINE_HANDOFF.md` §0 "Where things stand"** — current engine state,
   branch, next steps, and the agent-stamped history.
3. **Read `memory-bank/activeContext.md`** — the live cross-agent session state.

## Session-start sweep (required — same-checkout collisions have PROVEN happened)

- `git status --short` — if the tree is dirty and you did not make it dirty,
  another agent/session may be mid-flight. Stop and confirm with the user.
- `git worktree list --porcelain` and `git log --no-merges origin/engine/spice-correctness..HEAD`
  — catch in-flight branches and worktrees before starting or merging.
- Claude Desktop background tasks are advertised as worktree-isolated but one
  edited this very checkout on 2026-10-12 (see `docs/claude-archive/session-log.md`).
  **Never assume isolation; always check the tree.**
- If `~/.claude/jobs/*/state.json` shows a blocked background Claude job, its
  intent may be stale — check whether its premise is already done (§0) before
  acting on or around it.

## Git traps in this repo

- `.gitignore` "Local Project Exclusions" ignores `tests/`, `CLAUDE.md`,
  `acutesim_engine/ENGINE_HANDOFF.md`, `.claude/`, `datasets/`. **Tracked** files in those paths
  still commit fine, but **new** files there are silently untracked — `git add -f`
  them or they are lost (this almost lost `acutesim_engine/tests/ibmpg_diff.py` and
  `acutesim_engine/tests/xyce_diff.py`).
- Never run multiple git write commands in parallel — `index.lock` races corrupt
  staging (happened 2026-10-12; had to be re-committed).
- After deleting source files, re-run cmake configure
  (`cmake -B build -S . -DFETCHCONTENT_UPDATES_DISCONNECTED=ON`) or MOC autogen
  fails on ghost headers.
