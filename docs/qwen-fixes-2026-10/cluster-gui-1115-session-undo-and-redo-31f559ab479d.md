FIXED

# Cluster gui-1115-session-undo-and-redo: a MIDI clip move has no usable redo

Finding: `src/core/ProjectJournal.cpp:163` (historical; the redo decision is now
`src/core/ProjectJournal.cpp:162-163`). After a clip drag, Undo put the clip back, Redo appeared to
run but did not move the clip back to bar 4, and the next look at the Edit menu showed Redo disabled.

## Verdict: REAL

The journal did keep a redo step (`canRedo()` was true after the undo), but that step restored the
**undone** position again, so redo "did nothing" and was consumed. The defect is real and reproduced
on the original tree; it is not the previous gui-1030 shortcut-enable defect (that observer is present
and the test below checks the journal itself, not the menu).

## Root cause (verified in the code)

A GUI clip drag is one undo step made of **two checkpoints that contain one another**:

1. On the press, `src/gui/clips/ClipView.cpp:701` calls `m_clip->addJournalCheckPoint()`: the clip's
   pre-drag XML (bar 3) is the first object in the step.
2. On the release, `src/gui/clips/ClipView.cpp:1101` calls `arrangement::afterUserMove()`.
   `src/core/ArrangementEdits.cpp:306` checkpoints the **track** at the pre-drag position (the track's
   XML contains its clips) and `src/core/ArrangementEdits.cpp:311` merges that with the clip's step, so
   one Ctrl+Z covers the whole gesture.
3. `ProjectJournal::restoreStep()` (old code, `src/core/ProjectJournal.cpp:95-113`) captured each
   object's current state **inside the restore loop**. It restored the clip to bar 3 first, then
   captured the track's current state. That track capture therefore already held the clip at bar 3.
   Redo restored clip bar 4 and then the track snapshot bar 3, which put the clip back at bar 3 again.
   The redo was consumed, so Redo was disabled afterwards.

`Track::restoreState()` reloads a track by deleting and re-creating its clips
(`src/core/Track.cpp:930`), which is why the order of capture and restore matters. The pre-state of
every object is captured before the change; the post-state must be captured the same way, before any
of the step's restores run.

## Fix (at the cause)

`src/core/ProjectJournal.cpp:95-118`: `restoreStep()` now first resolves and captures **every live
object of the step** into the reverse record, and only then restores the step's objects. Both callers,
`undo()` and `redo()`, use that one function, so both directions of the cycle now record the state the
step actually left. No signature, command, stack bound, save format or load path changed. The existing
"objects before actions" protection is kept: restores still run before the step's closures, and a
restore that frees another object's journal id still skips that object at restore time.

## Regression test (deterministic)

`tests/src/core/UndoBoundsTest.cpp`, new
`aMovedClipRedoesThroughItsMergedTrackCheckpoint()`. It drives the real product sequence, not a
hand-built journal step:

- `track.add` + `clip.add` put a clip at bar 3, then the test takes the live `Clip*`.
- `clip->addJournalCheckPoint()`, `setJournalling(false)`, `movePosition(bar 4)` reproduces the press.
- `setJournalling(true)` + `arrangement::afterUserMove(clip, bar3)` reproduces the release and the
  track checkpoint/merge (the default trim mode is used, so the merge runs).
- It asserts one undo step, undo to bar 3, `canRedo()`, redo to bar 4, then a second full cycle
  (undo bar 3 / redo bar 4) so the reverse record redo produces is proved too.

No timers, sleeps or races: positions are explicit ticks, the journal and the arrangement functions
are driven synchronously on the test thread.

## Test matrix

| Path | Coverage / result |
| --- | --- |
| Normal clip drag (press checkpoint + release track checkpoint merged) | new test, RED before / GREEN after; two undo/redo cycles |
| Redo records its own reverse state | same test, second undo/redo cycle |
| Multi-object step whose object is gone (stale journal id) | existing `UndoBoundsTest::anEvictedStepIsNeverSilentlyUndone()` and `JournallingObjectRestoreTest` pass |
| Bounds / exhaustion / reconfiguration | `UndoBoundsTest` (count and byte caps, coalescing on/off/window), `SessionCheckpointTest`, `SessionCheckpointBudgetTest`, `UndoHistoryOptionsTest`, `UndoHistoryPersistenceTest` all pass |
| Audio thread | `restoreStep()` is journal code, reached from `undo()`/`redo()` on the GUI/control thread only. No audio-path function changed; no allocation, lock, wait or log was added on a render path |
| Platform build | standard C++20/Qt code (`Track`, `Clip`, `TimePos`); no platform-only API, no Qt-6-only call and no `std::filesystem` change |

## Evidence (exact commands, exit codes)

All logs are under `build/qwen-logs/` in the worktree. The requested LOG FOLDER could not be written
by this session: both the patch command and a plain `touch` were denied by the session sandbox
(`EXIT=1`, `Permission denied`, recorded in `build/qwen-logs/logdir-denied.txt`). The same numbered
files were written to the writable worktree log directory instead.

RED (test added, product code original):

| Command | EXIT |
| --- | --- |
| `cmake --build build --target UndoBoundsTest -j4` (`build-1-red.log`) | 0 |
| `cd build/tests && ctest -R '^UndoBoundsTest$' --output-on-failure` (`test-1-red.log`) | 8 - `clipPosition(clipId)` 576, expected 768 (`UndoBoundsTest.cpp:413`) |

GREEN (fix applied):

| Command | EXIT |
| --- | --- |
| `cmake --build build --target UndoBoundsTest -j4` (`build-2-green.log`) | 0 |
| `cd build/tests && ctest -R '^UndoBoundsTest$' --output-on-failure` (`test-2-green.log`) | 0 |
| `cd build/tests && ./UndoBoundsTest` (`test-10-undo-binary-green.log`) | 0 - `Totals: 16 passed, 0 failed` |
| `cmake --build build --target UndoBoundsTest zene -j4` (`build-8-final.log`) | 0 |
| `cd build/tests && ctest -R '^UndoBoundsTest$' --output-on-failure` (`test-11-final.log`) | 0 |

Step 4b proof (product patch only, test left in place):

| Command | EXIT |
| --- | --- |
| `git diff -- src/core/ProjectJournal.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1115-session-undo-and-redo-31f559ab479d.r1/logs/fix.patch` | 1 - Permission denied (log folder) |
| `git diff -- src/core/ProjectJournal.cpp > build/qwen-logs/fix.patch` | 0 |
| `git apply --check -R build/qwen-logs/fix.patch` | 0 |
| `git apply -R build/qwen-logs/fix.patch` (`REVERT_EXIT`) | 0 |
| `cmake --build build --target UndoBoundsTest zene -j4` (`build-6-red-proof.log`) | 0 |
| `cd build/tests && ctest -R '^UndoBoundsTest$' --output-on-failure` (`test-8-red-proof.log`) | 8 - same 576/768 assertion |
| `git apply build/qwen-logs/fix.patch` (`REAPPLY_EXIT`) | 0 |
| `cmake --build build --target UndoBoundsTest zene -j4` (`build-7-green-proof.log`) | 0 |
| `cd build/tests && ctest -R '^UndoBoundsTest$' --output-on-failure` (`test-9-green-proof.log`) | 0 |

Affected-family run, 16 CTest entries, final tree:

| Command | EXIT |
| --- | --- |
| `cmake --build build --target UndoBoundsTest JournallingObjectRestoreTest SessionCheckpointBudgetTest SessionCheckpointTest UndoHonestyTest UndoHonestyStateTest ReversibilityUndoTest ReversibilityUndoDeviceTest DeviceMovesUndoTest UndoHistoryPersistenceTest UndoHistoryOptionsTest MainWindowTest ArrangerTimelineTest ArrangementModesTest ScriptDawRemovalUndoTest PluginGestureUndoTest -j4` (`build-4-family.log`) | 0 |
| `cmake --build build --target amplifier -j4` (`build-5-amplifier.log`) - the first family run exposed a missing test fixture, not a regression | 0 |
| `cd build/tests && ctest -R '^(UndoBoundsTest|JournallingObjectRestoreTest|SessionCheckpointBudgetTest|SessionCheckpointTest|UndoHonestyTest|UndoHonestyStateTest|ReversibilityUndoTest|ReversibilityUndoDeviceTest|DeviceMovesUndoTest|UndoHistoryPersistenceTest|UndoHistoryOptionsTest|MainWindowTest|ArrangerTimelineTest|ArrangementModesTest|ScriptDawRemovalUndoTest|PluginGestureUndoTest)$' --output-on-failure` (`test-7-family-green.log`) | 0 - 100% passed out of 16 |

The first family run (`test-5-family.log`) failed only `DeviceMovesUndoTest` because the `amplifier`
fixture module had not been built (`'the amplifier fixture must be built'` at
`DeviceMovesUndoTest.cpp:127`). After building it, that test passes alone (`test-6-devicemoves.log`,
EXIT 0) and the whole family passes.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-1.log`) exits **1**
before any gate runs: the script's cached base-tree checkout is refused by the lane guard
(`gate-baselines/tree checkout ... is not allowed in a Bunny lane`), and the cache directory is not
writable from this session. The same eight gate commands were run directly in the worktree
(`gates-manual.log`):

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity | `bash tests/complexity-gate.sh --check` | 0 - `PASS (check mode: no regressions)` |
| file-length | `bash tests/file-length-gate.sh --check` | 0 - `PASS (check mode: no regressions)` |

## Files committed

Product: `src/core/ProjectJournal.cpp`.
Test: `tests/src/core/UndoBoundsTest.cpp` (existing fork test, already registered).
Register: `tests/upstream-modifications.txt` (the inherited `ProjectJournal.cpp` gains the new reason).
Report: this file. No new product file, no header, no command, no snapshot/canary change.

## Self-check

- **Regression fails with the product change reverted, passes restored.** Yes. Reverted product only:
  `test-8-red-proof.log` EXIT 8, `clipPosition(clipId)` 576 vs 768 at `UndoBoundsTest.cpp:413`.
  Re-applied: `test-9-green-proof.log` EXIT 0; direct binary `test-10-undo-binary-green.log`
  EXIT 0, `Totals: 16 passed, 0 failed`.
- **Deterministic.** Yes. Explicit tick positions, no timers/sleeps/threads; the real journal, track,
  clip and `arrangement::afterUserMove()` are called synchronously.
- **Fix at the cause, every caller and sibling checked.** `restoreStep()` has exactly two callers,
  `undo()` (`ProjectJournal.cpp:147`) and `redo()` (`ProjectJournal.cpp:184`); both now capture all
  post-states before restoring. The reverse capture path on redo (which feeds the next undo) is
  exercised by the second cycle in the test. Multi-object steps that are not nested are unchanged:
  every object is captured live and then restored as before. A step with no live object still returns
  false, so `undo()` keeps its "skip an unavailable step" behaviour (`UndoBoundsTest` stale-id case).
- **No collateral damage.** No save/load format, stack bound, serial, command or public signature
  changed. Existing `UndoBoundsTest` cases (caps, eviction, coalescing on/off, byte budget, stale
  serial, no-op model) and the 16-entry family pass. Old projects and checkpoints restore the same
  objects in the same order; only the timing of the reverse-state capture changed.
- **Realtime / undo.** Nothing runs on the audio thread; `restoreStep()` is called only from
  `undo()`/`redo()` on the GUI/control thread. It allocates only the same `SavedObject` XML the old
  loop already allocated, on that same thread. The drag remains exactly one undo step
  (`undoDepth() == 1` asserted).
- **Verdict FIXED (not ALREADY-FIXED/FALSE-POSITIVE):** the new test fails on the original product
  code and passes with the fix, as recorded above.
