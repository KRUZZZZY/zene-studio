# Batch F-subsystems — 2026-10-08

Branch: `bugs/m0-blk-subsystems`, starting at `8eacc41d2`.

## Round 1 record (review disposition)

The verdicts below record the independent review of the Round 1 report. In particular, Round 1 did not fix the
5173/5186 generation race or the retained Lua child-view pointers.

| Cluster | Verdict | Fix commit | Test | Red exit | Green exit | Gate exits |
|---|---|---|---|---|---|---|
| 5173 | REWORK | `73ecb52ba` | `ModulationAudioViewTest::targetDestructionWaitsForAnInFlightAudioWrite` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 5186 | REWORK | `73ecb52ba` (shared lifetime fix) | `ModulationAudioViewTest::targetDestructionWaitsForAnInFlightAudioWrite` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 2658 | BLOCKED — Wasmtime C API unavailable in the Round 1 build | — | `WasmWorkerPoolTest` not configured | N/A | N/A | — |
| 3644 | BLOCKED — Wasmtime C API unavailable in the Round 1 build | — | `WasmWorkerPoolTest` not configured | N/A | N/A | — |
| 4628 | BLOCKED — Wasmtime C API unavailable in the Round 1 build | — | `WasmWorkerPoolTest` not configured | N/A | N/A | — |
| 5201 | BLOCKED — ONNX Runtime unavailable in the Round 1 build | — | `OnnxRuntimeStemSeparatorTest` not configured | N/A | N/A | — |
| 1663 | FIXED | `e228edf8b` | `ScriptDawBindingTest::aChannelViewStopsResolvingAfterItsTargetIsRemoved` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 5335 | REWORK | `e228edf8b` (channel wrapper only) | `ScriptDawBindingTest::aChannelViewStopsResolvingAfterItsTargetIsRemoved` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |

### Round 1 verification notes

- CMake reported the Wasmtime C API, ONNX Runtime include directory, and ONNX Runtime library unavailable, so the four
  Round 1 blocked rows could not be built or tested in that configuration.
- Round 1 used focused `ctest -R` cases; the full suite was not run. Its ccache temporary path denied writes; the
  report recorded moving `CCACHE_TEMPDIR` under `/tmp` for subsequent builds.

## Round 2

| Cluster | Verdict | Fix commit | Test | Red exit | Green exit | Gate exits |
|---|---|---|---|---|---|---|
| 5173 | FIXED | `97789a3fd` | `ModulationAudioViewTest::rebindDuringDestructionIgnoresAStaleTargetClosure` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 5186 | FIXED | `97789a3fd` (shared generation-aware gate) | `ModulationAudioViewTest::rebindDuringDestructionIgnoresAStaleTargetClosure` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 2658 | FIXED | `e481ddf9d` | `WasmWorkerPoolTest::testStopWaitsForAudioEndpointsToQuiesce` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 3644 | FIXED | `e481ddf9d` | `WasmWorkerPoolTest::testMemoryRefreshReacquiresMovedPointer`, `testWasmWorkerRefreshesMemoryAfterGrowth`, `testOfflineRenderRefreshesMemoryAfterGrowth` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 4628 | FIXED | `e481ddf9d` | `WasmWorkerPoolTest::testRemovalWaitsForLoadedWorkerReaders` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 5201 | FIXED | `2af0fc217` | `OnnxRuntimeStemSeparatorTest::testMalformedModelFailureIsReportedByStemJob` | build 0; ctest 8 (uncaught ORT exception abort) | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 1663 | ALREADY-FIXED | `e228edf8b` | `ScriptDawBindingTest::aChannelViewStopsResolvingAfterItsTargetIsRemoved` (Round 1 proof) | Round 1: build 0; ctest 8 | Round 1: build 0; ctest 0 | Round 1: all four gates 0 |
| 5335 | FIXED | `2bcb5ac80` | `ScriptDawBindingTest::aChannelViewStopsResolvingAfterItsTargetIsRemoved` | build 0; ctest 8 | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |

### Round 2 verification notes

- Reconfigured with Wasmtime 48.0.1 at `third_party/wasmtime` and ONNX Runtime 1.28.0 at
  `$HOME/.local/zene-deps/onnxruntime-linux-x64-1.28.0`; both optional features and their tests were enabled.
- Each Round 2 source fix has a red/green regression. The modulation destruction test now handshakes with teardown
  entry instead of sleeping. WASM coverage checks queue endpoint quiescence, moved-memory reacquisition and worker
  reader lifetime. The malformed ONNX model reproduces the job-thread abort without the catch and reports a failure
  after it.
- Builds used `-j4`; only focused `ctest -R` cases ran, not the full suite. The configured ccache temp path under
  `/run/user/1000/ccache-tmp` denied writes, so builds set `CCACHE_TEMPDIR=/tmp/luna-F-round2-ccache-temp` and
  completed successfully.

## Round 3

| Cluster | Verdict | Fix commit | Test | Red exit | Green exit | Gate exits |
|---|---|---|---|---|---|---|
| 2658 | FIXED | `c6ecc0d33` | `WasmWorkerPoolTest::testEntryRacesWithStopWithoutPostStopEndpointUse` (100,000 stop races) | build 0; ctest 8 (40 late pins / 200,000 entry attempts, 0.020000%) | build 0; ctest 0 (0 late pins / 200,000 entry attempts) | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 5201 | FIXED | `3db2618ef` | `OnnxRuntimeStemSeparatorTest::testPostRunOrtExceptionIsReturnedAsFailure` | build 0; ctest 8 (new test failed on exception escape; malformed-session test passed) | build 0; ctest 0 | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |

### Round 3 verification notes

- The WASM regression was run against the round-2 two-atomic implementation and observed 40 late endpoint entries in
  100,000 stop races. The one-word closed-bit/count gate measured zero late entries in the same 100,000 races; the
  audio path only performs an atomic pin attempt, while `stop()` waits off the audio thread.
- The ONNX seam throws an `Ort::Exception` from output validation after `Session::Run`. With round-2's setup and
  `Run()` catches retained, this test fails because the post-`Run()` exception escapes. The outer catches on
  `separate()` now return `Status::Failed` and copy the exception message. This scope also covers tensor creation,
  output inspection/data access, and session setup.
- Reconfigured with ONNX Runtime 1.28.0 and vendored Wasmtime 48.0.1. Built `zene`, `WasmWorkerPoolTest`, and
  `OnnxRuntimeStemSeparatorTest` with `-j4`; only focused ctest targets ran, not the full suite. All four gates passed
  before each code commit.
- Round 3 logs are in the `luna-F-round3` scratch folder under `/tmp/claude-1000`.
- Logs are in `/tmp/claude-1000/-home-kruzzzzy-Documents-AI-KOS-PROJECT-projects-zene-studio-program/719627a5-c809-46ec-b4a4-231af5d313ec/scratchpad/luna-F-round2`.
