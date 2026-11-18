# Cluster new-p10-315: `Piano::isKeyPressed()` bounds

Verdict: **FIXED**

`include/Piano.h:53-56` now checks `isValidKey(key)` before indexing `m_pressedKeys`. Invalid keys return `false`; valid-key behavior is unchanged. `tests/src/gui/PianoRollBenchmarkTest.cpp:120-129` adds a deterministic boundary regression: a true guard byte follows the `Piano`, and queries for `-1` and `NumKeys` must both return false. The inherited header change is recorded in `tests/upstream-modifications.txt:1054`.

## Test evidence

The affected ctest family contains one CTest test, `PianoRollBenchmarkTest`. Each green run reports one test passed out of one; each red run reports that same test failed. The failing QtTest assertion was `invalidKeysAreNotPressed()` at the `NumKeys` query.

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED, original code | `cd build/tests && ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/test-1.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 | `invalidKeysAreNotPressed()` failed; QtTest totals: 3 passed, 1 failed. |
| GREEN, fixed code | `cd build/tests && ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/test-2.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 0 | 1/1 CTest tests passed. |
| RED, after removing the production patch | `cd build/tests && ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/test-3.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 | The same assertion failed after rebuilding both named targets. |
| GREEN, after restoring the patch | `cd build/tests && ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/test-4.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 0 | 1/1 CTest tests passed. |

## Build, reversal, and gates

| Exact command | Exit |
| --- | ---: |
| `cmake --build build --target PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/build-1.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `cmake --build build > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/build-2.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 97 |
| `cmake --build build --target zene PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/build-3.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `git diff -- include/Piano.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/fix.patch; patch_rc=$?; echo EXIT=$patch_rc` | 0 |
| `cmake --build build --target zene PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/build-4.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/fix.patch; patch_rc=$?; echo EXIT=$patch_rc` | 0 |
| `cmake --build build --target zene PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/build-5.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/gate-1.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-315.r1/logs/gate-2.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc` | 0 |

The full build command was refused by `bunny-guard` with `build a named target`; rebuilding the named production and test targets succeeded. The `fff` grep tool was unavailable in this session, so log terms `error`, `FAILED`, and `Assertion` were searched with `rg` instead.

## Files committed

- `include/Piano.h`
- `tests/src/gui/PianoRollBenchmarkTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-315.md`
