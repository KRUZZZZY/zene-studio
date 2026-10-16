IMPLEMENTED
# Q050-082 - Correct the Follow Action editor limitation in the docs

Unit: P1, S. Goal: tell users that per-cell Follow Action chains can be edited from the launcher grid.

## Acceptance

| # | Criterion | Command / proof | Exit |
|---|---|---|---|
| 1 | Update stale KNOWN-LIMITATIONS entry saying per-cell chain editing is socket-only | `grep -n "Per-cell chain editing is still socket-only" docs/KNOWN-LIMITATIONS.md` -> no output | 1 |
| 2 | Cell context menu exposes Follow Action Chain... and docs distinguish cell-chain editing from session-wide switch | `grep -n 'Follow Action Chain\.\.\.' src/gui/SessionGridMenus.cpp` -> 127; updated docs/KNOWN-LIMITATIONS.md | 0 |

Files changed: docs/KNOWN-LIMITATIONS.md, docs/050-features/Q050-082.md, docs/qwen-fixes-2026-10/cluster-Q050-082.md.
