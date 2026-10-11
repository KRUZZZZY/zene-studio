# Cluster new-p10-72: BLOCKED

## Finding and evidence

The finding is present in source: [ControlCommandsWasmEdit.cpp](../../src/core/ControlCommandsWasmEdit.cpp#L196) reads the JSON value as a `double`, narrows it to `float` on line 197, then passes that value to `host.setParam()` on line 200. A finite input such as `1e300` can overflow during that narrowing. The source-level behavior needs a regression before it can be fixed safely.

This worktree's configured build cannot exercise that path. [CMakeLists.txt](../../CMakeLists.txt#L1015) disables `WANT_WASM` when Wasmtime is unavailable; `build/CMakeCache.txt:767` and `:770` report the Wasmtime include directory and library as `NOTFOUND`. Consequently, `build/tests/CTestTestfile.cmake` contains no `ControlWasmSandbox`, `WasmSandboxTest`, or `WasmAbiConformanceTest` entry. Rebuilding `zene` succeeded, but its control surface does not register any `wasm.*` command.

The closest available transcript ran one script and produced 40 check results. It failed because all six `wasm.*` IDs were missing, before it could test the reported overflow. That is not valid RED evidence for this bug. No product or test files were changed, and no GREEN run is claimed. The affected CTest family has 0 registered tests; no zero-test CTest run was attempted.

The requested `fff` grep tool was not available in this session, so build and test logs were searched with `rg` instead.

## Verification evidence

| Evidence | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED attempt (capability probe only; not a valid bug regression) | `python3 ../../tests/control-wasm-sandbox.py ../zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 1 | All wasm command IDs missing; the infinite-value case was not reached. |
| GREEN | Not run | N/A | Blocked: WASM support and its test are absent from this configured build. |
| Rebuild `zene` | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Build completed. |
| CTest registration lookup | `rg -n "ControlWasmSandbox|WasmSandboxTest|WasmAbiConformanceTest" build/tests/CTestTestfile.cmake` | 1 | No matching registered tests. |
| Wasmtime cache lookup | `rg -n "Wasmtime_INCLUDE_DIR|Wasmtime_LIBRARY" build/CMakeCache.txt` | 0 | Both dependency paths are `NOTFOUND`. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |

**Affected-family count:** 0 registered CTest tests; 1 direct capability-probe script with 40 check results; 0 valid RED/GREEN regression runs.

**Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p10-72.md` only.
