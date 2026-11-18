# Cluster new-p10-77: NaN render samples in offline JSON

**Verdict: FIXED**

## Finding and change

The finding was real. `renderJson` could leave `peak` unchanged for NaN, serialize
the resulting NaN RMS as `null`, and report no invalid sample count while the
offline render remained successful. It now counts every non-finite output
sample and includes `non_finite_samples` in each pooled and inline run object.
The `wasm.render_offline` command description documents the field.

The regression assembles a WASM module that writes the quiet NaN bit pattern
`0x7fc00000`, invokes `wasm.render_offline`, serializes its result through
`QJsonDocument`, and expects one non-finite sample in a run.

## File and line evidence

- [`ControlCommandsWasmRender.cpp:103`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-77/src/core/ControlCommandsWasmRender.cpp:103) counts non-finite samples and inserts `non_finite_samples` at line 125; the command description documents it at line 314.
- [`ControlWasmRenderTest.cpp:45`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-77/tests/src/core/ControlWasmRenderTest.cpp:45) creates the deterministic NaN module; lines 97-119 drive the command and assert the serialized result.
- [`tests/CMakeLists.txt:1966`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-77/tests/CMakeLists.txt:1966) registers the test under `WANT_WASM`.

## Verification record

Wasmtime v48.0.1 was fetched and checksum-verified into the ignored
`third_party/wasmtime` directory. The updated CMake files triggered the normal
build regeneration, which found Wasmtime through `WASMTIME_ROOT`. No separate
configure command was run.

| Phase | Exact command | Exit and evidence |
|---|---|---|
| Dependency | `bash scripts/fetch-wasmtime.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r2/logs/wasmtime-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | `0`; pinned archive verified. |
| Initial combined build attempt | `WASMTIME_ROOT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-77/third_party/wasmtime cmake --build build --target zene ControlWasmRenderTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r2/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | `2`; CMake regenerated and this invocation's active make process did not know the newly added test target. Separate target builds below succeeded. |
| Baseline builds, production unchanged | `cmake --build build --target zene` (`build-2.log`); `cmake --build build --target ControlWasmRenderTest` (`build-3.log`) | `0`, `0`. |
| Baseline RED | From `build/tests`: `ctest -R ControlWasmRenderTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r2/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | `8`; 1 CTest test ran. The assertion saw `non_finite_samples` as `-1` (missing), expected `1`. |
| Fixed builds | `cmake --build build --target zene` (`build-4.log`); `cmake --build build --target ControlWasmRenderTest` (`build-5.log`) | `0`, `0`. |
| Fixed GREEN | Same ctest command from `build/tests`, with `test-2.log` as its output file | `0`; 1/1 CTest test passed. |
| Production-only reversal | `git diff -- src/core/ControlCommandsWasmRender.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r2/logs/fix.patch` | `0`; the patch contains only the production file. |
| Reversed builds and RED proof | `cmake --build build --target zene` (`build-6.log`); `cmake --build build --target ControlWasmRenderTest` (`build-7.log`); same ctest command from `build/tests`, output `test-3.log` | `0`, `0`, `8`; the same missing-field assertion failed again. |
| Restore and final builds | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r2/logs/fix.patch && git diff --quiet -- src/core/ControlCommandsWasmRender.cpp` returned `1` because the restored production diff is present (the patch application succeeded); then `cmake --build build --target zene` (`build-8.log`) and `cmake --build build --target ControlWasmRenderTest` (`build-9.log`) | Patch command `1` for the expected non-empty diff check; builds `0`, `0`. |
| Restored GREEN | Same ctest command from `build/tests`, output `test-4.log` | `0`; 1/1 CTest test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r2/logs/gate-complexity-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | `0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r2/logs/gate-file-length-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | `0`. |

The affected CTest family count was one test (`ControlWasmRenderTest`), executed
once per targeted ctest run; no run reported zero tests. The exposed tools did
not include `fff grep`, so the log folder was searched with `rg`: `error` and
`Assertion` had no matches, while `FAILED` appeared only in the two expected
RED ctest summaries.

## Files committed

- `src/core/ControlCommandsWasmRender.cpp`
- `tests/src/core/ControlWasmRenderTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-77.md`
