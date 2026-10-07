# Cluster new-p8-70

Verdict: **FIXED**

## Finding and code evidence

`saveVersion` in `src/core/ControlCommandsTrackVersions.cpp:89-92` now captures the live clips once, stores them under the previously active version when one exists, then stores the same snapshot under the requested name before changing `active`. `switchVersion` already refreshed the outgoing snapshot at line 112, which established the behavior this save path needed to match.

The regression in `tests/src/core/ArrangeToolsTest.cpp:137-153` saves A, edits its live clips, saves B, switches back to A, and checks that both edits return. The related existing switch test at lines 111-125 now also expects the edited snapshot.

## Red/green and negative-control evidence

The affected family is one CTest case (`ArrangeToolsTest`). Its red QtTest output reports 13 results: 11 passed and 2 failed. Both green CTest runs pass the target 1/1. No run reported zero tests.

| Stage | Exact command | Exit | Evidence log |
|---|---|---:|---|
| Initial RED, original production code | `cd build/tests && ctest -R ArrangeToolsTest --output-on-failure` | 8 | `test-1.log`: regression got 1 clip, expected 2; 11 passed, 2 failed |
| GREEN, production fix applied | `cd build/tests && ctest -R ArrangeToolsTest --output-on-failure` | 0 | `test-2.log`: 100% passed, 1/1 |
| Negative-control RED, production patch removed | `cd build/tests && ctest -R ArrangeToolsTest --output-on-failure` | 8 | `test-3.log`: same 1-versus-2 regression; 11 passed, 2 failed |
| Proof GREEN, production patch reapplied | `cd build/tests && ctest -R ArrangeToolsTest --output-on-failure` | 0 | `test-4.log`: 100% passed, 1/1 |

Build command for each of `build-1.log` through `build-4.log`: `cmake --build build --target zene ArrangeToolsTest` — EXIT=0 each time. This rebuilt `zene` and the affected test target before every CTest run.

Patch removal command: `git diff -- src/core/ControlCommandsTrackVersions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-70.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-70.r1/logs/fix.patch` — EXIT=0.

Patch reapplication command: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-70.r1/logs/fix.patch` — EXIT=0.

## Gates and checks

| Command | Exit | Evidence log |
|---|---:|---|
| `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |
| `git diff --check` | 0 | command output |

The fff grep tool was not exposed in this session, so log checks used targeted one-term `rg` searches rather than printing whole logs.

## Files committed

- `src/core/ControlCommandsTrackVersions.cpp`
- `tests/src/core/ArrangeToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-70.md`
