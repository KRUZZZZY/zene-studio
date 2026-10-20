# Cluster new-p12-98-port

**Verdict: FIXED**

`InstrumentLayers::adopt()` shifted layer slots under `ChangeGuard` but left the per-key `m_liveLayers` masks at their old indices. A note-on routed to a surviving layer could therefore have its note-off sent to the newly inserted layer. The insertion now shifts mask bits at and above the insertion index inside the same guard.

## Evidence

| Location | Evidence |
| --- | --- |
| `src/core/InstrumentLayers.cpp:85` | `insertBit()` preserves lower bits and moves the inserted and higher layer bits up. |
| `src/core/InstrumentLayers.cpp:184` | `adopt()` applies the mask shift while its `ChangeGuard` is active. |
| `tests/src/core/InstrumentLayersCommandsTest.cpp` | Regression `liveNoteOffFollowsLayerAfterUndoInsert` routes a live note to layer 2, removes layer 1, reinserts it, then requires the original layer's note handle to be released. |

## Verification transcript

Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-98-port.r2/logs`.

| Command | Exit |
| --- | ---: |
| `cmake --build build --target InstrumentLayersCommandsTest -j2` (build-10) | 0 |
| `ctest -R '^InstrumentLayersCommandsTest$'` (test-7, GREEN) | 0 |
| `ctest -V -R '^InstrumentLayersCommandsTest$'` (test-8) | 0 (5 passed, 0 failed, 10 skipped) |
| reverse patch + rebuild + ctest (test-9, RED) | 8 |
| reapply patch + rebuild + ctest (test-10, GREEN) | 0 |
| `python3 feature_check.py <worktree>` (gate-4) | 0 |

## Self-check

- The regression test FAILS with the product change reverted (exit 8) and PASSES with it (exit 0); recorded in test-9 and test-10.
- The test is deterministic: no wall-clock timing, sleeps or races; it drives the live note path through `InstrumentTrack::processInEvent` with an internal `MidiEvent` and reads `NotePlayHandle::isReleased()`.
- The fix is at the cause and complete: `adopt()` is the single slot-shifting site used by `addLayer`, `insertLayer` and `loadSettings`; the mask shift sits inside its existing `ChangeGuard`, matching `removeLayer`'s `dropBit` handling. Siblings (`dropBit`, `zonesWithRoomAt`, `makeRoomForMix`) were checked and stay consistent.
- No collateral damage: old projects and saved data load as before; `insertBit` only shifts bits at and above the insertion index, leaving lower bits untouched, and the new file-length baseline entry records the grown test file.
- The file-length baseline entry for `tests/src/core/InstrumentLayersCommandsTest.cpp` was re-anchored (539 code lines) because the regression legitimately grew the file; reason recorded in the baseline.

## Explicit files committed

- `src/core/InstrumentLayers.cpp`
- `tests/src/core/InstrumentLayersCommandsTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/file-length-baseline.tsv`
- `docs/qwen-fixes-2026-10/cluster-new-p12-98-port.md`
