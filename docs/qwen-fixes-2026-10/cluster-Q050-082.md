IMPLEMENTED
# Q050-082 - Correct the Follow Action editor limitation in the docs

Unit: P1, S. Goal: tell users that per-cell Follow Action chains can be edited from the launcher grid.

Round 2 (review rework): the KNOWN-LIMITATIONS bullet's headline no longer makes an unqualified
socket-only claim about Follow Actions - Arrangement Record and Back-to-Arrangement are stated as
socket-only while the session-wide Follow Actions switch and per-cell chain editing are stated as GUI
features - and `SessionGridViewTest.theMenusEditTheLauncherThroughTheRegistry` now explicitly asserts
the cell context menu contains `Follow Action Chain...` with `controlCommand == session.set_slot`
(and the scene menu with `session.set_scene`), then reran the test and the documentation checks.

## Acceptance

| # | Criterion | Command / proof | Exit |
|---|---|---|---|
| 1 | Update stale KNOWN-LIMITATIONS entry saying per-cell chain editing is socket-only | `grep -n "Per-cell chain editing is still socket-only" docs/KNOWN-LIMITATIONS.md` -> no output | 1 |
| 1b | No unqualified socket-only claim for the session-wide switch or chain editing remains (rework) | `grep -n "Follow Actions, Arrangement Record and the Back-to-Arrangement switch are drivable" docs/KNOWN-LIMITATIONS.md` -> no output; `grep -n "NOT socket-only"` -> docs/KNOWN-LIMITATIONS.md:119 | 1 / 0 |
| 2 | Cell context menu exposes Follow Action Chain... (session.set_slot) and docs distinguish cell-chain editing from session-wide switch | `ctest -R SessionGridViewTest --output-on-failure` from `build/tests`, with the new assertions in `theMenusEditTheLauncherThroughTheRegistry` (tests/src/gui/SessionGridViewTest.cpp:230-233, 244-247); `grep -n 'Follow Action Chain\.\.\.' src/gui/SessionGridMenus.cpp` -> 127; docs/KNOWN-LIMITATIONS.md:118-131 | 0 |

## Gates (round 2)

| Gate | Exit |
|---|---|
| tests/complexity-gate.sh --check | 1 (red on train 5 by design; output diffed against ../zene-cachyos -> identical, no file of this unit listed) |
| tests/file-length-gate.sh --check | 1 (as above) |
| tests/fork-sources-gate.sh | 0 |
| tests/all-sources-reproduce.sh | 0 |
| tests/no-upstream-regression-gate.sh | 0 |
| tests/scripted/check-namespace | 0 |
| tests/no-tautology-gate.sh | 0 |
| git diff --check | 0 |

Files changed: docs/KNOWN-LIMITATIONS.md, tests/src/gui/SessionGridViewTest.cpp, docs/050-features/Q050-082.md, docs/qwen-fixes-2026-10/cluster-Q050-082.md.
