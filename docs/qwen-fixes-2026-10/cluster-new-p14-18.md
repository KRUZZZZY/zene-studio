FIXED

# Cluster new-p14-18: an empty input-transform update adds a no-op undo step

Finding (Luna patrol, BUG 12): `track.input_transform_set` with only `track` recorded an undo step and marked the song modified although the transform did not change, so the next `control.undo` was spent on that no-op.

## Evidence (file:line, branch `bugs/qwen-new-p14-18`)

- Real at the base (`2da2ed356`): `src/core/ControlCommandsInputTransform.cpp` recorded `addUndoStep` and `setModified()` unconditionally after `transform.fromJson` (the code the scoping brief quotes at lines 125-128).
- `track.input_transform_set` with only `track` removes `track`, `scale_snap` and `reset` from the fields, so `transform.fromJson({})` stores nothing and `after == before`.
- `changedNothing()` in `src/core/ControlRegistry.cpp:298-303` only suppresses calls that produced no journal step. The handler's own `addUndoStep` produced one, so the bug stays at the handler. The fix belongs there.
- Sibling handlers already use this rule: `src/core/ControlCommandsCueMix.cpp:191`, `src/core/ControlCommandsPluginOrganise.cpp:88-90`, `src/core/ControlCommandsMixerStrip.cpp:265`.
- `MidiInputTransform::toJson` (`src/core/MidiInputTransform.cpp:161-167`) serialises all eight settings plus the derived `active`, so JSON equality means the configuration is unchanged. The runtime `m_heldAs` table is not part of the transform.

## Fix

`src/core/ControlCommandsInputTransform.cpp:125-133`: build the result object first. If `after == before`, return success with `{track, transform}` and no `addUndoStep`, no `setModified()`, and no `__transaction`. Otherwise record exactly one step as before. `reset` of an already-identity transform is the same no-op. A real change is unaffected.

## Regression test

`tests/src/core/MidiInputTransformTest.cpp:355` `aTrackOnlyUpdateChangesNothingAndTakesNoUndoStep`: the song is marked saved, then a track-only update must leave the undo depth, the dirty flag and the `__transaction` absent. A real edit followed by the track-only update must leave the next `control.undo` reverting the real edit. The test uses no clocks, sleeps or threads. Two includes changed only in the test: `ProjectJournal.h` (for `undoDepth()`). No new files, so no registration is needed.

## Commands and exit codes

Build note: the first build failed with EXIT=2 because ccache could not write its temp file under `/run/user/1000` (`build-1-red.log`). All later builds set `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p14-18`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-18.r1/logs/`.

| Step | Command (run from the worktree or `build/tests` as noted) | EXIT | Log | Result |
| --- | --- | --- | --- | --- |
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p14-18 cmake --build build --target zene MidiInputTransformTest -j2` | 0 | build-2-red.log | builds |
| RED test | `cd build/tests && ctest -R '^MidiInputTransformTest$' --output-on-failure` | 8 | test-1-red.log | `aTrackOnlyUpdate...` FAIL: `undoDepth()` actual 1, expected 0 (`MidiInputTransformTest.cpp:367`); 15 passed, 1 failed |
| GREEN build | same build command | 0 | build-3-green.log | builds |
| GREEN test | `cd build/tests && ctest -R '^MidiInputTransformTest$' --output-on-failure` | 0 | test-2-green.log | 100% passed |
| Revert: save patch | `git diff -- src/core/ControlCommandsInputTransform.cpp > .../logs/fix.patch` | 0 | fix.patch | |
| Revert: undo fix | `git apply -R .../logs/fix.patch` | 0 | | |
| No-fix build | same build command | 0 | build-4-nofix.log | builds |
| No-fix test | `cd build/tests && ctest -R '^MidiInputTransformTest$' --output-on-failure` | 8 | test-3-nofix.log | same assertion: `undoDepth()` actual 1, expected 0 |
| Restore fix | `git apply .../logs/fix.patch` | 0 | | |
| Fixed build | same build command | 0 | build-5-fixed.log | builds |
| Fixed test | `cd build/tests && ctest -R '^MidiInputTransformTest$' --output-on-failure -V` | 0 | test-4-fixed.log | QTest `Totals: 16 passed, 0 failed` |

Affected family: `ctest -R '^MidiInputTransformTest$'` runs 1 ctest test, which holds 16 QTest cases (15 existing plus the new one). The existing `theCommandsUndoAndTheFile` and `aPartialUpdateThatConflictsWithTheCurrentTransformIsRefused` pass with the fix.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` EXIT=1 (`gates-1.log`): it could not run. It creates `<worktree>/../zene-qwen-new-p14-18-feature-check/`, which is outside the write-jail (PermissionError at `feature_check.py:71`). Its ratchet baselines also write into the shared `zene-cachyos` git data. I did not redirect either path. Instead I ran each gate command from the worktree, with logs in the log folder:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (`gate-complexity.log`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (`gate-file-length.log`) |

The two ratchet gates list `REGRESSION` items for other files only (for example `ControlCommandsMixerTools.cpp`, `TakeLane.cpp`, `DeviceDspTest.cpp`). None names `src/core/ControlCommandsInputTransform.cpp` or `tests/src/core/MidiInputTransformTest.cpp`. `MidiInputTransformTest.cpp` is 441 lines, under the 500-line limit. Under `feature_check.py`'s rule (a ratchet row fails only for an item in a changed file) both rows would show PASS. That is a reading of the script, not a run of it.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored.** Yes: RED exit 8 (`test-1-red.log`), no-fix exit 8 (`test-3-nofix.log`), GREEN exit 0 (`test-2-green.log`, `test-4-fixed.log`). The reverted and restored builds are in `build-4-nofix.log` and `build-5-fixed.log`.
- **Every acceptance criterion is proven by a test I ran.** The brief defines no acceptance table. The criteria are the two in §5 of the brief: a track-only update leaves the transform and clean song state unchanged (asserted by undo depth, `isModified()` and the absent `__transaction`); a real edit then the no-op gives one `control.undo` that reverts the real edit (asserted by the final `control.undo`). Both ran in `aTrackOnlyUpdateChangesNothingAndTakesNoUndoStep`, exit 0 with the fix.
- **Each edit is ONE undo step, and reversibility is honest.** A real change still records exactly one step through the unchanged `addUndoStep` and `__transaction`. A no-op records none. The `track.input_transform_set` reversibility row (`src/core/ControlReversibilityTableArranger.cpp:156`, `TrueInverse`) is unchanged.
- **Nothing added runs on the audio thread.** The change is in a control-command handler on the UI/control thread. The new `after == before` comparison is a `QJsonObject` compare in that handler. No audio path is touched (`setTransform` is not reached from `processInEvent` or `apply`).
- **Old projects still load; new data round-trips.** Serialisation (`MidiInputTransform::save`/`load`, `toJson`/`fromJson`) is unchanged. The change only affects a call whose result equals its input. `theCommandsUndoAndTheFile` (save, load, reset) still passes.
- **Qt 5.15 / MSVC / macOS portability.** `QJsonObject::operator==` is available in Qt 5 and Qt 6. No new Qt-6-only API or platform code.
- **Sibling commands.** The brief names no sibling beyond `track.input_transform_get`, a read-only command that the change does not touch. Other commands with the same no-op pattern (CueMix, PluginOrganise, MixerStrip) already follow it; I did not change them, because each is its own bug and outside this finding.
- **Tests that pass without the change.** Ruled out by the RED run above.
- **Test determinism.** No clock, sleep, thread or race. The test uses `Engine::projectJournal()->undoDepth()`, `Song::noteProjectSaved()` and `Song::isModified()`.
- **Gates.** See the Gates section above: `feature_check.py` could not run in this write-jail; the six simple gates exit 0 when run directly, and the two ratchets exit 1 with no item in a changed file.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED, and the bug is real at the base (see Evidence).
- **Not checked:** the full `ctest` suite, and the live-instance script `tests/control-note-editor.py` (it mentions `input_transform`, but it is not a ctest target).
