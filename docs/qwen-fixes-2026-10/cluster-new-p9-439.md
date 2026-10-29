FIXED

Cluster new-p9-439: "Recording an automation clip with no live target can crash".

The crash the finding describes is not reachable: the no-target case never yields a null model. The adjacent defect it points at is real, and this change fixes it: a recording automation clip with no target still queued writes, taken from a fallback model the clip does not own. Round 2 (review verdict REWORK) removed the mutex acquisition this fix had added on the render path.

## Evidence

- **The null-model claim is false.** `AutomationClip::firstObject()` returns the first object if one is present and non-null, and otherwise a static fallback `FloatModel` ([src/core/AutomationClip.cpp:286-297](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/AutomationClip.cpp:286), fallback at lines 296-297). Every path returns a non-null pointer, so the dereferences in the recording branch are never on null. The finding's line 0 is the recording branch of `Song::processAutomations` ([src/core/Song.cpp:789-820](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/Song.cpp:789)).
- **The real defect.** Before the fix the gate was `bool writes = p->isRecording();`, so a recording clip with no target still queued `recordedModel->inverseScaledValue(...)` of the fallback model, and recorded the fallback in `m_recordedModels`. The control-thread drain applied those writes as nodes, so a clip with no control gained automation points. (Base: `639e1d23e`.)
- **Target removal and destruction are real.** `AutomationClip::removeObject` (AutomationClip.cpp:231) and `AutomationClip::objectDestroyed` (AutomationClip.cpp:1540) erase the target from `m_objects`, so a clip whose target is removed or destroyed while still armed for recording reaches this path.
- **Fix, render-path safe.** The gate is now [src/core/Song.cpp:793](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/Song.cpp:793): `bool writes = p->isRecording() && p->hasLiveTarget();`. `hasLiveTarget()` is an inline acquire-load of an atomic flag ([include/AutomationClip.h:119](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/include/AutomationClip.h:119)). It takes no mutex. The flag is written by `publishLiveTarget()` ([include/AutomationClip.h:450](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/include/AutomationClip.h:450)) under `m_clipMutex` at every change to `m_objects`:
  - copy constructor: [AutomationClip.cpp:145](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/AutomationClip.cpp:145)
  - `addObject`: [AutomationClip.cpp:216](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/AutomationClip.cpp:216)
  - `removeObject`: [AutomationClip.cpp:245](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/AutomationClip.cpp:245)
  - `objectDestroyed`: [AutomationClip.cpp:1560](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/AutomationClip.cpp:1560)
  - `cleanObjects`: [AutomationClip.cpp:1583](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/src/core/AutomationClip.cpp:1583)
  - `clearObjects()` (inline, used by `InlineAutomation`'s copy): [include/AutomationClip.h:377](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-439/include/AutomationClip.h:377)

  A grep of `src` and `include` for every write to `m_objects` (assignment, `push_back`, `erase`, `clear`, `insert`, `swap`, `resize`, `std::move`) finds only these sites. `AutomationClipView.cpp` and `InlineAutomation.h` read or call the same accessors; they do not mutate the list directly.
- **Sibling paths checked.** The automation-mode branch (Song.cpp:794-797) also uses `recordedModel`, but the fallback is never given a mode: the mode setters are `AutomatableModel::setAutomationMode` (the control command, which addresses real parameters) and the mixer route restore, and the fallback stays `Read`, the default (`testReadIsTheDefault`). `Song::queueAutomationClipWrite` (Song.cpp:260) is a control-thread write outside the gate and is unchanged. `AutomationClip::recordValue` (AutomationClip.cpp:618) is unchanged; its callers always hold a real target.

## Regression tests

Both are in `tests/src/core/AutomationModesTest.cpp`:

- `testRecordingClipWithoutTargetWritesNothing` (line 911). It builds a three-node curve, arms recording, removes the target, plays 8 periods, and requires `pendingWriteCount()` to stay at 0 and the clip's time map to stay bit-identical after the drain. Its inversion re-attaches the target and requires writes to be queued.
- `testRecordingClipWhoseTargetIsDestroyedWritesNothing` (line 937). The same contract when the target is deleted: a deleted control leaves the clip through `objectDestroyed`. Its inversion attaches a live control and requires writes; it drains the queue before the clip goes out of scope.

Both use only APIs that exist in the base product (`hasObjects()`, `pendingWriteCount()`, `timeMapBits`, the existing harness). That matters for the RED proof: a test that calls the new accessor cannot compile against the base product, which would be a compile failure rather than a behavioural failure. An earlier round-2 draft did exactly that; it was corrected before the RED run.

Deterministic: no timing and no threads. It uses the same synchronous `processNextBuffer` harness as the rest of the file, and the drain is a `DirectConnection` `invokeMethod` on the drain timer.

Test matrix:

| Path | Covered by |
| --- | --- |
| Normal path: target present, recording on | the inversion halves of both tests; existing `testAutomationWritesAreQueuedOffTheRenderPath`, `testTouchWritesWhileHeldAndReturnsToReading` |
| Failure: target removed while armed (`removeObject`) | `testRecordingClipWithoutTargetWritesNothing`, first half |
| Failure: target destroyed while armed (`objectDestroyed`) | `testRecordingClipWhoseTargetIsDestroyedWritesNothing`, first half |
| Reconfiguration: target re-attached (`addObject`) | the inversion half of both tests |
| Not directly tested: `cleanObjects`, `clearObjects`, copy constructor | reviewed by reading only; the flag is a one-line mirror of `!m_objects.empty()` at each site |
| Audio-thread constraints | the gate reads an atomic (no lock, no allocation, no logging); the family run includes `AutomationRenderAllocationTest` and it passes |
| Platform | built and run on Linux only (CachyOS here); no platform API used |

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-439.r2/logs/` (round 2, prefix `r2-`), and round-1 logs are in `.../new-p9-439.r1/logs/`.

Environment note: ccache's temp directory is outside the write-jail. Every build was run with `CCACHE_TEMPDIR=$TMPDIR` set in the command. This redirects only ccache's temp files. The first round-1 attempt without it failed with `Permission denied` (`build-1-red.log`, EXIT=2).

**Round 2**

| Step | Command | EXIT |
| --- | --- | --- |
| Build with fix (zene + AutomationModesTest) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationModesTest -j4 > r2-build-1-fixed.log 2>&1; echo EXIT=$?` | 0 (47 compile units rebuilt) |
| GREEN run | `cd build/tests && ctest -R '^AutomationModesTest$' -V > r2-test-1-fixed.log 2>&1; echo EXIT=$?` | 0; `Totals: 23 passed, 0 failed` |
| Save the full product diff against the pre-fix base | `git diff 639e1d23e -- src/core/Song.cpp src/core/AutomationClip.cpp include/AutomationClip.h > r2-fix.patch; echo DIFF_EXIT=$?` | 0 (3 files, 17 insertions, 2 deletions) |
| Reverse the product change | `git apply -R r2-fix.patch; echo REVERSE_EXIT=$?` | 0 (product files now equal `639e1d23e`) |
| Build without fix, first attempt (test used `hasLiveTarget()`, absent in base) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationModesTest -j4 > r2-build-2-nofix.log 2>&1; echo EXIT=$?` | 2: `no member named 'hasLiveTarget'` at AutomationModesTest.cpp:920, 931, 954, 957, 969. Not a valid RED; the test was corrected |
| Test calls removed; build without fix | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationModesTest -j4 > r2-build-3-red.log 2>&1; echo EXIT=$?` | 0 |
| **RED run** (without fix) | `cd build/tests && ctest -R '^AutomationModesTest$' -V > r2-test-3-red.log 2>&1; echo EXIT=$?` | **8** (2 failed, 21 passed) |
| RED assertions | `FAIL! : ...testRecordingClipWithoutTargetWritesNothing()` `Actual (rig.clip.pendingWriteCount()): 6`, `Expected (pendingBefore): 0`; `FAIL! : ...testRecordingClipWhoseTargetIsDestroyedWritesNothing()` `Actual (clip.pendingWriteCount()): 6`, `Expected (pendingBefore): 0` | - |
| Restore the fix | `git apply r2-fix.patch; echo APPLY_EXIT=$?`, then `git diff 639e1d23e -- <the 3 files> \| cmp - r2-fix.patch` | 0; identical to the saved patch |
| Build with fix again | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationModesTest -j4 > r2-build-4-green.log 2>&1; echo EXIT=$?` | 0 |
| **GREEN run** (after restore) | `ctest -R '^AutomationModesTest$' -V > r2-test-4-green.log 2>&1; echo EXIT=$?` | **0**; `Totals: 23 passed, 0 failed` |
| Family build (12 targets besides AutomationModesTest, which was built in step 1; -j4) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target AutomationPreFirstValueTest SampleAccurateAutomationTest ControlAutomationModesTest AutomationRenderAllocationTest AutomationPrepassProofTest AutomationLanesTest AutomationLaneChildrenTest AutomationWriteToEndTest AutomationThinTest AutomationTrackTest AutomationClipDisconnectTest ControlAutomationScriptTest -j4 > r2-build-5-family.log 2>&1; echo EXIT=$?` | 0 |
| **Family run** (from `build/tests`, -j2) | `ctest -R '^(AutomationModesTest\|...)$' -j2 --output-on-failure > r2-test-5-family.log 2>&1; echo EXIT=$?` | **0**; `100% tests passed out of 13` |
| Header includers: build (23 targets) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target AutomationClipDisconnectTest AutomationEditorEditingTest AutomationLaneChildGuiTest AutomationLaneChildrenTest AutomationLanesTest AutomationModeMenuTest AutomationModesTest AutomationPreFirstValueTest AutomationRenderAllocationTest AutomationThinTest AutomationTrackTest AutomationWriteToEndTest ClapEffectIntegrationTest ControlAutomationModesTest ControlAutomationScriptTest EngineRenderBenchmarkTest InstrumentLatencyPdcTest MixerStripTest MixingRoutingTest NoteCurvesAcrossTest SampleAccurateAutomationTest UndoHistoryPersistenceTest W3EngineTest -j4 > r2-build-6-includers.log 2>&1; echo EXIT=$?` | 0 |
| **Header includers: run** (from `build/tests`, -j2) | `ctest -R '^(...)$' -j2 --output-on-failure > r2-test-6-includers.log 2>&1; echo EXIT=$?` (the 23 above, plus `AutomationPrepassProofTest`) | **0**; `100% tests passed out of 24` |

The header `AutomationClip.h` is included by 24 test sources (`rg -l AutomationClip.h tests/src`). All 24 test targets were rebuilt and run, and all 24 pass. The `.cpp`-only `Song.cpp` change does not require other rebuilds.

Gates. `feature_check.py` could not run in this lane: it writes its per-gate logs to `<worktree>/../<name>-feature-check/`, outside the write-jail. `r2-feature-check-1.log` shows `PermissionError` (EXIT=1). A write probe into that directory is denied (`touch` EXIT=1), even though its Unix permissions show owner write. The earlier logs in that directory are from another run, and I did not use them as evidence. The gate commands the harness lists were run directly in the worktree (`r2-final-gate-*.log`):

| Gate | Command | EXIT | Result |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | red on items in files this change does not touch; zero items in `Song.cpp`, `AutomationClip.h`, `AutomationClip.cpp`, `AutomationModesTest.cpp`, `upstream-modifications.txt`. The feature-check rule passes this row. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | red on items in untouched files; zero items in the changed files. The feature-check rule passes this row. |

The reviewer reports `feature_check.py` EXIT 0 from its own run. I could not reproduce that run here, and the two ratchet rows above are my reading of the rule, not the harness's output.

## Registration

No new files. Three inherited files changed, and each has a row in `tests/upstream-modifications.txt` (the last three lines): `src/core/Song.cpp` (the gate), `include/AutomationClip.h` (`hasLiveTarget()`, `publishLiveTarget()`, the flag, and `clearObjects()` republishing) and `src/core/AutomationClip.cpp` (the publish sites and the copy constructor). `tests/src/core/AutomationModesTest.cpp` is already in `tests/fork-sources.txt`.

## Observations, not changed

- `AutomationClip::firstObject()` still takes `m_clipMutex` (AutomationClip.cpp:288) and `processAutomations()` calls it for every automation clip in range, every tick, on the render thread. This predates this change and is not in this diff. The review's blocker concerned the `hasObjects()` call this fix had added, which is gone; the same class of violation on this path remains and needs its own change. The natural fix is to take the fallback decision from the lock-free flag and the model pointer published under the same mutex.

## Self-check

Answers to the five completion-check items, in order.

1. **The regression test FAILS with the product change reverted and PASSES with it.** Yes. Reverted (product files equal the base `639e1d23e`): `r2-test-3-red.log`, EXIT=8, both new tests fail with `Actual 6`, `Expected 0`. Restored: `r2-test-4-green.log`, EXIT=0, `Totals: 23 passed, 0 failed`. The earlier round-1 proof (`test-3-nofix.log`, EXIT=8, then `test-4-fixed-verbose.log`, EXIT=0) covered the round-1 gate.
2. **Deterministic.** No wall-clock time, sleeps or threads. Both tests drive `Song::processNextBuffer()` synchronously and read `pendingWriteCount()` and the clip's time map directly. The drain, `drainAutomationWrites()`, is a `DirectConnection` `invokeMethod` on the drain timer and runs in the calling thread. Each test's inversion (a live target re-attached, writes must queue) guards against a pass caused by the harness never writing.
3. **Fix at the cause, complete.** The cause is the recording gate at src/core/Song.cpp:793. The flag mirrors `!m_objects.empty()` after every mutation site listed in Evidence, which I checked with a grep of `src` and `include`. Siblings checked: the automation-mode branch (Song.cpp:794-797), `Song::queueAutomationClipWrite` (Song.cpp:260), and `AutomationClip::recordValue` (AutomationClip.cpp:618), as described above.
4. **No collateral damage.** The behaviour changes only for a clip whose recording flag is set and which holds no target. A clip with a target, or a clip that is not recording, runs the same code, and the flag costs one atomic store per target change. No limit or check was added. No save format or load path is touched. The 24 test targets that include `AutomationClip.h` all pass (`r2-test-6-includers.log`, EXIT=0). Realtime: the render-path change adds no lock, allocation, log or I/O, since the gate reads an atomic. The pre-existing `firstObject()` lock is recorded under Observations and is not changed here.
5. **Verdict evidence (not-real part).** The null-dereference claim is FALSE-POSITIVE: `AutomationClip::firstObject()` never returns null. It returns the static fallback `FloatModel` at AutomationClip.cpp:296-297 (`static FloatModel fm(...)`; `return &fm;`). The real defect, writes taken from that fallback, is FIXED as described above.

**Open item, environment (not product):** `feature_check.py` cannot write its logs from this lane (`r2-feature-check-1.log`, `PermissionError`, and a denied write probe). Its gate commands were run directly, and the table above is my reading of the harness's rules. The harness row needs a run from an unjailed lane.
