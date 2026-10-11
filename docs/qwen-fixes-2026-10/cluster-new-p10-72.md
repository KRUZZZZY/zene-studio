# Cluster new-p10-72: BLOCKED

## Finding and change

The finding is real. `handleSetParam` previously narrowed the JSON `double` to `float` before checking its range. It now rejects nonfinite values and values outside `[-std::numeric_limits<float>::max(), +max]` with `InvalidArgs` before the cast or `setParam()` call ([ControlCommandsWasmEdit.cpp:199](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-72/src/core/ControlCommandsWasmEdit.cpp:199)).

The existing WASM control transcript now sends `1e300` after setting slot 0 to `0.25`, and checks for `invalid_args` plus an unchanged slot ([control-wasm-sandbox.py:276](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-72/tests/control-wasm-sandbox.py:276)). It is registered only in a WASM-enabled configuration.

## Why this remains blocked

This configured build has `Wasmtime_INCLUDE_DIR` and `Wasmtime_LIBRARY` set to `NOTFOUND` in `build/CMakeCache.txt`; CMake therefore disables WASM. The generated build has no reference to `ControlCommandsWasmEdit.cpp`, and `build/tests/CTestTestfile.cmake` has no `ControlWasmSandbox` registration. `cmake --build ... --target zene` completes, but does not compile the changed handler. Targeted CTest exits 0 while printing `No tests were found!!!`, which is zero tests and is not a passing run.

The no-fix and with-fix cycles below therefore do not demonstrate RED or GREEN. The product change and regression are committed as an unverified fix; verdict stays BLOCKED until these commands run in a WASM-enabled build.

## Evidence

All commands ran from the worktree root. Build and CTest output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/`. The `fff` grep tool was not available, so those logs were searched with `rg`.

| Check | Exact command | Exit | Observed result |
| --- | --- | ---: | --- |
| Remove product hunk | `git diff -- src/core/ControlCommandsWasmEdit.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/fix.patch` | 0 | Hunk removed for the no-fix attempt. |
| Rebuild without fix | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/build-red-2.log 2>&1; status=$?; echo EXIT=$status` | 0 | Target completed; handler is absent from this build graph. |
| RED attempt (not valid) | `cd build/tests && ctest -R ControlWasmSandbox --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/test-red-2.log 2>&1; status=$?; echo EXIT=$status` | 0 | `No tests were found!!!`; 0 tests executed, so no RED result. |
| Restore product hunk | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/fix.patch` | 0 | Fix restored. |
| Rebuild with fix | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/build-green-2.log 2>&1; status=$?; echo EXIT=$status` | 0 | Target completed; handler remains absent from this build graph. |
| GREEN attempt (not valid) | `cd build/tests && ctest -R ControlWasmSandbox --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/test-green-2.log 2>&1; status=$?; echo EXIT=$status` | 0 | `No tests were found!!!`; 0 tests executed, so no GREEN result. |
| Affected-family test count | `ControlWasmSandbox` in `build/tests/CTestTestfile.cmake` | N/A | 0 registered and 0 executed. |
| Complexity gate, final | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/gate-complexity-2.log 2>&1; status=$?; echo EXIT=$status` | 0 | Passed. |
| File-length gate, final | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/gate-file-length-2.log 2>&1; status=$?; echo EXIT=$status` | 0 | Passed. |
| Python syntax check | `python3 -c 'import ast, pathlib; ast.parse(pathlib.Path("tests/control-wasm-sandbox.py").read_text())' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-72.r2/logs/test-syntax.log 2>&1; status=$?; echo EXIT=$status` | 0 | Regression script parses. |

An earlier complexity-gate attempt returned 1 because `check_param` measured CCN 11. Moving the overflow predicate into a helper brought the final check under the limit; the final gate command above returned 0.

**Files committed:** `src/core/ControlCommandsWasmEdit.cpp`, `tests/control-wasm-sandbox.py`, `tests/upstream-modifications.txt`, and this report.
