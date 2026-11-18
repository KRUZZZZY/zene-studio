# Cluster new-p8-68

**Verdict: BLOCKED**

The finding is present in both converters: `src/core/ControlCommandsWasm.cpp:229-235` and `src/core/ControlCommandsWasmRender.cpp:183-187` call `QJsonValue::toDouble()` for each array item without checking its type. A string sample such as `"0.5"` therefore becomes zero.

The configured build cannot exercise or compile this feature. `CMakeLists.txt:1015-1021` disables `WANT_WASM` when the wasmtime C API is not found. In this build, `Wasmtime_INCLUDE_DIR` and `Wasmtime_LIBRARY` are both `NOTFOUND` in `build/CMakeCache.txt`; consequently the generated CTest registry has no WASM tests. Reconfiguring was explicitly disallowed. I did not change production code or retain an unexecuted regression that would fail in a WASM-enabled build.

## Verification evidence

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Build | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-68.r1/logs/build-1.log 2>&1` | 0 | Succeeded, with WASM disabled in this configured build. |
| RED attempt | From `build/tests`: `ctest -R ControlWasmSandbox --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-68.r1/logs/test-1.log 2>&1` | 0 | Invalid as regression evidence: CTest reported “No tests were found!!!”; zero tests ran. |
| GREEN | Not run | N/A | Blocked because the WASM test is not registered in this build. |
| Affected-family test count | From `build/tests`: `ctest -R ControlWasmSandbox --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-68.r1/logs/test-1.log 2>&1` | 0 | Zero WASM control tests were registered or executed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-68.r1/logs/gate-1.log 2>&1` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-68.r1/logs/gate-2.log 2>&1` | 0 | Passed. |

The requested `fff grep` tool was not available in this session's tool list or shell. I searched the log folder with `rg` for `error`, `FAILED`, and `Assertion`; none matched. The CTest log was also searched for `No tests` and confirmed the zero-test result.

**Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p8-68.md` only. No production fix is claimed.
