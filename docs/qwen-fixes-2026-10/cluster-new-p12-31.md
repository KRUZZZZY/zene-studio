# Cluster new-p12-31

**Verdict: FIXED**

`render.render` previously accepted an output path only when its first character
was `/`. `QFileInfo(*out).isAbsolute()` now applies the host's native path
rules, so Windows drive and UNC paths are accepted on Windows while relative
paths remain refused.

## Evidence

- [ControlCommandsProject.cpp:216](../../src/core/ControlCommandsProject.cpp#L216)
  uses `QFileInfo(*out).isAbsolute()` before rendering.
- [ControlRenderOutputPathTest.cpp:84](../../tests/src/core/ControlRenderOutputPathTest.cpp#L84)
  invokes `render.render` with a native absolute path. The Windows build uses
  `C:\Music\mix.wav`; the Linux runner uses an absolute temporary path. Its
  source guard makes this Windows-only regression fail on Linux when the
  POSIX-only check is present.
- The Windows runtime path was not executable in this Linux environment. The
  local test's RED/GREEN and reversal results below prove the source guard
  catches this fix; a Windows run will additionally exercise the drive-path
  call through `ControlRegistry`.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Harness setup build (`build-2.log`) | `cmake --build build --target zene ControlRenderOutputPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/build-2.log 2>&1; build_status=$?; echo EXIT=$build_status` | 2 | Initial test compilation exposed a missing `Song.h` include; fixed before the acceptance RED. |
| Harness source-root attempt (`test-1.log`) | `ctest -R '^ControlRenderOutputPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/test-1.log 2>&1; test_status=$?; echo EXIT=$test_status` | 8 | Test failed to find the production source from the test binary's relative `__FILE__`. |
| Harness source-root retry (`test-2.log`) | `ctest -R '^ControlRenderOutputPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/test-2.log 2>&1; test_status=$?; echo EXIT=$test_status` | 8 | Same source-root failure; fixed with the configured source-root definition. |
| Harness source-boundary retry (`test-3.log`) | `ctest -R '^ControlRenderOutputPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/test-3.log 2>&1; test_status=$?; echo EXIT=$test_status` | 8 | Source function boundary marker was corrected before acceptance RED. |
| Pre-fix build (`build-6.log`) | `cmake --build build --target zene ControlRenderOutputPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/build-6.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 | Original production code and final regression test built before RED. |
| RED on original production code (`test-4.log`) | `ctest -R '^ControlRenderOutputPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/test-4.log 2>&1; test_status=$?; echo EXIT=$test_status` | 8 | The source guard failed on the slash-only check; the native-path command reached the empty-session refusal. |
| Post-fix build (`build-7.log`) | `cmake --build build --target zene ControlRenderOutputPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/build-7.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 | Both affected targets rebuilt. |
| GREEN after fix (`test-5.log`) | `ctest -R '^ControlRenderOutputPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/test-5.log 2>&1; test_status=$?; echo EXIT=$test_status` | 0 | Test passed. |
| Reversal, remove product fix | `git diff -- src/core/ControlCommandsProject.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/fix.patch; revert_status=$?; echo EXIT=$revert_status` | 0 | Product patch was removed. |
| Reversal build (`build-8.log`) | `cmake --build build --target zene ControlRenderOutputPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/build-8.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 | Rebuilt without the product fix. |
| Reversal RED (`test-6.log`) | `ctest -R '^ControlRenderOutputPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/test-6.log 2>&1; test_status=$?; echo EXIT=$test_status` | 8 | The same source-guard assertion failed again. |
| Reversal, restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/fix.patch; apply_status=$?; echo EXIT=$apply_status` | 0 | Product patch was restored. |
| Reversal build (`build-9.log`) | `cmake --build build --target zene ControlRenderOutputPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/build-9.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 | Rebuilt with the product fix. |
| Reversal GREEN (`test-7.log`) | `ctest -R '^ControlRenderOutputPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/test-7.log 2>&1; test_status=$?; echo EXIT=$test_status` | 0 | Same test passed. |
| Render-family CTest (`test-8.log`) | `ctest -R 'ControlRender' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/test-8.log 2>&1; test_status=$?; echo EXIT=$test_status` | 0 | 3 tests passed: `ControlRenderOutputPathTest`, `ControlRenderPresets`, and `ControlRenderFormatCapability`. |
| Scope manifests (`gate-8.log`) | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/gate-8.log 2>&1; gate_status=$?; echo EXIT=$gate_status` | 0 | Fork and whole-tree source inventories reproduce, including the new test. |
| Complexity gate (`gate-9.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/gate-9.log 2>&1; gate_status=$?; echo EXIT=$gate_status` | 0 | Passed. |
| File-length gate (`gate-10.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-31.r1/logs/gate-10.log 2>&1; gate_status=$?; echo EXIT=$gate_status` | 0 | Passed. |

## Files in this commit

- `src/core/ControlCommandsProject.cpp`
- `tests/src/core/ControlRenderOutputPathTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-31.md`
