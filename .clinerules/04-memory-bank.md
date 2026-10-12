# Memory Bank

I am Cline, an expert software engineer. My memory resets completely between
sessions — that is what drives me to maintain perfect documentation. The Memory
Bank is the durable, in-repo replacement for the machine-local auto-memory
Claude Code kept at `~/.claude/projects/` (that memory is preserved read-only in
`docs/claude-archive/`).

## Structure — `memory-bank/` (repo root, Cline's default path)

| File | Purpose |
|---|---|
| `projectbrief.md` | Foundation: what this project is and wants to be |
| `productContext.md` | Why it exists, who it serves, owner's direction |
| `systemPatterns.md` | Architecture, dependency rules, key patterns |
| `techContext.md` | Stack, build, test commands, technical constraints |
| `activeContext.md` | **The live cross-agent state — updates most frequently** |
| `progress.md` | What works, what's being built, what's left |

This repo already has excellent context docs, so the static files are mostly
**pointers** into them (`Roadmap.md`, `context.md`, `CLAUDE.md`,
`ENGINE_HANDOFF.md`, per-folder `flow.md`). Only `activeContext.md` and
`progress.md` carry their own live content. **Never duplicate content the
pointer files already hold — duplicates drift.**

## Update triggers

- **Session start:** read all six files (they are small; the pointers tell you
  where the depth lives).
- **After completing a phase/major task:** update `activeContext.md` and
  `progress.md`, then follow `.clinerules/01-collab-handoff.md` (agent stamp,
  commit, push).
- **"Initialize memory bank" / "update memory bank":** full refresh of
  `activeContext.md` + `progress.md` from the session.

## The bridge to Claude Code

`CLAUDE.md` imports `@memory-bank/activeContext.md` — Claude Code auto-loads
it at every session start. That single import line is how Claude always sees
Cline's latest state without being told, and Cline sees Claude's updates by
reading the same file. Keep `activeContext.md` short and current; it is the
one file both agents truly share.
