# Cluster new-p12-174

**Verdict: FIXED**

`relativeOrAbsolute()` treated every relative result beginning with `..` as outside its base. That incorrectly rejected valid names such as `..ambience.wav`. The check in `src/core/PathUtil.cpp:195-200` now treats only `..` and paths beginning with `../` as parent-directory paths. The regression in `tests/src/core/RelativePathsTest.cpp:69-78` checks both the selected factory-sample base and the resulting shortest relative path.

## Evidence

| Check | Exact command (working directory) | Exit | Evidence |
| --- | --- | ---: | --- |
| Original-code RED | `ctest -R '^RelativePathsTest$' --output-on-failure` (`build/tests`) | 8 | `test-1.log`: regression failed; actual value was the absolute factory-sample path, expected `..ambience.wav`. |
| Fixed-code GREEN | `ctest -R '^RelativePathsTest$' --output-on-failure` (`build/tests`) | 0 | `test-2.log`: 1/1 ctest tests passed. |
| Reverse-patch RED | `ctest -R '^RelativePathsTest$' --output-on-failure` (`build/tests`) | 8 | `test-3.log`: same regression failed after removing the production patch. |
| Reapplied-patch GREEN | `ctest -R '^RelativePathsTest$' --output-on-failure` (`build/tests`) | 0 | `test-4.log`: 1/1 ctest tests passed after reapplying the patch. |
| Final GREEN | `ctest -R '^RelativePathsTest$' --output-on-failure` (`build/tests`) | 0 | `test-5.log`: 1/1 ctest tests passed after final edits. |
| Affected-family count | `ctest -R '^RelativePathsTest$' --output-on-failure` (`build/tests`) | — | 1 ctest test executed per run; zero-test runs: none. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (worktree root) | 0 | `gate-3.log`. |
| File-length gate | `bash tests/file-length-gate.sh --check` (worktree root) | 0 | `gate-4.log`. |

## Commands and results

All build and test output was captured under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-174.r1/logs`.

- `cmake --build build --target RelativePathsTest` — EXIT=0 (`build-1.log`, `build-2.log`, `build-3.log`, `build-4.log`, `build-6.log`).
- `cmake --build build --target zene` — EXIT=0 (`build-5.log`, `build-7.log`).
- `git diff -- src/core/PathUtil.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-174.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-174.r1/logs/fix.patch` — EXIT=0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-174.r1/logs/fix.patch` — EXIT=0.
- `bash tests/complexity-gate.sh --check` — EXIT=0 (`gate-3.log`).
- `bash tests/file-length-gate.sh --check` — EXIT=0 (`gate-4.log`).
- `git diff --check` — EXIT=0.

## Files committed

- `src/core/PathUtil.cpp`
- `tests/src/core/RelativePathsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-174.md`
