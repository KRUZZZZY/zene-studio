# Batch F-subsystems — 2026-10-08

Branch: `bugs/m0-blk-subsystems`, starting at `8eacc41d2`.

| Cluster | Verdict | Fix commit | Test | Red exit | Green exit | Gate exits |
|---|---|---|---|---|---|---|
| 5173 | FIXED | `73ecb52ba` | `ModulationAudioViewTest::targetDestructionWaitsForAnInFlightAudioWrite` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 5186 | FIXED | `73ecb52ba` (shared lifetime fix) | `ModulationAudioViewTest::targetDestructionWaitsForAnInFlightAudioWrite` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 2658 | BLOCKED — source confirms `stop()` drains both SPSC queues on the control thread; the reconfigured build reports the Wasmtime C API missing and disables the WASM feature, so its test target is unavailable | — | `WasmWorkerPoolTest` not configured | N/A — target unavailable | N/A — target unavailable | — |
| 3644 | BLOCKED — source confirms cached linear-memory pointers are read after `callProcess()` in the worker and offline renderer; the Wasmtime C API is missing, so the WASM test target is unavailable | — | `WasmWorkerPoolTest` not configured | N/A — target unavailable | N/A — target unavailable | — |
| 4628 | BLOCKED — source confirms the pool loads a raw worker pointer before `claimForLane()` without covering that gap; the Wasmtime C API is missing, so the WASM test target is unavailable | — | `WasmWorkerPoolTest` not configured | N/A — target unavailable | N/A — target unavailable | — |
| 5201 | BLOCKED — source confirms `Ort::Session` construction and `session.Run()` can throw outside a catch; `ONNXRuntime_INCLUDE_DIR` and `ONNXRuntime_LIBRARY` are `NOTFOUND`, so the ONNX test target is unavailable | — | `OnnxRuntimeStemSeparatorTest` not configured | N/A — target unavailable | N/A — target unavailable | — |
| 1663 | FIXED | `e228edf8b` | `ScriptDawBindingTest::aChannelViewStopsResolvingAfterItsTargetIsRemoved` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 5335 | FIXED | `e228edf8b` (shared stable-id fix) | `ScriptDawBindingTest::aChannelViewStopsResolvingAfterItsTargetIsRemoved` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |

## Verification notes

- `cmake -S . -B build` completed. CMake reported `WASM DSP sandbox: Disabled (wasmtime C API not found; set WASMTIME_ROOT)`. The ONNX Runtime include directory and library were both not found. The four blocked rows have no product change or red/green claim; their regressions could not be built or run in this configured environment.
- Builds used `-j6`, with the prescribed dependency paths and `LMMS_PLUGIN_DIR` unset. The configured ccache temporary directory under `/run/user/1000/ccache-tmp` denied writes, so subsequent builds set `CCACHE_TEMPDIR=/tmp`. Early build iterations also exposed two Qt connection overload errors, which were corrected; the completed red/green proof builds exited 0 as shown above.
- Only the focused `ctest -R` cases above were run. The full suite was not run.
