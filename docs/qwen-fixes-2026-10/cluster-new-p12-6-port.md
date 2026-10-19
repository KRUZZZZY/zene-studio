# Cluster new-p12-6-port: stepped automation moves

**Verdict: ALREADY-FIXED**

The port's `movePoint` already preserves both sides of a stepped node. `putMovedNode` (`src/core/ControlCommandsAutomationTools.cpp:135`) selects the outgoing value from the source node when its in/out values differ (`source.getValueOffset() != 0.f`) and otherwise uses the incoming value, then restores the curve, bend and locked tangents. This is the behaviour the older-line fix (`fc31c6558`) introduced via `restoreStepOutgoingValue`, carried forward here by the newer `putMovedNode` refactor (cluster new-p9-577). No product change is required.

Evidence in the tree:

- `src/core/ControlCommandsAutomationTools.cpp:135` `putMovedNode` preserves `source.getOutValue()` for stepped nodes.
- `src/core/ControlCommandsAutomationTools.cpp:171` `movePoint` snapshots `oldNode` and calls `putMovedNode`.
- `tests/src/core/ControlAutomationModesTest.cpp:347` `movePointPreservesItsDiscontinuityAndLockedTangents` asserts both in and out values survive a move.
- `tests/src/core/AutomationLanesTest.cpp:627` `movePointKeepsTheCurveAndBendOfThePointItMoves` covers the same path on a clip that does not start at tick 0.

A temporary revert of `putMovedNode` to a plain `putValue` made both value assertions fail (`getOutValue` returned the incoming value instead of the preserved outgoing value), confirming the preservation is load-bearing in the current code.

## Evidence table

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Build affected tests | `cmake --build build --target zene tripleoscillator ControlAutomationModesTest AutomationLanesTest` | 0 | All targets built. |
| Modes test | `ctest -R '^ControlAutomationModesTest$' --output-on-failure` from `build/tests` | 0 | 1/1 passed. |
| Lanes test | `ctest -R '^AutomationLanesTest$' --output-on-failure` from `build/tests` | 0 | 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | Pre-existing baseline mismatches in this branch; no new function introduced by this fix. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | Pre-existing baseline mismatches in this branch; no file grew because of this fix. |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-6-port.md`
