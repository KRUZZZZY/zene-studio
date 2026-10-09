# P0 realtime fixes

Base: `888934e1b` (`050/m0-train-2`). Queue units: Q050-005 and Q050-050.

## Acceptance results

| Queue | Acceptance criterion | Verdict and implementation | Regression test | Red exit | Green exit |
|---|---|---|---|---:|---:|
| Q050-005 | Sample-rate changes do not allocate or resize buffers in the audio callback. | FIXED. `sampleRateNeedsUpdate()` prepares the replacement ring buffer off-thread; `processImpl()` only applies a published buffer at the block boundary. Grain capacity is reserved during construction. | `GranularPitchShifterRealtimeTest::sampleRateHandoffDoesNotAllocateAndKeepsPitchShift` changes 44.1 kHz to 48 kHz while a render thread uses `AllocationProbe` and `RealtimeScope`; it asserts zero callback allocations and a callback timing bound. | 8: reversing only the product fix records one callback allocation. | 0: focused ctest passes. |
| Q050-005 | Prepared buffers swap safely and pitch-shift rendering remains correct. | FIXED. The callback swaps prepared vectors and state; the old buffer is placed in an atomic retired slot and reclaimed by the control thread. The test checks the shifted output against 466.16 Hz within 18 Hz. | Same focused render test. | 8: the red run stops at the allocation assertion before its pitch assertion. | 0: the changed-rate render and pitch assertion pass. |
| Q050-050 | `host_log` and `takeLog` produce race-free, consistent concurrent snapshots. | FIXED. `WasmLogBuffer` stores at most 4095 bytes and uses a single-attempt atomic flag. A contended write is dropped; a contended snapshot returns empty. | `WasmWorkerPoolTest::testConcurrentHostLogSnapshotsAreBoundedAndConsistent` runs alternating full-size A/B messages while a separate thread calls `takeLog`; every nonempty snapshot must contain one complete message. | 8 under TSan: it reports the `hostLogCallback` write racing the `takeLog` string copy. | 0 under TSan: focused ctest passes with no race report. |
| Q050-050 | The worker remains nonblocking and log retrieval stays bounded. | FIXED. The writer never waits for the reader; snapshots are limited to the fixed buffer capacity. | Same stress test checks 2500 concurrent blocks complete, the worker remains ready, at least one snapshot is observed, and all observed snapshots are bounded and intact. The normal focused ctest also passes. | 8 under TSan on the same log-buffer race. | 0 under normal ctest and TSan ctest. |

## Changed code and registration

- Q050-005: `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:68` reserves grain capacity, `:96` applies prepared buffers, `:276` prepares them, `:304` swaps them, and `:339` reclaims retired buffers. `plugins/GranularPitchShifter/GranularPitchShifterEffect.h:157` declares the prepared state and `:175` the atomic handoff slots. The test is registered as `GranularPitchShifterRealtimeTest` and depends on the granular plug-in module.
- Q050-050: `src/wasm/WasmLogBuffer.h:35` appends bounded messages and `:46` takes snapshots; `src/wasm/WasmSandbox.cpp:231` connects `host_log` and `:562` connects `takeLog` to it. `tests/src/wasm/WasmWorkerPoolTest.cpp:427` contains the stress test.
- New test source and helper registrations are present in `tests/fork-sources.txt`; `tests/all-sources.txt` reproduces its header recipe. The inherited test and sandbox edits have reasons in `tests/upstream-modifications.txt`.
- No agent commands, project-format changes, or reversibility classes were added.

## Builds, tests, and gates

| Queue | Build | Test results | Gate exits |
|---|---|---|---|
| Q050-005 | `zene GranularPitchShifterRealtimeTest`, exit 0 | Red ctest 8; green ctest 0 | complexity 0; file-length 0; fork-sources 0; all-sources reproduction 0; no-upstream regression 0; namespace 0; no-tautology 0 |
| Q050-050 | `WasmWorkerPoolTest` normal build 0; TSan build 0 | Red TSan ctest 8; green TSan ctest 0 with no reports; normal ctest 0 | complexity 0; file-length 0; fork-sources 0; all-sources reproduction 0; no-upstream regression 0; namespace 0; no-tautology 0 |

## Portability and limits

The product changes use C++20 atomics and standard containers, with no Qt 6-only APIs or platform-specific system calls. The granular regression test compiles on the supported targets but skips at runtime on Windows because that test host cannot load the plug-in module outside `zene.exe`. Cross-platform hosted builds were not run in this task.

On this Linux host, TSan requires `setarch -R` because of the host's address-space layout. The `WasmWorkerPoolTest` target disables Wasmtime's parallel Cranelift compilation only in the test binary, avoiding unrelated third-party TSan reports; the shipped product keeps Wasmtime's default setting.

## Fix commits

- `b3a4d9d7c fix(L10): prepare granular buffers off the audio thread (cluster Q050-005)`
- `f4ccfd2d7 fix(L10): snapshot WASM logs without races (cluster Q050-050)`
