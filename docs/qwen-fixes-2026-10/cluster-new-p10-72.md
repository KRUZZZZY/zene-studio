# Cluster new-p10-72: BLOCKED

## Finding and fix

The finding is real. `handleSetParam` in [ControlCommandsWasmEdit.cpp:199](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-72/src/core/ControlCommandsWasmEdit.cpp:199) now rejects nonfinite values and values outside the finite `float` range before casting or calling `setParam()`. The regression in [control-wasm-sandbox.py:276](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-72/tests/control-wasm-sandbox.py:276) checks that `1e300` returns `invalid_args` and leaves slot 0 unchanged.

## Verification status

Luna's requested WASM behavioral proof is still blocked by the instruction not to configure this build. The pinned Wasmtime C API was fetched into `third_party/wasmtime`, and rebuilding the existing tree succeeded, but `build/CMakeCache.txt` still records `Wasmtime_INCLUDE_DIR` and `Wasmtime_LIBRARY` as `NOTFOUND`. The generated compile database omits `ControlCommandsWasmEdit.cpp`, and `tests/CMakeLists.txt` registers `ControlWasmSandbox` only under `if(WANT_WASM)` (around line 3226). CTest therefore ran zero tests. Reversing and restoring the product hunk in this tree would still run zero tests, so it cannot provide the requested RED/GREEN proof; no hunk reversal is claimed.

The current session does not expose the requested `fff` grep tool. Build and test logs were searched with `rg` one term at a time; no `error`, `FAILED`, or `Assertion` matches were found in the build or CTest logs.

## Evidence

Commands ran from the worktree root except CTest, which ran from `build/tests`. Build and CTest output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r3/logs/`.

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Fetch pinned Wasmtime | `scripts/fetch-wasmtime.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r3/logs/fetch-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 | Wasmtime C API installed in the worktree. |
| Build existing configuration with fix | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r3/logs/build-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 | Build succeeds, but the generated target excludes the WASM handler. |
| Targeted CTest | `ctest -R ControlWasmSandbox --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r3/logs/test-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 | CTest says `No tests were found!!!`; zero tests executed. This is not a pass. |
| Cache check | `rg -n "WANT_WASM|Wasmtime_INCLUDE_DIR|Wasmtime_LIBRARY" build/CMakeCache.txt` | 0 | `WANT_WASM=ON`; both Wasmtime paths remain `NOTFOUND`. |
| Complexity gate, final | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r3/logs/gate-complexity-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 | Passed. |
| File-length gate, final | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r3/logs/gate-file-length-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 | Passed. |

**Affected-family test count:** 0 registered and 0 executed. Valid RED: not run. Valid GREEN: not run.

**Files committed:** the existing fix commit `5fe81e3d7` contains `src/core/ControlCommandsWasmEdit.cpp`, `tests/control-wasm-sandbox.py`, `tests/upstream-modifications.txt`, and this report. This follow-up commit updates only `docs/qwen-fixes-2026-10/cluster-new-p10-72.md` with the current blocker evidence.
