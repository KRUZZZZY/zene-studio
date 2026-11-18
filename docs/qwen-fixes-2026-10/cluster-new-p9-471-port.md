# Cluster new-p9-471-port

**Verdict: FIXED** (port of `ce4459f22` from `bugs/qwen-new-p9-471` onto `050/m0-train-5`; not ALREADY-FIXED: the newest line still joined the worker in the destructor).

The finding is real. The GUI-owned `StemJobManager` destructor set `m_quit`, cancelled the jobs and then called `m_thread->wait()`, so closing the window blocked until a running backend returned from `separate()`. Only then did cancellation get a chance to take effect.

## What changed

- `include/StemSeparation/StemJobManager.h`: the worker-side state moves into a private `SharedState` held by `m_state`. The worker and its emit helpers are static and take that state, so none of them touches the manager after it is gone. `QThread`, `m_thread` and the mutex/condition members leave the header.
- `src/core/StemJobManager.cpp`:
  - Constructor (`:80-86`): the worker is a detached `std::thread` over a copy of `m_state`; `signalTarget` is set to `this`.
  - Destructor (`:109-124`): nulls `signalTarget` under `signalMutex`, cancels every job, sets `quit`, wakes the worker and returns. It does not join.
  - Emit helpers (`:335-395`): each takes `signalMutex`, and if `signalTarget` is still set posts the signal to the manager with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Pending posts are discarded when the manager is destroyed.
  - Worker functions (`:398` onward) use `state->` only; no `this` (checked with `rg -n "\bthis\b"`; the remaining hits are constructor, `reportToTaskArea` and `submit`/`cancel`, which run on the caller's thread).
- `tests/src/core/StemJobManagerTest.cpp`: `BlockingInferenceSeparator` (`:141`) and `testDestructionDoesNotWaitForAnInferenceCall` (`:558`) from the accepted commit, unchanged in logic. The test blocks inside `separate()`, destroys the manager, and asserts the destructor returned before the backend was released (`QVERIFY` at `:580`).
- `tests/upstream-modifications.txt`: reason appended to the existing `src/core/StemJobManager.cpp` entry. No new files were added, so `tests/fork-sources.txt` is unchanged.

Behaviour preserved: the same signals with the same arguments are delivered to GUI-thread receivers, in worker order; `cancel()`, `submit()`, `state()`, `progress()`, `result()`, `error()`, the model provisioner, `cancelAll()` and `fetchingModel()`/`downloadProgress()` are unchanged apart from reading `m_state->` instead of members.

## Caveats (not covered by the regression)

- The worker is detached. A process that exits while inference is still running can have the backend torn down on the worker while static destructors run. The accepted fix had the same property.
- The backend is now destroyed on the worker thread, when the last job ends, instead of inside the manager's destructor.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-471-port.r1/logs`

Environment note: the first build failed with `ccache: error: failed to create temporary file ... /run/user/1000/ccache-tmp ... Permission denied` (exit 2, `build-red-1.log`). It is not a code error. Every later build set `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-471`. `test-red-1.log` ran the stale binary because of that failed build, so I do not use it as evidence.

| Check | Command | Result |
|---|---|---|
| RED build (unfixed code, new test) | `cmake --build build --target StemJobManagerTest > .../build-red-2.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| RED test | `cd build/tests && ctest -R '^StemJobManagerTest$' --output-on-failure > .../test-red-2.log 2>&1; echo EXIT=$?` | `EXIT=8`; `18 passed, 1 failed`; `FAIL! : testDestructionDoesNotWaitForAnInferenceCall() 'returnedBeforeRelease' returned FALSE`, `Loc: StemJobManagerTest.cpp(580)` |
| GREEN build (fixed code) | `cmake --build build --target zene StemJobManagerTest -j2 > .../build-green-1.log 2>&1; echo EXIT=$?` | `EXIT=0`; `Built target zene` |
| GREEN test | `ctest -R '^StemJobManagerTest$' --output-on-failure > .../test-green-1.log 2>&1; echo EXIT=$?` | `EXIT=0`; `100% tests passed out of 1` |
| Proof: save patch | `git diff -- include/StemSeparation/StemJobManager.h src/core/StemJobManager.cpp > .../fix.patch` | written (`fix.patch`, 19486 bytes) |
| Proof: revert product code | `git apply -R .../fix.patch` | exit not captured (command backgrounded); `git status` afterwards showed the two product files clean, i.e. identical to `HEAD` |
| Proof: rebuild reverted code | `cmake --build build --target zene StemJobManagerTest -j2 > .../build-proof-red-2.log 2>&1; echo EXIT=$?` | `EXIT=0` (background completion: exit code 0) |
| Proof: RED on reverted code | `ctest -R '^StemJobManagerTest$' --output-on-failure > .../test-proof-red-2.log 2>&1; echo EXIT=$?` | `EXIT=8`; `FAIL! : ...testDestructionDoesNotWaitForAnInferenceCall() 'returnedBeforeRelease' returned FALSE`, `Loc: StemJobManagerTest.cpp(580)` |
| Proof: re-apply fix | `git apply .../fix.patch` | `APPLY_EXIT=0` |
| Proof: rebuild fixed code | `cmake --build build --target zene StemJobManagerTest -j2 > .../build-proof-green-2.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| Proof: GREEN on fixed code | `ctest -R '^StemJobManagerTest$' --output-on-failure > .../test-proof-green-2.log 2>&1; echo EXIT=$?` | `EXIT=0`; `100% tests passed out of 1` |
| Includer targets | `cmake --build build --target StemSplitPipelineTest OnnxRuntimeStemSeparatorTest -j2 > .../build-family-2.log 2>&1; echo BUILD_EXIT=$?` | `BUILD_EXIT=0` (both header includers, since the header changed) |
| Affected family (`-V`) | `ctest -R '^(StemJobManagerTest\|StemSplitPipelineTest\|OnnxRuntimeStemSeparatorTest)$' -V > .../test-family-2.log 2>&1; echo EXIT=$?` | `EXIT=0`; `100% tests passed out of 3`: StemJobManagerTest 19/19 passed (including `testDestructionDoesNotWaitForAnInferenceCall`), StemSplitPipelineTest 12/12, OnnxRuntimeStemSeparatorTest 9/9 |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../gate-complexity-final.log 2>&1; echo COMPLEXITY_EXIT=$?` | `COMPLEXITY_EXIT=1` (see below) |
| File-length gate | `bash tests/file-length-gate.sh --check > .../gate-file-length-final.log 2>&1; echo FILELENGTH_EXIT=$?` | `FILELENGTH_EXIT=1` (see below) |

### Gate failures (pre-existing, not caused by this change)

- Both gates also exit 1 on an unmodified `HEAD` export (`git archive HEAD` extracted to `/tmp/head-gate-check`; `gate-complexity-head-baseline.log` and `gate-file-length-head-baseline.log`, both `EXIT=1`).
- The `REGRESSION` lines are identical between `HEAD` and this branch (`diff` of the `REGRESSION` lines gave no difference for either gate). Complexity: `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`. File-length: 14 test and source files, e.g. `ArrangerTimelineTest.cpp`, `TakeLane.cpp`.
- None of those files is touched by this fix, and `StemJobManager.cpp`/`.h` do not appear in either gate's output.
- I did not fix them: they are outside this cluster, and the rules forbid changing gate limits or baselines.

## Files in this commit

- `include/StemSeparation/StemJobManager.h`
- `src/core/StemJobManager.cpp`
- `tests/src/core/StemJobManagerTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-471-port.md`
