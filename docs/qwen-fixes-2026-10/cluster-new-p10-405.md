# Cluster new-p10-405

**Verdict: FIXED**

`src/core/TakeLane.cpp:90-114` now requires integer, non-negative `begin`, `end`, and `lane` attributes, and validates `slip` when present while preserving its optional zero default. `src/core/TakeLane.cpp:423-428` drops any segment that fails parsing, has an empty range, or names an unavailable lane. The regression in `tests/src/core/TakeLaneCompTest.cpp:116-164` covers malformed, negative, and absent values against a track with lane 0.

## Regression evidence

| Run | Exact command | EXIT | Result |
|---|---|---:|---|
| RED on the original parser | `ctest -R '^TakeLaneCompTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 8 | The regression assertion failed seven times. |
| Final negative control, parser patch reversed | `ctest -R '^TakeLaneCompTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/test-6.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 8 | After rebuilding without the production patch, the same regression assertion failed eight times. |
| GREEN after restoring the final patch | `ctest -R '^TakeLaneCompTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/test-7.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 | 1/1 registered CTest cases passed. |

The affected family is `TakeLaneCompTest`: **1 registered CTest case**, which runs the test executable including the new regression slot.

## Gates

| Gate command | EXIT | Log |
|---|---:|---|
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 1 | `gate-1.log` (the initial parser scored CCN 13) |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 | `gate-2.log` |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/gate-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 | `gate-3.log` |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/gate-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 | `gate-4.log` |

The first complexity check reported the initial parser's CCN as 13 (EXIT 1); the validation was split into a small helper, and the final complexity gate passed. The first file-length check also passed (EXIT 0).

## Build and negative-control commands

All command output was redirected to new numbered files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs`. `EXIT` is the status printed by each command wrapper.

| Log | Exact command | EXIT |
|---|---|---:|
| `build-1.log` | `cmake --build build --target TakeLaneCompTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `build-2.log` | `cmake --build build --target zene TakeLaneCompTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `test-2.log` | `ctest -R '^TakeLaneCompTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `fix.patch` reverse | `git diff -- src/core/TakeLane.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `build-3.log` | `cmake --build build --target zene TakeLaneCompTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `test-3.log` | `ctest -R '^TakeLaneCompTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 8 |
| `fix.patch` apply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `build-4.log` | `cmake --build build --target zene TakeLaneCompTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `test-4.log` | `ctest -R '^TakeLaneCompTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `build-5.log` | `cmake --build build --target zene TakeLaneCompTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/build-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `test-5.log` | `ctest -R '^TakeLaneCompTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/test-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Final `fix.patch` reverse | `git diff -- src/core/TakeLane.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `build-6.log` | `cmake --build build --target zene TakeLaneCompTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/build-6.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `fix.patch` apply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| `build-7.log` | `cmake --build build --target zene TakeLaneCompTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-405.r1/logs/build-7.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |

## Files committed

- `src/core/TakeLane.cpp`
- `tests/src/core/TakeLaneCompTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-405.md`
