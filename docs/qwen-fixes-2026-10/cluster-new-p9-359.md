# Cluster new-p9-359 — FIXED

## Finding and change

`include/PlayHandle.h:165` declares `m_audioBusHandle` without an in-class initializer. The
constructor at `src/core/PlayHandle.cpp:37-45` now initializes it to `nullptr` at line 44.
`tests/src/core/AudioBusHandleTest.cpp:134-146` adds `unsetAudioBusHandleIsNull()`, which
constructs a handle over poisoned storage and checks the public getter. The original constructor
failed that assertion; the fixed constructor passes it.

The existing product-added test source is now listed in `tests/fork-sources.txt:1908`. The
inherited production change is recorded in `tests/upstream-modifications.txt:700`.

## Verification

Build commands ran from the worktree root. CTest commands ran from `build/tests`. Build and test
output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs`.

| Evidence | Exact command and result |
| --- | --- |
| RED on original code | `ctest -R '^AudioBusHandleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) — EXIT=8. |
| GREEN after fix | `ctest -R '^AudioBusHandleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) — EXIT=0. |
| Affected family | `ctest -R '^(AudioBusHandleTest|SampleClipWindowTest|MixerConcurrencyTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) — EXIT=0; 3/3 CTest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0. |
| Explicit committed files | `src/core/PlayHandle.cpp`<br>`tests/src/core/AudioBusHandleTest.cpp`<br>`tests/fork-sources.txt`<br>`tests/upstream-modifications.txt`<br>`docs/qwen-fixes-2026-10/cluster-new-p9-359.md` |

### RED on the original constructor

- `cmake --build build --target zene AudioBusHandleTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `ctest -R '^AudioBusHandleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=8. `unsetAudioBusHandleIsNull()` reported that `handle->audioBusHandle() == nullptr` returned false.

### GREEN with the fix

- `cmake --build build --target zene AudioBusHandleTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `ctest -R '^AudioBusHandleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.

### Sensitivity proof

- `git diff -- src/core/PlayHandle.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `cmake --build build --target zene AudioBusHandleTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `ctest -R '^AudioBusHandleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=8.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `cmake --build build --target zene AudioBusHandleTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `ctest -R '^AudioBusHandleTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.

### Affected test family and gates

- `cmake --build build --target zene AudioBusHandleTest SampleClipWindowTest MixerConcurrencyTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `ctest -R '^(AudioBusHandleTest|SampleClipWindowTest|MixerConcurrencyTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0; 3/3 CTest tests passed.
- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-359.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.

The `fff grep` tool/CLI was unavailable in this environment; captured logs were searched with
`rg` for `error`, `FAILED`, and `Assertion`. The `FAILED` matches are only the expected RED runs
in `test-1.log` and `test-3.log`.

## Files committed

- `src/core/PlayHandle.cpp`
- `tests/src/core/AudioBusHandleTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-359.md`
