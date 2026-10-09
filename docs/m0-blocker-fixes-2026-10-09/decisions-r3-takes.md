# M0 blocker fix report — decisions-r3-takes

Base: train 2 tip `888934e1b`. The active-take model replaces loop-pass auto-muting. Each recorded
source group uses the newest pass until the user chooses a take; that choice survives later passes.
Play all takes sums unmuted grouped passes. Existing comp playback stays authoritative.

| Cluster / item | Verdict | Commits | Tests and red / green exits | Gate exits | Design |
|---|---|---|---|---|---|
| decisions-r3-takes — loop-record active-take selection, Play all takes, persistence, control and GUI | FIXED | Design `468de6cc8`; fix `610446243` | Builds: `zene` and affected test targets, `EXIT=0`; portable no-WASM `zene`, `EXIT=0`. Targeted ctest set: 10/10 passed (`RecordingInputPathTest`, `RecordingRealtimeTest`, `RecordingPassTest`, `RecordingLatencyTest`, `RecordingRecoveryMenuTest`, `TakeLaneCompTest`, `TakeLaneTest`, `TakeLaneCompPlaybackTest`, `TakeLanesPanelTest`, `ReversibilityContractTest`), `EXIT=0`. The render test verifies Play all summing; the project test verifies save/reopen; the control test verifies take-choice and Play all undo/redo. Sensitivity check: temporarily reversed only the default newest-pass update; `RecordingRealtimeTest` failed as expected, `EXIT=8`; restored `TakeLane.cpp` matched its saved SHA-256; rebuilt and reran green, `EXIT=0`. `RtSafetySweep`, `ControlCommandsSnapshot`, and `python3 tests/doc-figures-gate.py` passed, `EXIT=0`. Live portable snapshot: 825 ids / 76 groups. WASM-enabled A16 reference measurement: 834 rows = 414 `true_inverse`, 40 `snapshot`, 24 `irreversible`, 356 `not_mutating`. | complexity 0; file-length 0; fork-sources 0; no-upstream 0; rt-safety 0; doc-figures 0 | [Active-take design](../m0-design/decisions-r3-takes.md) |

## Commit list

- `12dd52fa7` — document the active-take owner decision (prior continuation commit).
- `468de6cc8` — record the active-take design and realtime invariants.
- `610446243` — implement active-take selection, Play all takes, persistence, commands, panel, snapshot and tests.
- This report is committed separately after the implementation.
