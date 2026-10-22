# FIXED - cluster new-p20-88-port (review round 1): trigger timing published before the writer wait

Luna's round-1 rework on this cluster was REAL. Round 1 moved the writer
quiescence out of the reader-visible write window (the original BUG 16 snapshot
spin is gone), but `publishView()` still published the trigger state at its
TOP - `publishTriggers()` reset `m_triggeredAt[slot]` to -1 the moment a
modulator's trigger track changed (old `src/core/ModulationLayerPublisher.cpp:90`,
called from `:171`) - while `m_view` still held the OLD view for the whole
writer wait (old `include/ModulationLayer.h:366-374`). During that wait the
version stays even, so an audio worker snapshots the still-published old view
and calls `sinceTrigger()` (`src/core/ModulationLayerPublisher.cpp:161-166`),
which reads the already-reset `m_triggeredAt` and returns 0: the old view's
phase was restarted before the new view was committed
(`applyModulationBlock`, `src/core/ModulationLayerPublisher.cpp:318`).

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-88-port.r2/logs`.

## The fix

The staged view and its trigger state now commit TOGETHER, inside the
(reader-visible) write window - a bounded commit with no wait inside it, so
writer quiescence stays where round 1 put it, outside the window:

- `include/ModulationLayer.h` (`edit()`): inside the odd-version window, after
  `m_view = staged`, the same window also stores `m_hasLayer` and calls
  `publishTriggers()` - the reset of `m_triggeredAt`, the new `m_triggerTrack`,
  `m_perVoice`, `m_anyTrigger`, `m_anyPerVoice` and the `m_voices` discard
  (all fixed-size, allocation-free, lock-free, wait-free). For the entire
  writer wait the still-published old view is therefore paired with the old
  trigger timing; the new view is never observable without its own trigger
  state (the version only turns even after both land).
- `src/core/ModulationLayerPublisher.cpp` (`publishView()`): no longer touches
  any reader-visible state (`m_hasLayer`, `publishTriggers()`) - it stages the
  view and the liveness watches and waits for the pinned writers, as round 1
  did.

No member was added (the class layout is unchanged - the round-1 report's
LMMS_TESTING constraint still holds), no new control command, nothing new on
the audio thread. `undo`/project load/save paths are untouched: the change is
only WHEN the already-existing publications land relative to the writer wait.

## Regression (deterministic, no wall-clock, no sleeps)

`tests/src/core/ModulationAudioViewTest.cpp`,
`anEditWaitingForAnActiveWriterKeepsTheOldViewsTriggerPhase`:

- publishes two entries (slot 0, slot 1) driven by modulator 0, trigger track
  11; stamps a note at song time 10.0 via `setBlockSeconds(10.0)` +
  `noteStarted(11)`;
- an "audio worker" pins slot 0 with `beginTargetWrite(0, token)` (a
  semaphore hands the pin to the main thread - no timing involved);
- the main thread runs an edit that rebinds the modulator's trigger track to
  33; `publishView()` closes slot 0 and blocks on the pin - the worker's
  observation (`targetAlive(0, token)` flips to false because the closing bit
  is set BEFORE the wait) is guaranteed to happen inside the wait, since the
  worker still holds the pin and the edit cannot pass the wait until the
  worker releases it;
- during the wait the worker snapshots (gets the OLD view: 2 entries,
  triggerTrack 11), reads `sinceTrigger(0, 11, 12.75)` - must be 2.75, the
  old phase - and writes the block: slot 0 is closed (skipped), the LATER
  slot 1 is still open and receives the write computed at the old phase
  (-50.0f = sine 0.75-of-cycle, 0.25 depth, -100..100 range);
- the existing no-spin regression
  (`anEditWaitingForAnActiveWriterKeepsTheReaderVisibleVersionEven`) still
  passes: the version stays even during the wait.

Without the fix `sinceTrigger` returns 0 (reset `-1.0` sentinel) and slot 1
is written 0.0f - the test fails on both counts.

## Commands and exit codes (all run from the worktree; ctest from `build/tests`)

| step | command | exit | result |
|---|---|---|---|
| RED build | `cmake --build build --target zene ModulationAudioViewTest -j12` | 0 | logs/01-build-red.log |
| RED test | `ctest -R '^ModulationAudioViewTest$' --output-on-failure` | 8 | logs/02-test-red.log - 16 sub-tests: 15 passed, 1 failed; `anEditWaitingForAnActiveWriterKeepsTheOldViewsTriggerPhase`: Actual `phaseDuringWait` 0, Expected 2.75 |
| GREEN build | `cmake --build build --target zene ModulationAudioViewTest -j12` | 0 | logs/03-build-green.log |
| GREEN test | `ctest -R '^ModulationAudioViewTest$' --output-on-failure` | 0 | logs/04-test-green.log - 1/1 passed |
| dependent rebuilds | `cmake --build build --target ModulationLayerTest ModulationLayerProjectRoundTripTest ModulationRateTest ModulatorToolsTest ModulationLayerValueTest ControlModulatorCommandsTest InstrumentRoutingTest InstrumentLayersCommandsTest MidiInputTransformTest ProjectOpenIntegrityTest InstrumentSetupUiTest ModulationAudioViewTest -j12` | 0 | logs/06-build-testdeps.log (Song.h includes ModulationLayer.h; the `zene` target rebuild covers all product code) |
| family | `ctest -R 'Modulation\|ModulatorTools\|ControlModulatorCommands\|InstrumentRouting\|InstrumentLayersCommands\|MidiInputTransform\|ProjectOpenIntegrity\|InstrumentSetupUi' --output-on-failure` | 0 | logs/07-test-family.log - 12/12 passed |
| reversal patch | `git diff -- include/ModulationLayer.h src/core/ModulationLayerPublisher.cpp > logs/fix.patch` (107 lines; product files only) | 0 | logs/fix.patch |
| reverse-apply | `git apply -R logs/fix.patch` | 0 | - |
| reversed build | `cmake --build build --target zene ModulationAudioViewTest -j12` | 0 | logs/08-build-reversed.log |
| reversed test | `ctest -R '^ModulationAudioViewTest$' --output-on-failure` | 8 | logs/09-test-reversed.log - FAIL: `phaseDuringWait` 0 vs 2.75 (15 passed, 1 failed) |
| re-apply | `git apply logs/fix.patch` | 0 | - |
| reapplied build | `cmake --build build --target zene ModulationAudioViewTest -j12` | 0 | logs/10-build-reapplied.log |
| reapplied test | `ctest -R '^ModulationAudioViewTest$' --output-on-failure` | 0 | logs/11-test-reapplied.log - PASS |
| gate | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | logs/12-feature-check.log - all PASS, complexity/file-length "no new item in your files" |

## Files changed

- `include/ModulationLayer.h` - `edit()` commits `m_hasLayer` + the trigger
  state inside the write window with the view; doc comments updated.
- `src/core/ModulationLayerPublisher.cpp` - `publishView()` no longer
  publishes reader-visible state before its wait.
- `tests/src/core/ModulationAudioViewTest.cpp` - new deterministic
  regression + coverage bullet in the file header.
- `tests/upstream-modifications.txt` - reasons for the three files appended.

## Self-check

- REAL? Yes - the rework point reproduced exactly as Luna described: RED run
  shows `phaseDuringWait` 0.0 during the writer wait where the old view's
  phase must stay 2.75.
- Test fails without the fix (exit 8) and passes with it (exit 0); both legs
  of the reversal proof recorded above.
- The fix is at the cause and complete: every reader-visible publication in
  the publisher (`m_hasLayer`, `m_triggerTrack`, `m_triggeredAt`,
  `m_perVoice`, `m_anyTrigger`, `m_anyPerVoice`, `m_voices`, `m_view`) now
  lands inside the bounded commit; `publishTriggers()` has no other caller;
  writer quiescence, token re-issue and the liveness watches stay outside the
  window; the no-spin regression still passes.
- No collateral behaviour change outside the wait window: before the first
  edit and after every edit completes, all values are identical to before
  (constructor still seeds `-1` sentinels; `edit()` still sets everything on
  every call). Old projects and saved data are untouched.
- Realtime: the audio thread gained nothing new (no new members, no
  allocation, no lock); the window grew by one fixed-size trigger-state
  commit (16 slots of atomics + the fixed voice-pool walk) - still bounded,
  still wait-free; writer waits remain on the control thread.
- Gates: feature_check.py EXIT=0, all rows PASS.
