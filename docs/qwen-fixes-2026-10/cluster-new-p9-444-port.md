# Cluster new-p9-444-port

Verdict: **FIXED** (port of `bugs/qwen-new-p9-444` onto `050/m0-train-5`; the bug is still present on the newest line, so this is not ALREADY-FIXED).

The finding is real. `StepRecorder` owns the pending `StepNote` heap objects in `m_curStepNotes`, and it had no destructor. Closing the piano roll while a step is in progress destroyed the recorder and leaked the pending notes. `StepRecorder` now deletes any remaining entries in its destructor, the same cleanup the apply and dismiss paths already do.

## Port notes

- The original commit `15122bb67` was re-applied by hand. It was not merged, cherry-picked or rebased.
- The regression test is the original `PianoRollBenchmarkTest::destructionReleasesPendingStepNotes`, placed after the existing `cleanupTestCase()`. Include ordering was adjusted for the newer file: the access hook now wraps `StepRecorder.h` and `PianoRoll.h` after `StepRecorderWidget.h`. Nothing else in that file was changed.
- The original test's `nothrow` `operator new` was rewritten as a `try`/`catch` wrapper around the throwing overload. Behaviour is the same.

## File evidence

- `src/core/StepRecorder.cpp:104` allocates a pending note in `notePressed()`: `m_curStepNotes.push_back(new StepNote(...))`.
- `include/StepRecorder.h:140` owns those notes as raw pointers in `std::vector<StepNote*> m_curStepNotes`.
- `src/core/StepRecorder.cpp:47-54` is the new destructor. It deletes every entry still in `m_curStepNotes`.
- `include/StepRecorder.h:50` declares the destructor.
- The existing cleanup paths already free and clear the vector: `prepareNewStep()` (`src/core/StepRecorder.cpp`, delete loop followed by `clear()`) and `removeNotesReleasedForTooLong()` (delete then `erase`). No path frees an entry without removing it, so the destructor cannot double-free.
- `tests/src/gui/PianoRollBenchmarkTest.cpp:177-189` is the regression. It records one pending note through `m_stepRecorder.notePressed`, deletes the piano roll, and requires that the tracked `StepNote` block was freed. The probe is the global `operator delete` at `tests/src/gui/PianoRollBenchmarkTest.cpp:112-113` and `trackedDelete` at line 83.

## Verification evidence

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-444-port.r1/logs`. Commands were run from the worktree, and `ctest` from `build/tests`. `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-444` was needed because ccache could not write its temp file under `/run/user/1000`. The first build attempt failed on that alone (`build-1-red.log`, exit 2), and the retry with the override succeeded.

| Stage | Command | Exit | Result |
|---|---|---:|---|
| RED build (no fix) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-444 cmake --build build --target zene PianoRollBenchmarkTest > $LOGS/build-2-red.log 2>&1` | 0 | built |
| RED | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > $LOGS/test-1-red.log 2>&1` (from `build/tests`) | **8** | `FAIL! : PianoRollBenchmarkTest::destructionReleasesPendingStepNotes() 's_trackedPendingStepNoteDeleted' returned FALSE.` 1 of 1 test failed |
| GREEN build (fix) | `CCACHE_TEMPDIR=... cmake --build build --target zene PianoRollBenchmarkTest > $LOGS/build-3-green.log 2>&1` | 0 | built |
| GREEN | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > $LOGS/test-2-green.log 2>&1` (from `build/tests`) | **0** | 100% tests passed, 1 of 1 |
| Reverse product fix | `git diff -- include/StepRecorder.h src/core/StepRecorder.cpp > $LOGS/fix.patch` then `git apply -R $LOGS/fix.patch` | 0 / 0 | product files reverted |
| Reversed build | `CCACHE_TEMPDIR=... cmake --build build --target zene PianoRollBenchmarkTest > $LOGS/build-4-reversed.log 2>&1` | 0 | built |
| Reversed RED | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > $LOGS/test-3-reversed.log 2>&1` (from `build/tests`) | **8** | same FALSE assertion as the first RED |
| Reapply fix | `git apply $LOGS/fix.patch` | 0 | product fix restored |
| Reapplied build | `CCACHE_TEMPDIR=... cmake --build build --target zene PianoRollBenchmarkTest > $LOGS/build-5-reapplied.log 2>&1` | 0 | built |
| Reapplied GREEN | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > $LOGS/test-4-reapplied.log 2>&1` (from `build/tests`) | **0** | 100% tests passed, 1 of 1 |
| Dependent targets | `CCACHE_TEMPDIR=... cmake --build build --target zene PianoRollRecordTest AccessibilityWalkTest PianoRollBenchmarkTest StepRecorderWidgetTest > $LOGS/build-6-family.log 2>&1` | 0 | all built |
| Affected family | `ctest -R '^(PianoRollBenchmarkTest\|PianoRollRecordTest\|AccessibilityWalkTest\|StepRecorderWidgetTest)$' -V > $LOGS/test-5-family.log 2>&1` (from `build/tests`) | **0** | 4 of 4 ctest tests passed; QTest totals: AccessibilityWalkTest 4, PianoRollBenchmarkTest 6, PianoRollRecordTest 3, StepRecorderWidgetTest 3 = 16 passed, 0 failed |
| Complexity gate | `bash tests/complexity-gate.sh --check > $LOGS/gate-complexity.log 2>&1` | **1** | see below |
| File-length gate | `bash tests/file-length-gate.sh --check > $LOGS/gate-file-length.log 2>&1` | **1** | see below |

### Ratchet gates: failing, and failing the same way on the base

Both gates exit 1 with regressions in files this change does not touch: 6 complexity entries (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`) and 14 file-length entries (for example `TakeLane.cpp` 520 lines, `ArrangerTimelineTest.cpp` 689 lines).

To confirm they predate this change, the same two gate scripts were run on a clean `git archive HEAD` export in `/tmp` (`b02a0ff2b`, without this work):

| Check | Command | Exit |
|---|---|---:|
| Base complexity gate | `bash tests/complexity-gate.sh --check > $LOGS/gate-base-complexity.log 2>&1` (in the export) | 1 |
| Base file-length gate | `bash tests/file-length-gate.sh --check > $LOGS/gate-base-file-length.log 2>&1` (in the export) | 1 |

The sorted `REGRESSION` lines from the base and from this tree are identical. Neither gate limit nor baseline was touched. The gate failures are outside this bug's scope and were not "fixed", because clearing them would mean refactoring unrelated lanes. Someone owning those files needs to clear them.

## Files in the commit

- `include/StepRecorder.h`
- `src/core/StepRecorder.cpp`
- `tests/src/gui/PianoRollBenchmarkTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-444-port.md`

Log searches used `rg` (for `error`, `FAIL!`, `tests passed`); the fff tools were not loaded in this session.
