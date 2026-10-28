FIXED

new-p9-283: `isSilent()` forms a sample reference when given zero frames

Evidence (branch `bugs/qwen-new-p9-283`, base `639e1d23e`):

- The defect is in the `SampleFrame*` overload of `MixHelpers::isSilent`. Before the fix, [src/core/MixHelpers.cpp:65](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/MixHelpers.cpp:65) evaluated `&src[0][0]` unconditionally. For `frames == 0` with a null `src`, `src[0]` dereferences a null pointer, which is undefined behaviour. The same line also turns a negative `frames` into `static_cast<std::size_t>(frames * 2)`, a huge unsigned span that starts on real data.
- The fix is a guard before the span is formed: [src/core/MixHelpers.cpp:63-69](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/MixHelpers.cpp:63) now returns `true` when `frames <= 0` (guard at [MixHelpers.cpp:67](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/MixHelpers.cpp:67)). Frames greater than zero take the unchanged path.
- The negative-count case is the one that is observably wrong. A loud buffer with `frames = -1` returned `false` before the fix (the span wraps and `all_of` stops at the first loud sample). The regression test reproduces that.
- The zero-frame null-pointer case is undefined behaviour in the abstract, but on the toolchain used here `&null[0][0]` evaluates to null and the empty span returns `true` anyway. So that case is a guard against UB, not something this test can make fail. This is stated plainly in the test comment.
- Reachability from current callers is only partly traced. Callers:
  - [src/tracks/InstrumentTrack.cpp:274](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/tracks/InstrumentTrack.cpp:274) calls `isSilent(buf, frames)` with the `f_cnt_t frames` of `processAudioBuffer`. I did not trace every caller of `processAudioBuffer` to confirm whether `frames` can be 0 or negative. The guard makes the function safe either way.
  - [src/core/AudioBusHandle.cpp:221](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/AudioBusHandle.cpp:221) already skips `data() == nullptr` before the call at [AudioBusHandle.cpp:226](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/AudioBusHandle.cpp:226). Unchanged.
  - [src/core/AudioBuffer.cpp:341](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/AudioBuffer.cpp:341) and [AudioBuffer.cpp:369](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/AudioBuffer.cpp:369) use the `std::span` overload, which the change does not touch.
- Siblings: the same `&x[0][0]` pattern appears in [src/core/Sample.cpp:121](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/Sample.cpp:121), [src/core/SampleDecoder.cpp:193](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/SampleDecoder.cpp:193), [src/core/SessionAudioBake.cpp:69](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/SessionAudioBake.cpp:69) and [src/core/SessionAudioCells.cpp:173](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/src/core/SessionAudioCells.cpp:173). Those are different functions (resampler and decoder inputs), not `isSilent`. They are not changed here and their empty-input behaviour is not verified.

## Regression test

[tests/src/core/AudioBufferTest.cpp:1048](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/tests/src/core/AudioBufferTest.cpp:1048) `IsSilent_NormalFramesAreClassifiedByLevel` (normal path, passes before and after) and [tests/src/core/AudioBufferTest.cpp:1061](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-283/tests/src/core/AudioBufferTest.cpp:1061) `IsSilent_ZeroOrNegativeFramesAreSilent`. Both are deterministic: fixed buffers, no timing, sleeps or threads. The matrix:

| Path | Input | Expected | Before fix | After fix |
| --- | --- | --- | --- | --- |
| normal, silent | 2 zero frames | true | pass | pass |
| normal, loud | right channel of frame 1 = 0.5 | false | pass | pass |
| zero frames, null pointer | `isSilent(nullptr, 0)` | true | pass (UB, not observable here) | pass |
| zero frames, loud data | `loud.data(), 0` | true | pass | pass |
| negative frames, loud data | `loud.data(), -1` | true | **FAIL** (returned false) | pass |

No new allocation, lock, log or I/O is added. The fix is one compare and branch, so the realtime rule holds on the audio paths that call it (`InstrumentTrack.cpp:274`, `AudioBusHandle.cpp:226`). No command is added and no undo step is affected.

## Commands and exit codes

`LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-283.r1/logs`

Build environment note: the first build failed in ccache because its temp directory resolves to a path the write-jail does not allow. That is `build-1-red.log`, EXIT=2. All later builds were run with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-new-p9-283` set in the environment. No configuration file was changed.

| Step | Command (run from the worktree unless stated) | Exit | Result |
| --- | --- | --- | --- |
| Build, no fix (first attempt) | `cmake --build build --target AudioBufferTest -j4 > $LOGS/build-1-red.log 2>&1` | 2 | ccache temp-file permission error, not a compile error |
| Build, no fix (retry) | `CCACHE_TEMPDIR=... cmake --build build --target AudioBufferTest -j4 > $LOGS/build-2-red.log 2>&1` | 0 | built with the new test, fix not yet applied |
| **RED** | `cd build/tests && ctest -R '^AudioBufferTest$' --output-on-failure > $LOGS/test-1-red.log 2>&1` | **8** | 44 passed, 1 failed. `FAIL! : AudioBufferTest::IsSilent_ZeroOrNegativeFramesAreSilent() 'lmms::MixHelpers::isSilent(loud.data(), -1)' returned FALSE.` |
| Save fix | `git diff -- src/core/MixHelpers.cpp > $LOGS/fix.patch` | 0 | |
| 4b revert | `git apply -R $LOGS/fix.patch` | 0 | |
| 4b build | `cmake --build build --target zene AudioBufferTest -j4 > $LOGS/build-4-reverted.log 2>&1` | 0 | |
| 4b run, fix reverted | `cd build/tests && ctest -R '^AudioBufferTest$' -V --output-on-failure > $LOGS/test-3-reverted-red.log 2>&1` | **8** | same FAIL as RED, 44 passed / 1 failed |
| 4b re-apply | `git apply $LOGS/fix.patch` | 0 | |
| 4b build | `cmake --build build --target zene AudioBufferTest -j4 > $LOGS/build-5-reapplied.log 2>&1` | 0 | |
| 4b run, fix re-applied | `cd build/tests && ctest -R '^AudioBufferTest$' -V --output-on-failure > $LOGS/test-4-reapplied-green.log 2>&1` | **0** | 45 passed, 0 failed |
| Final rebuild after test-comment edit | `cmake --build build --target zene AudioBufferTest -j4 > $LOGS/build-7-final-green.log 2>&1` | 0 | |
| **GREEN (final)** | `cd build/tests && ctest -R '^AudioBufferTest$' -V --output-on-failure > $LOGS/test-6-final-green.log 2>&1` | **0** | `Totals: 45 passed, 0 failed`; `1/1 Test #7: AudioBufferTest ... Passed` |
| GigPlayer family | `cmake --build build --target GigPlayer -j4 > $LOGS/build-6-gigplayer.log 2>&1` | 2 | `No rule to make target 'GigPlayer'`. `ctest -N` lists no GigPlayer test in this tree (the reference tests are not wired into `tests/CMakeLists.txt`). **Not run.** The covering family here is AudioBufferTest. |

Affected family: `ctest -R '^AudioBufferTest$'` = 1 ctest test, 45 QtTest cases (45/45 pass after the fix, 44/45 with the fix reverted).

### Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited **1**, before running any gate. Its per-gate log directory is created at `<worktree>/../<name>-feature-check`, which is outside my write-jail (`PermissionError`, `$LOGS/gates-1-feature-check.log`). The script has no option to change that location, so I ran each gate command the script lists, from the worktree, with its output in `$LOGS`:

| Gate | Command | Exit |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets exit 1 as they do on the train. Under feature_check's rule they pass when no REGRESSION item is new in a changed file. I applied that rule with a read-only comparison against the cached base listing `gate-baselines/639e1d23e4b84a751f6ed90bd626d35d24727cc4.json` (`$LOGS/ratchet_compare.py`, output `$LOGS/gates-5-final-ratchet-compare.log`, EXIT=0):

- complexity: 7 REGRESSION lines, 0 new in the changed files
- file-length: 22 REGRESSION lines, 0 new in the changed files

So every feature_check row would PASS. This is my reproduction of the script's logic, not the script's own run. The script run itself is EXIT=1 for the environmental reason above.

## Files committed for this verdict

- `src/core/MixHelpers.cpp` (product fix, 4 lines added)
- `tests/src/core/AudioBufferTest.cpp` (2 regression functions)
- `tests/upstream-modifications.txt` (two registry lines; both files are inherited from upstream base `4e677cb6c6ab`, verified with `git cat-file -e`)
- `docs/qwen-fixes-2026-10/cluster-new-p9-283.md` (this report)

No new source files, so `tests/fork-sources.txt` is not touched.

## Self-check

- **Each new test FAILS with the product change reverted and PASSES restored.** Yes. The negative-frame assertion fails with the fix reverted (`test-1-red.log`, EXIT 8; `test-3-reverted-red.log`, EXIT 8) and passes with it (`test-4-reapplied-green.log`, `test-6-final-green.log`, EXIT 0). The zero-frame and normal-path assertions pass both ways, as expected for their behaviour.
- **Each acceptance criterion is proven by a test I ran.** Zero frames return true, negative frames return true, and frames above zero keep their value. Proven by `IsSilent_ZeroOrNegativeFramesAreSilent` (line 1061) and `IsSilent_NormalFramesAreClassifiedByLevel` (line 1048), both run in `test-6-final-green.log`.
- **Edit is one undo step; reversibility class.** No command or undo path is touched. N/A.
- **Nothing added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal.** Yes. The change is a single `frames <= 0` branch. `isSilent` is reached from `InstrumentTrack::processAudioBuffer` (`InstrumentTrack.cpp:274`) and `AudioBusHandle` (`AudioBusHandle.cpp:226`). Neither gains any new call.
- **Old projects still load and render exactly as before; new data round-trips.** No save format or project data is touched. For `frames > 0` the code path is byte-for-byte the same as before.
- **Portability.** Plain C++20, no Qt or platform API. Nothing to guard.
- **Determinism.** No wall-clock, sleep or thread. Inputs are fixed `std::array` buffers and literal counts.
- **Caller and sibling check.** Done as listed in the Evidence section. The sibling `&x[0][0]` sites in other functions are reported and left unchanged.
- **Gates.** feature_check.py itself exited 1 for the environmental reason stated; the reproduction of its eight rows gives PASS for all (see Gates).
- **Registration.** Inherited files changed are registered in `tests/upstream-modifications.txt` with reasons, in the same commit.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: this verdict is FIXED.
- **submit_check gates row.** `submit_check.py` runs `feature_check.py`, which exits 1 with `PermissionError` creating `<worktree>/../zene-qwen-new-p9-283-feature-check`, outside the write-jail. Its stdout has no FAIL rows (`gates-6-feature-check-rerun.log`). The eight gate commands themselves all pass as recorded above.
