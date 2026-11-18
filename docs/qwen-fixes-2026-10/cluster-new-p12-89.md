# Cluster new-p12-89: FIXED

## Finding

`plugins/GranularPitchShifter/GranularPitchShifterControls.h:54` returned `4` although the class declares 15 models: 13 `FloatModel`s, one `BoolModel`, and one `ComboBoxModel`. The method now returns `15`. The inherited header change is recorded in `tests/upstream-modifications.txt:632`.

The regression at `tests/src/plugins/PluginPortsMigrationTest.cpp:220-238` loads the shipped granular pitch shifter module and checks its public `controlCount()`. `src/gui/EffectView.cpp:82` currently uses this value as a visibility predicate (`> 0`); it does not enumerate controls. The API value itself was incorrect.

The suggested CTest name `GranularPitchShifterControls` is not registered in this configured build. `ctest -N` found zero matches. I used the registered `PluginPortsMigrationTest`, which loads the affected production module.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Original behavior, isolated regression | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/test-2.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` | 8 | RED: actual `4`, expected `15`; one CTest test ran. QtTest totals: 8 passed, 1 failed, 0 skipped. |
| Fixed behavior | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/test-3.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` | 0 | GREEN: 1/1 CTest tests passed. |
| Reverse-patch proof | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/test-4.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` | 8 | RED again after removing the production change; actual `4`, expected `15`. |
| Reapplied-patch proof | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/test-5.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` | 0 | GREEN again: 1/1 CTest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/gate-3.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit $gate_status` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/gate-4.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit $gate_status` | 0 | Passed. |

Affected-family count: one registered CTest test, `PluginPortsMigrationTest`, containing nine QtTest lifecycle/slots. The isolated RED run had only the intended count assertion fail; all other eight passed.

## Command record

Every build below exited `0` and rebuilt `zene` plus the affected test target. The final builds also rebuilt `granularpitchshifter` from the changed header.

| Log | Exact build command | Exit |
|---|---|---:|
| `build-1.log` | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/build-1.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` | 0 |
| `build-2.log` | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/build-2.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` | 0 |
| `build-3.log` | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/build-3.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` | 0 |
| `build-4.log` | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/build-4.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` | 0 |
| `build-5.log` | `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/build-5.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` | 0 |

The first regression placement was inside the migration render slot. Its command returned `8` and the early assertion prevented later slots from receiving their render data. I moved the count assertion into a dedicated slot; the isolated RED result above is the accepted regression proof.

| Log | Exact test command | Exit |
|---|---|---:|
| `test-1.log` | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/test-1.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` | 8 |

The CTest-name discovery command was `ctest -N -R '^GranularPitchShifterControls$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/inspect-1.log 2>&1; echo EXIT=$?` (exit `0`; the log reports `Total Tests: 0`). No zero-test CTest run was used as test evidence.

The reversal command was `git diff -- plugins/GranularPitchShifter/GranularPitchShifterControls.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/fix.patch; reverse_status=$?; echo EXIT=$reverse_status; exit $reverse_status` (exit `0`). The restore command was `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/fix.patch; apply_status=$?; echo EXIT=$apply_status; exit $apply_status` (exit `0`).

Both gates also passed before the metadata entry was added: `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/gate-1.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit $gate_status` (exit `0`) and `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-89.r1/logs/gate-2.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit $gate_status` (exit `0`). The `gate-3.log` and `gate-4.log` runs above are the final gate runs after all code and metadata changes.

The `fff` grep tool was not available in this session, so build/test logs were checked with `rg` for `error`, `FAILED`, and `Assertion`.

## Files in the commit

- `plugins/GranularPitchShifter/GranularPitchShifterControls.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-89.md`
