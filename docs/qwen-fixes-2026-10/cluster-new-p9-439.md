FIXED

Cluster new-p9-439: "Recording an automation clip with no live target can crash".

The crash the finding describes is not reachable: the no-target case never yields a null model. The adjacent defect it points at is real, and it is what this change fixes: a recording automation clip with no target still queued writes, taken from a fallback model the clip does not own.

## Evidence

- **The null-model claim is false.** `AutomationClip::firstObject()` returns the first object if one is present and non-null, and otherwise a static fallback `FloatModel`: [src/core/AutomationClip.cpp:283-295](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/AutomationClip.cpp:283). Every path returns a non-null pointer, so the dereferences at the recording branch are never on null. The finding's line 0 is the recording branch of `Song::processAutomations`, now at [src/core/Song.cpp:789-818](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/Song.cpp:789).
- **The real defect.** Before the fix, `bool writes = p->isRecording();` (Song.cpp:790 before the edit) was true for any recording clip, target or not. A target-less clip then queued `recordedModel->inverseScaledValue(...)` of the fallback model into the clip, and recorded the fallback in `m_recordedModels`. Those writes were applied to the clip as nodes by the control-thread drain, so a clip with no control gained automation points.
- **Removal of targets is real.** `AutomationClip::removeObject` (AutomationClip.cpp:229) and `AutomationClip::objectDestroyed` (AutomationClip.cpp:1537) erase the target from `m_objects`, so a clip whose target is removed while still armed for recording reaches this path.
- **Fix.** [src/core/Song.cpp:789-792](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/Song.cpp:789): `bool writes = p->isRecording() && p->hasObjects();`. A recording clip writes only while it holds a target. `hasObjects()` is evaluated only when `isRecording()` is true, so clips that are not recording pay nothing extra.
- **Sibling paths checked.** The automation-mode branch (Song.cpp:793-795) also uses `recordedModel`, but the fallback is never given a mode: the only mode setters are `AutomatableModel::setAutomationMode` (its control command, which addresses real parameters) and the mixer route restore; the fallback's mode stays `Read`, the default (`testReadIsTheDefault`). `Song::queueAutomationClipWrite` (Song.cpp:260) is a control-thread write that does not go through the recording gate and is unchanged. `AutomationClip::recordValue` (AutomationClip.cpp:618) is unchanged; its callers always hold a real target.

## Regression test

`testRecordingClipWithoutTargetWritesNothing` in `tests/src/core/AutomationModesTest.cpp` (line 911). It builds a three-node curve, arms recording, removes the target, plays 8 periods, and requires `pendingWriteCount()` to stay at 0 and the clip's time map to stay bit-identical after the drain. Its inversion re-attaches the target and requires writes to be queued, so the no-target assertion cannot pass by the harness never writing. Deterministic: no timing, the same `processNextBuffer` harness as the rest of the file.

Test matrix:

| Path | Covered by |
| --- | --- |
| Normal path: target present, recording on | the inversion half of the new test; the existing `testAutomationWritesAreQueuedOffTheRenderPath` and `testTouchWritesWhileHeldAndReturnsToReading` |
| Failure: target removed while armed | the new test's first half |
| Reconfiguration: target re-attached | the new test's inversion half |
| Audio-thread constraints | `hasObjects()` takes `m_clipMutex` only on clips that are recording; `AutomationRenderAllocationTest` passes (family run below) |
| Platform | built and run on Linux only (CachyOS here); the test uses no platform API |

## Commands and exit codes

Note on the environment: the first build of the test target failed with `ccache: error: failed to create temporary file ... Permission denied` (build-1-red.log, EXIT=2). ccache's temp directory is outside the write-jail. Every build below was run with `CCACHE_TEMPDIR=$TMPDIR` set in the command, which redirects only ccache's temp files; the compiler inputs and outputs are unchanged.

| Step | Command (logs under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-439.r1/logs/`) | EXIT |
| --- | --- | --- |
| Build, first attempt (ccache permission error) | `cmake --build build --target AutomationModesTest -j4 > build-1-red.log 2>&1; echo EXIT=$?` | 2 |
| RED build (new test, fix not applied yet) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target AutomationModesTest -j4 > build-2-red.log 2>&1; echo EXIT=$?` | 0 |
| **RED run** (from `build/tests`) | `ctest -R '^AutomationModesTest$' --output-on-failure > test-1-red.log 2>&1; echo EXIT=$?` | **8** (test fails) |
| RED assertion | `FAIL! : AutomationModesTest::testRecordingClipWithoutTargetWritesNothing()`, `Actual (rig.clip.pendingWriteCount()): 6`, `Expected (pendingBefore): 0`, `AutomationModesTest.cpp(923)` | - |
| Fix applied | `Song.cpp` edit (lines 789-792) | - |
| GREEN build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationModesTest -j4 > build-3-green.log 2>&1; echo EXIT=$?` | 0 |
| **GREEN run** | `ctest -R '^AutomationModesTest$' --output-on-failure > test-2-green.log 2>&1; echo EXIT=$?` | **0** (100% passed) |
| Proof: product diff saved | `git diff -- src/core/Song.cpp > fix.patch; echo DIFF_EXIT=$?` | 0 |
| Proof: fix reverted | `git apply -R fix.patch; echo REVERSE_EXIT=$?` | 0 |
| Proof: rebuild without fix | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationModesTest -j4 > build-4-nofix.log 2>&1; echo EXIT=$?` | 0 |
| **Proof: run without fix** | `ctest -R '^AutomationModesTest$' --output-on-failure > test-3-nofix.log 2>&1; echo EXIT=$?` | **8** (same failure as RED: `Actual 6`, `Expected 0`) |
| Proof: fix re-applied | `git apply fix.patch; echo APPLY_EXIT=$?` | 0 |
| Proof: rebuild with fix | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationModesTest -j4 > build-5-fixed.log 2>&1; echo EXIT=$?` | 0 |
| **Proof: run with fix (verbose)** | `ctest -R '^AutomationModesTest$' -V > test-4-fixed-verbose.log 2>&1; echo EXIT=$?` | **0**; log shows `Totals: 22 passed, 0 failed` and `PASS : ...testRecordingClipWithoutTargetWritesNothing()` |
| Family build (12 automation test targets besides AutomationModesTest, -j4) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target AutomationPreFirstValueTest SampleAccurateAutomationTest ControlAutomationModesTest AutomationRenderAllocationTest AutomationPrepassProofTest AutomationLanesTest AutomationLaneChildrenTest AutomationWriteToEndTest AutomationThinTest AutomationTrackTest AutomationClipDisconnectTest ControlAutomationScriptTest -j4 > build-6-family.log 2>&1; echo EXIT=$?` | 0 |
| **Family run** (from `build/tests`, `-j2`) | `ctest -R '^(AutomationModesTest\|AutomationPreFirstValueTest\|SampleAccurateAutomationTest\|ControlAutomationModesTest\|AutomationRenderAllocationTest\|AutomationPrepassProofTest\|AutomationLanesTest\|AutomationLaneChildrenTest\|AutomationWriteToEndTest\|AutomationThinTest\|AutomationTrackTest\|AutomationClipDisconnectTest\|ControlAutomationScriptTest)$' -j2 --output-on-failure > test-5-family.log 2>&1; echo EXIT=$?` | **0**; `100% tests passed out of 13` |

Tests executed: 1 target with 22 test functions (AutomationModesTest, 22 passed, 0 failed), plus 12 related targets (13 in total, 13 passed). The three test files suggested in the finding (ControlBounceSupportTest, TapTempoTest, StartHubTest) do not exercise automation recording, so they were not run; the new test sits in AutomationModesTest, the file that owns the `processNextBuffer` recording harness.

Gates. I could not run `feature_check.py` as written: it writes its per-gate logs to `<worktree>/../<name>-feature-check/`, which is outside the write-jail (`gate-1-feature-check.log`: `PermissionError ... zene-qwen-new-p9-439-feature-check`, EXIT=1). I ran the same gate commands it lists, directly in the worktree:

| Gate | Command | EXIT | Result |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | 7 `REGRESSION` lines, all in files this change does not touch (`WasmSandbox.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommands*.cpp`); none in `Song.cpp`, `AutomationModesTest.cpp` or `upstream-modifications.txt`. The feature-check rule (fail only on items new in changed files) therefore passes this row. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | 22 `REGRESSION` lines, none for `Song.cpp` or `AutomationModesTest.cpp`; the feature-check rule passes this row. |

The two ratchet rows are a judgement from the rule, not the harness's own output. Logs: `gate-*.log`.

## Registration

No new files. `src/core/Song.cpp` is an inherited file already listed in `tests/upstream-modifications.txt`; I added a row for this change (`src/core/Song.cpp	cluster new-p9-439: ...`). `tests/src/core/AutomationModesTest.cpp` is already in `tests/fork-sources.txt`.

## Observations, not changed

- `AutomationClip::firstObject()` takes `m_clipMutex` (AutomationClip.cpp:285) and is called from the render thread for every automation clip in range, every tick. This is a pre-existing lock on the audio path, against the realtime rule. The fix adds one lock on recording clips only, and does not touch this one. A separate change should remove the lock from the render path.

Files committed for this verdict: `src/core/Song.cpp`, `tests/src/core/AutomationModesTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p9-439.md`.

## Self-check

Answers to the five completion-check items, in order.

1. **The regression test FAILS with the product change reverted and PASSES with it.** Yes. Reverted: `test-3-nofix.log`, EXIT=8, `Actual (rig.clip.pendingWriteCount()): 6` / `Expected (pendingBefore): 0` at AutomationModesTest.cpp:923. Restored: `test-4-fixed-verbose.log`, EXIT=0, `Totals: 22 passed, 0 failed`. The RED run before any product edit is `test-1-red.log`, EXIT=8, with the same assertion.
2. **Deterministic.** The test uses no wall-clock time, sleeps or threads. It drives `Song::processNextBuffer()` synchronously, the same harness as the rest of AutomationModesTest, and reads `pendingWriteCount()` and the clip's time map directly. The control-thread drain is `drainAutomationWrites()`, a `DirectConnection` `invokeMethod` on the drain timer, so it runs in the calling thread. Its inversion (target re-attached, writes must queue) guards against a pass caused by the harness never writing.
3. **Fix at the cause, complete.** The cause is the recording gate at src/core/Song.cpp:792, `bool writes = p->isRecording() && p->hasObjects();`. Siblings checked: the automation-mode branch (Song.cpp:793-795) also uses `recordedModel`, but the fallback model is never given a mode, so it stays `Read` (the default, `testReadIsTheDefault`); `Song::queueAutomationClipWrite` (Song.cpp:260) is a control-thread write outside the gate; `AutomationClip::recordValue` (AutomationClip.cpp:618) is unchanged and its callers hold a real target.
4. **No collateral damage.** The behaviour changes only for a clip whose recording flag is set and which holds no target; its writes are no longer produced. A clip with a target, or a clip that is not recording, runs the same code. No limit or check was added, so none can hit a caller that legitimately exceeds it. No save format or load path is touched. The 13-target automation family passes (`test-5-family.log`, EXIT=0). Realtime: the change adds one `m_clipMutex` acquisition (`hasObjects()`, AutomationClip.cpp:349), taken only on clips that are recording; it allocates, frees, logs and does no I/O. `firstObject()` already takes the same mutex on this path (AutomationClip.cpp:285), which is a pre-existing realtime-rule issue and is recorded under Observations.
5. **Verdict evidence (not-real part).** The null-dereference claim is FALSE-POSITIVE: `AutomationClip::firstObject()` never returns null, it returns the static fallback `FloatModel` at AutomationClip.cpp:293-294 (`static FloatModel fm(...)`; `return &fm;`). The real defect (writes taken from that fallback) is FIXED as described above.

**Open item, environment (not product):** the completion check's `gates (feature_check.py)` row is FAIL because `feature_check.py` writes its per-gate logs to `<worktree>/../<name>-feature-check/` and the `base_listings` cache, both outside this lane's write-jail (`gate-1-feature-check.log`: `PermissionError`). The gate commands it runs were run directly in the worktree: six PASS (EXIT 0), and the two ratchets EXIT 1 with every `REGRESSION` line in a file this change does not touch (see the Gates table). The harness row needs to be re-run from an unjailed lane to turn green.
