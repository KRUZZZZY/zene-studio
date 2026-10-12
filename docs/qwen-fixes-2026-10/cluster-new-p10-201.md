# Cluster new-p10-201: malformed keymap offset overflow

Verdict: **FIXED**

## Evidence

- `src/core/Keymap.cpp:81-85`: `getDegree()` widens the key offset and modulo arithmetic to `std::int64_t` before indexing the map.
- `src/core/Keymap.cpp:99-112`: `getOctave()` widens its offset and calculation, then safely bounds the result to the public `int` return type.
- `src/core/Keymap.cpp:151-154`: project XML key fields are clamped to the same MIDI range used by `.kbm` loading.
- `tests/src/core/SessionTuningTest.cpp:913-942`: the regression loads `middle_key = INT_MIN`, checks XML normalization and mapping, and separately checks the wide arithmetic through the constructor.

The regression fails on the original production code and passes after the fix. The reverse-patch proof repeats that result.

## Regression and proof

`ctest -R '^SessionTuningTest$' --output-on-failure` was run from `build/tests` throughout. The registered affected family is one CTest test (`SessionTuningTest`). With its real instrument dependency built, RED recorded 14 passed and 1 failed Qt test case; GREEN passed the registered test (1/1). Removing the production patch reproduced the same 14/1 RED result; restoring it returned to GREEN.

## Build and test commands

Commands ran in the worktree unless marked `build/tests`. Build/test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-201.r1/logs/`.

| Log | Command | Exit | Result |
| --- | --- | ---: | --- |
| `build-1.log` | `cmake --build build --target zene SessionTuningTest -j12` | 0 | Initial build succeeded. |
| `test-1.log` (`build/tests`) | `ctest -R '^SessionTuningTest$' --output-on-failure` | 8 | Regression failed; an existing instrument-render slot also failed because the plugin module was not built yet. |
| `build-2.log` | `cmake --build build --target tripleoscillator zene SessionTuningTest -j12` | 0 | Built the test's real instrument dependency. |
| `test-2.log` (`build/tests`) | `ctest -R '^SessionTuningTest$' --output-on-failure` | 8 | RED: 14 passed, regression failed. |
| `build-3.log` | `cmake --build build --target tripleoscillator zene SessionTuningTest -j12` | 0 | Rebuilt after the fix. |
| `test-3.log` (`build/tests`) | `ctest -R '^SessionTuningTest$' --output-on-failure` | 0 | GREEN: 1/1 registered test passed. |
| `git diff -- src/core/Keymap.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-201.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-201.r1/logs/fix.patch` | 0 | Removed only the production fix for the negative proof. |
| `build-4.log` | `cmake --build build --target tripleoscillator zene SessionTuningTest -j12` | 0 | Rebuilt without the production fix. |
| `test-4.log` (`build/tests`) | `ctest -R '^SessionTuningTest$' --output-on-failure` | 8 | Reverse-patch RED: 14 passed, regression failed. |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-201.r1/logs/fix.patch` | 0 | Restored the production fix. |
| `build-5.log` | `cmake --build build --target tripleoscillator zene SessionTuningTest -j12` | 0 | Rebuilt with the production fix restored. |
| `test-5.log` (`build/tests`) | `ctest -R '^SessionTuningTest$' --output-on-failure` | 0 | Reverse-patch recovery GREEN: 1/1 registered test passed. |
| `gate-1.log` | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| `gate-2.log` | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

The fff grep tool was not exposed in this session and no `fff` CLI was installed; build/test logs were searched with `rg` for `error`, `FAILED`, and `Assertion`, then for test result lines.

## Files included in the commit

- `src/core/Keymap.cpp`
- `tests/src/core/SessionTuningTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-201.md`
