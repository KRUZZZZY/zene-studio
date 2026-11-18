# Cluster new-p9-248: WhiteNoise LFO initial sample

Verdict: **FIXED**

## Evidence

`include/LfoController.h:86` declares `m_heldSample` without an in-class initializer. On the first `updateValueBuffer()` pass, `src/core/LfoController.cpp:115-123` uses the held value unless the phase has wrapped. The constructor now initializes it at `src/core/LfoController.cpp:53`, and selecting WhiteNoise draws the held sample at `src/core/LfoController.cpp:201-204`.

The regression in `tests/src/core/UndoHonestyTest.cpp:383-398` fills raw object storage with `0xFF`, selects WhiteNoise through `loadSettings()`, advances one frame period, and checks that the first output is finite and differs from the base value. Against the original production code it fails at `std::isfinite(firstValue)`. With the fix it passes. The reverse-patch proof repeats the same fail/pass pair.

Affected family: `UndoHonestyTest`, **1/1 registered CTest tests executed** on each run.

## Commands and exit codes

All build, test, and gate output was captured in new numbered files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-248.r1/logs`. `fff grep` was not available as a tool or shell command in this session; log checks used one-term `rg` searches instead.

| Command | Exit code | Log |
| --- | ---: | --- |
| `cmake --build build --target zene` | 0 | `build-1.log`, `build-4.log`, `build-6.log`, `build-8.log` |
| `cmake --build build --target UndoHonestyTest` | 0 | `build-2.log`, `build-3.log`, `build-5.log`, `build-7.log`, `build-9.log` |
| From `build/tests`: `QT_QPA_PLATFORM=offscreen ctest -R '^UndoHonestyTest$' --output-on-failure` — original-code RED (initial fixture and corrected fixture) | 8 | `test-1.log`, `test-2.log` |
| Same CTest command — fixed-code GREEN | 0 | `test-3.log` |
| `git diff -- src/core/LfoController.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-248.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-248.r1/logs/fix.patch` | 0 | — |
| Same CTest command after reverse patch (must fail) | 8 | `test-4.log` |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-248.r1/logs/fix.patch` | 0 | — |
| Same CTest command after restoring patch (must pass) | 0 | `test-5.log` |
| `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |
| `git diff --cached --check` | 0 | — |

The app and test target builds were run between each RED/GREEN CTest invocation as recorded above. CTest was always run from `build/tests`; every CTest invocation ran one registered test, so none were zero-test runs.

## Files in the fix commit

- `src/core/LfoController.cpp`
- `tests/src/core/UndoHonestyTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-248.md`
