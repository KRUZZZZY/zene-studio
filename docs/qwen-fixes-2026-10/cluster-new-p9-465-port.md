# FIXED — cluster new-p9-465-port

Port of the accepted fix `bugs/qwen-new-p9-465` (commits `0b8c432b6`, `1bf127284`) onto the newest line `050/m0-train-5`, written by hand against the current code (no merge, cherry-pick or rebase). The defect is real on this line: `TaskCenter::progress()`, `finish()` and `forget()` identify a job only by `(owner, localId)`, so a worker report queued before a new `begin()` with the same local id ends or changes the new job.

## Verdict basis

- Real on this line: `src/core/TaskCenter.cpp` (pre-fix) `doBegin()` runs on the main thread immediately and `finish()`/`progress()` post a queued call that then matches the newest active entry with that `(owner, localId)`.
- Live callers with reused ids: `BrowserIndex::start()` always uses `(this, 0)`; `ControlMasteringSupport.cpp` `setMasteringRunState()` always uses `(&runStateStore(), 0)` for each run.
- Not ALREADY-FIXED: the newest line had no generation handling in `TaskCenter.h`/`.cpp` at the time of the port.

## Fix (what changed)

- `include/TaskCenter.h`: `TaskToken` (opaque: owner, local id, generation). `begin()` returns it; `progress()` and `finish()` require it. `Entry` carries `generation`; private helpers `findActive(owner, id, gen)`, `currentGeneration`, `clearGeneration`; `doBegin`/`doFinish` take the generation.
- `src/core/TaskCenter.cpp`:
  - `begin()` allocates a generation under `m_generationMutex` and returns the token (`TaskCenter.cpp:121`).
  - `doBegin()` drops a begin that is no longer the latest for its id (`:137`, `:143`) and cancels only the stale generation (`:139-141`).
  - `progress()` / `finish()` apply only to the token's generation (`:161-164`, `:173-175`); `doFinish()` clears the generation map entry (`:190`).
- Callers now keep and pass tokens:
  - `RenderQueue.h`: `RenderJob::taskToken`; `finish(token, id, ...)`. `RenderQueue.cpp:253` (add), `:283` (next), `:293-303` (render lambdas), `:315` (convert), `:372` (cancel).
  - `RenderQueueBounce.cpp:140`, `:206`, `:257`, `:291`: use the job's own token (the bounce path already looks up the job by id on the main thread).
  - `BrowserIndex.h` / `BrowserIndex.cpp:216-220`: token captured by the scan worker; `reportScanEnd(token)`.
  - `StemJobManager.h`: `Job::taskToken` and `taskTokenFor()`; `StemJobManager.cpp:88-103` (report adapters), `:169` (submit), `:332` (`taskTokenFor`).
  - `ControlMasteringSupport.cpp:65-69` (`RunState::task`), `:249-255` (run begin/finish). The accepted fix used a function-local `static`; the token lives in the run state instead.
  - `ControlCommandsVideo.cpp:183` (`landExtracted` takes the token), `:207`, `:216`, `:220-223`.

## Regression tests

`tests/src/core/TaskCenterTest.cpp`: `aLateFinishDoesNotEndAReusedLocalId` (`:136`) and `aLateProgressDoesNotChangeAReusedLocalId` (`:154`). Both start a job, queue a worker report with the old token, begin a replacement with the same id, drain the main thread, and assert the replacement is still Running (and, for progress, still at -1 with no status). The assertions run after the cleanup `finish`, so a failure cannot leave an open task for later tests. Every existing call site in `TaskCenterTest.cpp` and `TaskIndicatorTest.cpp` was moved to the token API; no assertion was weakened.

## Commands and exit codes

Build note: ccache could not write under `/run/user/1000` (`ccache: error: failed to create temporary file ...`), so every build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-465`.

| Step | Command (run from the worktree unless noted) | EXIT | Result |
|---|---|---:|---|
| RED build, attempt 1 | `cmake --build build --target TaskCenterTest > .../build-1-red.log 2>&1` | 2 | ccache permission error; compile did not run. |
| RED build, attempt 2 | `CCACHE_TEMPDIR=... cmake --build build --target TaskCenterTest > .../build-2-red.log 2>&1` | 2 | My new test called `drainTaskCenterEvents`, which I had not yet defined. Helper added. |
| RED build | `CCACHE_TEMPDIR=... cmake --build build --target TaskCenterTest > .../build-3-red.log 2>&1` | 0 | Built against the unfixed product code. |
| **RED test (base API)** | `cd build/tests && ctest -R '^TaskCenterTest$' --output-on-failure > .../test-2-red.log 2>&1` | **8** | 13 tests run: 11 passed, 2 failed. `aLateFinish...`: observed state 3 (Failed) vs expected 1 (Running). `aLateProgress...`: observed progress 0.75 vs expected -1.0. This is the assertion-level RED (test version written against the pre-fix `(owner, id)` API). |
| Fix patch saved | `git diff -- <11 product files> > .../fix.patch` | 0 | Product files only. |
| Revert product patch | `git apply -R .../fix.patch` | 0 | |
| RED build after revert | `CCACHE_TEMPDIR=... cmake --build build --target TaskCenterTest > .../build-6-revert.log 2>&1` | 2 | The token-API test does not compile against the reverted API (`'TaskToken' does not name a type`, `TaskCenterTest.cpp:102`). This is a compile-level failure, not an assertion. The assertion-level RED is the test-2 run above. |
| Restore fix | `git apply .../fix.patch` then `git diff ... \| cmp - .../fix.patch` | 0 / 0 | Working tree identical to the saved patch. |
| Rebuild with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene TaskCenterTest TaskIndicatorTest StemJobManagerTest VideoTrackTest StemSplitPipelineTest LibraryBrowserTest MasteringTest ControlRenderJobTest BrowserFeaturesWindowTest -j2 > .../build-5-fix.log 2>&1` | 0 | 0 error lines. |
| **GREEN test** | `cd build/tests && ctest -R '^(TaskCenterTest\|TaskIndicatorTest\|StemJobManagerTest\|VideoTrackTest\|StemSplitPipelineTest\|LibraryBrowserTest\|MasteringTest\|ControlRenderJobTest\|BrowserFeaturesWindowTest)$' --output-on-failure > .../test-4-green-final.log 2>&1` | **0** | **9 of 9 CTest targets executed, 100% passed.** (Same result in `test-3-green-family.log`.) |
| Complexity gate (with fix) | `bash tests/complexity-gate.sh --check > .../gate-1-complexity.log 2>&1` | **1** | Pre-existing regressions only; none in a changed file (see below). |
| File-length gate (with fix) | `bash tests/file-length-gate.sh --check > .../gate-2-file-length.log 2>&1` | **1** | Pre-existing regressions only; none in a changed file. |
| Complexity gate (fix reverted) | same, `.../gate-3-complexity-without-fix.log` | 1 | Identical REGRESSION/FAIL lines to gate-1 (diff exit 0). |
| File-length gate (fix reverted) | same, `.../gate-4-file-length-without-fix.log` | 1 | Identical REGRESSION/FAIL lines to gate-2 (diff exit 0). |
| Whitespace | `git diff --check` | 0 | |

### Gate failures, stated plainly

Both required gates exit 1, and both failures predate this change. With the fix reverted, the regression lists are identical.

- Complexity: `REGRESSION` for `XyPadPairTest.cpp:dotCentreAlong`, `DawProjectReadTracks.cpp:parseNotes`, `ControlCommandsMixerTools.cpp:adjust`, `ControlCommandsMixerStrip.cpp:setStrip`, `ProjectRevisions.cpp:rotateProjectRevision`, `ControlCommandsShortcuts.cpp:importedOverrides`. None of these files is in this change.
- File length: 14 new files over 500 lines (for example `SmfInterchangeReader.cpp` 504, `TakeLane.cpp` 520, `VideoTrackTest.cpp` 589). None is in this change. `VideoTrackTest.cpp` is a test I built and ran, but I did not change it.

I did not change any gate limit or baseline, and I did not fix other lanes' files. Those are outside this bug.

## Other checks

- No new control commands were added, so the `ControlCommandsSnapshot` canary is unaffected.
- All changed files are product-new (absent at the upstream base `4e677cb6c6ab`), so no `tests/upstream-modifications.txt` entry is needed. No new files were created, so `tests/fork-sources.txt` is unchanged.
- The realtime rule is not touched: the new code runs only on the main thread or on the worker that already posts to it. The generation mutex is held only for map reads and writes.

## Files committed

- `include/BrowserIndex.h`
- `include/RenderQueue.h`
- `include/StemSeparation/StemJobManager.h`
- `include/TaskCenter.h`
- `src/core/BrowserIndex.cpp`
- `src/core/ControlCommandsVideo.cpp`
- `src/core/ControlMasteringSupport.cpp`
- `src/core/RenderQueue.cpp`
- `src/core/RenderQueueBounce.cpp`
- `src/core/StemJobManager.cpp`
- `src/core/TaskCenter.cpp`
- `tests/src/core/TaskCenterTest.cpp`
- `tests/src/gui/TaskIndicatorTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-465-port.md`
