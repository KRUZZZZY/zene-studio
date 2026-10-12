# Cluster new-p10-144: Dynamics Processor wave-shape length

Verdict: **FIXED**

`DynProcControls::loadSettings()` decoded the `waveShape` attribute and passed the pointer to a 200-sample `graphModel` without checking how many bytes were decoded. A missing or short attribute could therefore make `graphModel::setSamples()` read past the allocation. At [plugins/DynamicsProcessor/DynamicsProcessorControls.cpp:89](../../plugins/DynamicsProcessor/DynamicsProcessorControls.cpp), the loader now calls `setSamples()` only when the decoded size exactly matches the model's 200 floats; all other sizes restore the default curve.

The regression at [tests/src/plugins/PluginPortsMigrationTest.cpp:406](../../tests/src/plugins/PluginPortsMigrationTest.cpp) loads a one-float shape through the real module and checks that the saved state contains the default curve. The production file is recorded in `tests/upstream-modifications.txt:629` with the reason for the change.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED on original production code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 8 | `dynamicsProcessorRejectsShortWaveShape()` failed its saved-shape comparison. |
| GREEN with fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 0 | Passed, 1/1 registered CTest tests. |
| RED after removing production patch | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 8 | The same assertion failed again. |
| GREEN after restoring production patch | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 0 | Passed, 1/1 registered CTest tests. |
| Affected-family test count | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 0 | 1/1 CTest tests ran. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 | Passed. |

The requested `DynamicsProcessorControls` CTest name is not registered in this configured tree: `ctest -N -R 'DynamicsProcessor'` reported `Total Tests: 0`. The existing `PluginPortsMigrationTest` is the registered test that loads the real Dynamics Processor module and exercises this load/save path.

## Commands and results

Commands ran from the worktree root unless stated otherwise. Build output was captured in the listed new log files. The first build attempt compiled the initial regression with two test-source errors (`base64` namespace and a const `QDomElement`); those test-source issues were corrected before any test run.

The `fff grep` tool was not exposed in this session, and no `fff` executable was available. I searched each build, test, and gate log with `rg -n` using the requested terms (`error`, `FAILED`, and `Assertion`) instead of reading whole logs.

| Exact command | Exit |
|---|---:|
| `ctest -N -R 'DynamicsProcessor' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/inventory-1.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 0 |
| `cmake --build build --target zene PluginPortsMigrationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | 2 |
| `cmake --build build --target zene PluginPortsMigrationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `cmake --build build --target zene PluginPortsMigrationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `git diff -- plugins/DynamicsProcessor/DynamicsProcessorControls.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| `cmake --build build --target zene PluginPortsMigrationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| `cmake --build build --target zene PluginPortsMigrationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-144.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result` | 0 |

## Files committed

- `plugins/DynamicsProcessor/DynamicsProcessorControls.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-144.md`
