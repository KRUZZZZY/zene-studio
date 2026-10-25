FIXED

4380: FIXED

Decision: REAL. `src/core/RenderQueue.cpp:312-338` sent renderer stderr to the null device and finished with a generic process error. `TaskCenterTest::failedRenderSurfacesChildDiagnostics` now launches the queue's fake CLI child, which emits “encoder refused the requested output” and exits 3; its job report includes that text. `appendDiagnosticTail` retains at most 512 bytes. The test failed on the original production code and failed again when the production patch was removed, then passed after restoration.

Regression commands and exit codes (run from the stated directories):

- RED build: `cmake --build build --target zene TaskCenterTest` — EXIT=0 (`build-6.log`).
- RED test from `build/tests`: `ctest -R '^TaskCenterTest$' --output-on-failure` — EXIT=8 (`test-6.log`), missing-diagnostic assertion.
- GREEN build: `cmake --build build --target zene TaskCenterTest` — EXIT=0 (`build-7.log`).
- GREEN test from `build/tests`: `ctest -R '^TaskCenterTest$' --output-on-failure` — EXIT=0 (`test-7.log`), 1/1 CTest target passed.
- 4b removal command: `git diff -- src/core/RenderQueue.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-4380.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-4380.patch`; rebuild — EXIT=0 (`build-8.log`); test from `build/tests` — EXIT=8 (`test-8.log`), same missing-diagnostic assertion.
- 4b restore command: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-4380.patch && cmake --build build --target zene TaskCenterTest`; rebuild — EXIT=0 (`build-9.log`); test from `build/tests` — EXIT=0 (`test-9.log`), 1/1 CTest target passed.

Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4380` initially EXIT=1 (`feature-check-1.log`): the changed test exceeded the file-length ratchet and the existing whole-tree manifest did not match its declared recipe. The regression was moved to the smaller registered `TaskCenterTest`; `tests/all-sources.txt` was regenerated with its own header recipe, removing stale vendored sfizz entries. `bash tests/all-sources-reproduce.sh` then printed REPRODUCES, and the same feature-check command exited 0 (`feature-check-2.log`); all eight rows passed, including complexity and file-length with no new items. The renderer and worker run outside the audio thread; no audio callback changed.

Files in the 4380 fix commit: `src/core/RenderQueue.cpp`, `tests/src/core/TaskCenterTest.cpp`, `tests/all-sources.txt`, and this report.

5270: FIXED

Decision: REAL. Before this fix, `runRender` passed `job.out` directly to the child and `discardOutput` deleted it when its modification time was at least `startedMs`. `src/core/RenderQueue.cpp:93-105` now gives render jobs a unique same-directory staging path that keeps the requested format suffix; `runRender` passes it to the child and `endJob` commits it only after a successful exit, preserving the prior destination on failure or cancellation. `TaskCenterTest::cancellingRenderPreservesPreviousDestination` assigns the existing destination a future mtime after the child is synchronized and cancellation requested, then proves the original bytes survive. The successful sibling test proves the staged file replaces the destination after completion.

Regression commands and exit codes:

- RED build: `cmake --build build --target zene TaskCenterTest` — EXIT=0 (`build-13.log`).
- RED test from `build/tests`: `ctest -R '^TaskCenterTest$' --output-on-failure` — EXIT=8 (`test-13.log`), original destination had been removed.
- GREEN build with the affected header dependents: `cmake --build build --target zene TaskCenterTest ControlRenderJobTest RenderSourceGuardTest` — EXIT=0 (`build-14.log`).
- GREEN test from `build/tests`: `ctest -R '^TaskCenterTest$' --output-on-failure` — EXIT=0 (`test-14.log`), 1/1 CTest target passed.
- 4b removal command: `git diff -- include/RenderQueue.h src/core/RenderQueue.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-5270.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-5270.patch`; rebuild — EXIT=0 (`build-15.log`); test from `build/tests` — EXIT=8 (`test-15.log`), same lost-destination assertion.
- 4b restore command: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-5270.patch && cmake --build build --target zene TaskCenterTest ControlRenderJobTest RenderSourceGuardTest` — EXIT=0 (`build-16.log`); `ctest -R '^(RenderSourceGuardTest|TaskCenterTest|ControlRenderJobTest)$' --output-on-failure` from `build/tests` — EXIT=0 (`test-16.log`), 3/3 affected CTest targets passed.

Files in the 5270 fix commit: `include/RenderQueue.h`, `src/core/RenderQueue.cpp`, `tests/src/core/TaskCenterTest.cpp`, `tests/upstream-modifications.txt`, and this report.

QA follow-up for 5270: the first staging implementation replaced a symlink instead of writing through to its target, and its temporary and backup names exceeded the filesystem component limit for a valid 244-byte output filename. The follow-up stores the render's resolved destination, stages and backs up under short unique names in that destination's directory, and refuses directory destinations before launching the child. `RenderSourceGuardTest::successfulRenderKeepsSymbolicLinkDestination`, `renderToSymbolicLinkDirectoryDoesNotReplaceIt`, and `successfulRenderSupportsLongDestinationNames` cover these cases.

- Reversal proof: after reverting only `include/RenderQueue.h` and `src/core/RenderQueue.cpp` to the submitted branch, `cmake --build build --target zene RenderSourceGuardTest -j12` — EXIT=0 (`qa-reversed-build-2.log`); from `build/tests`, `ctest -R '^RenderSourceGuardTest$' --output-on-failure` — EXIT=8 (`qa-reversed-test-2.log`), all three compatibility assertions failed. Restored the product patch with `git apply` — EXIT=0.
- GREEN: `cmake --build build --target zene TaskCenterTest ControlRenderJobTest RenderSourceGuardTest -j12` — EXIT=0 (`qa-restored-build-2.log`); from `build/tests`, `ctest -R '^(RenderSourceGuardTest|TaskCenterTest|ControlRenderJobTest)$' --output-on-failure` — EXIT=0 (`qa-restored-test-2.log`), 3/3 targets passed.
- QA fix commit: `include/RenderQueue.h`, `src/core/RenderQueue.cpp`, `tests/src/core/RenderSourceGuardTest.cpp`, `tests/upstream-modifications.txt`, and this report.

new-p15-167: FIXED

Decision: REAL. Before this fix, `runConvert` launched `staged(copy)` without a stop signal, and `cancel` only killed `m_process`, which is null for conversions; `m_current` remained occupied until the worker callback. `include/RenderQueue.h:86-87` now carries a shared atomic cancellation token and worker lifecycle flag. `src/core/RenderQueue.cpp:113-148,185-196` checks the token before and between stages and at 4096-frame write chunks; cancellation removes the partial stage. `cancel` signals the token, ends the task and releases the queue immediately. `clearFinished` retains the job until worker cleanup completes, and `finish` ignores stale worker callbacks when deciding whether to delete the current process. `TaskCenterTest::cancellingConversionStopsWorkerAndAdvancesQueue` blocks the pool worker with a semaphore, cancels the conversion, starts a render before releasing that worker, then confirms the cancelled source was not decoded and the render completed.

Regression commands and exit codes:

- RED before fix: `cmake --build build --target zene TaskCenterTest ControlRenderJobTest RenderSourceGuardTest` — EXIT=0 (`build-18.log`); `ctest -R '^TaskCenterTest$' --output-on-failure` from `build/tests` — EXIT=8 (`test-18.log`), `advancedWhileWorkerBlocked` was false.
- GREEN after fix: the same build command — EXIT=0 (`build-19.log`); the same ctest command from `build/tests` — EXIT=0 (`test-19.log`), 1/1 target passed.
- 4b removal: `git diff -- include/RenderQueue.h src/core/RenderQueue.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-new-p15-167.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-new-p15-167.patch`; rebuild — EXIT=0 (`build-21.log`); test from `build/tests` — EXIT=8 (`test-21.log`), same queue-advancement assertion.
- 4b restore: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-new-p15-167.patch && cmake --build build --target zene TaskCenterTest ControlRenderJobTest RenderSourceGuardTest` — EXIT=0 (`build-22.log`); test from `build/tests` — EXIT=0 (`test-22.log`), 1/1 target passed.
- Affected family: `ctest -R '^(RenderSourceGuardTest|TaskCenterTest|ControlRenderJobTest)$' --output-on-failure` from `build/tests` — EXIT=0 (`test-23.log`), 3/3 CTest targets passed.

Files in the new-p15-167 fix commit: `include/RenderQueue.h`, `src/core/RenderQueue.cpp`, `tests/src/core/TaskCenterTest.cpp`, `tests/upstream-modifications.txt`, and this report.

## Self-check

- Reverting each production patch makes its regression fail; restoring it makes the same regression pass. 4380: `test-8.log` EXIT=8, `test-9.log` EXIT=0. 5270: `test-15.log` EXIT=8, `test-16.log` EXIT=0. new-p15-167: `test-21.log` EXIT=8, `test-22.log` EXIT=0. Initial RED runs are `test-6.log`, `test-13.log`, and `test-18.log`, each EXIT=8. Each proof rebuilt the affected targets before running the test.
- The tests use a fake child process and explicit file/semaphore barriers. The render cancellation test waits for the fake child PID file, cancels, and injects a future destination timestamp; conversion cancellation holds the global pool worker with semaphores and gates the next fake child with a release file. No assertion depends on elapsed time or sleeps; bounded Qt wait helpers only prevent a hung test. See `tests/src/core/TaskCenterTest.cpp:403-529`.
- The 4380 fix covers the render process completion and failed-start paths in `runRender`. The 5270 fix covers both render and conversion staging, the shared success commit in `finish`, and both cancellation/failure cleanup paths; the direct `convert` helper also commits through the same staging helper. The conversion cancellation token is checked before and between decode/resample/write stages; both worker completion and queue release are handled in `finish`/`cancel`. Evidence is in `src/core/RenderQueue.cpp:62-197,232-240,334-384,387-426` and the matching declaration in `include/RenderQueue.h:82-87`. The affected test family passed 3/3 (`test-16.log`, `test-23.log`, both EXIT=0).
- Existing project and saved-data formats are unchanged: these changes touch render job runtime state and output-file handling only; no project serialization or load path changed. The tests verify preservation of prior output on cancellation and replacement on success. No new limit was added. Final feature gate command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4380` — EXIT=0 (`feature-check-5.log`); all eight rows passed.
- The conditional check for ALREADY-FIXED or FALSE-POSITIVE does not apply: all three findings were REAL and are marked FIXED above, with code and regression evidence.
