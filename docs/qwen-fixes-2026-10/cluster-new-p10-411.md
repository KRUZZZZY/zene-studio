# Cluster new-p10-411: malformed fallback template recursion

**Verdict: FIXED**

## Finding and evidence

When the initial project load fails during launch, `Song::loadProject()` used to call `createNewProject()` while `m_loadOnLaunch` was still true. If `createNewProject()` selected a malformed default template, its nested `loadProject()` entered the same fallback branch again. The failed-load branch now clears `m_loadOnLaunch` before attempting the fallback, so the template gets one attempt.

The fix is at `src/core/Song.cpp:1814-1818`. The regression is `ImportFilterTest::aBrokenLaunchTemplateCannotReenterFallback` at `tests/src/core/ImportFilterTest.cpp:77-130`. It gives a child process a malformed user `default.mpt`, then triggers launch fallback with a missing project. The child has a bounded stack, so the old recursion is isolated and produces a deterministic `CrashExit`.

| Evidence | Command / result |
|---|---|
| RED, original source | From `build/tests`: `ctest -R '^ImportFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=8; the child crashed in the fallback regression. |
| RED, reversal proof | After reversing only `src/core/Song.cpp`, the same CTest command wrote `test-4.log` and exited 8; `QProcess::exitStatus()` was `CrashExit`. |
| GREEN, fixed source | After reapplying the fix and rebuilding, the same CTest command wrote `test-5.log` and exited 0. |
| Affected family count | 1 registered CTest test executed; GREEN was 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=1. The reported over-target functions are in other files, including `XyPadPairTest.cpp` and `DawProjectReadTracks.cpp`; neither changed file is named. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=1. The reported over-length files are unrelated to this change; neither changed file is named. |

## Build and proof commands

- `cmake --build build --target ImportFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0 (regression built before the fix).
- `cmake --build build --target ImportFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0 (fixed-source GREEN run).
- `git diff -- src/core/Song.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/fix.patch` — EXIT=0; the following build and reversal RED run used the reverted source.
- `cmake --build build --target zene ImportFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0 (reversal proof build).
- Reversal RED: `ctest -R '^ImportFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=8.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene ImportFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/build-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0 (after reapplying the fix).
- Reversal GREEN: `ctest -R '^ImportFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-411.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0.
- Both ratchet gates were rerun against the completed change set as `gate-3.log` and `gate-4.log`; their exit codes are recorded above. Their baselines were not changed.

The `fff` grep tool was unavailable in this session; build and test logs were searched with `rg` in the specified log folder.

## Files committed

- `src/core/Song.cpp`
- `tests/src/core/ImportFilterTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-411.md`
