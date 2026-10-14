# Cluster 3648 — Invalid sample rates are passed through to WASM

Verdict: **BLOCKED** (the defect is REAL in the source, but the WASM subsystem is
compiled out of this build and its tests cannot be built or run on this box).

## The defect (source evidence)

- `src/wasm/WasmOfflineRender.cpp:251-266` — `renderOffline()` stores the caller's
  `sampleRate` in the outcome (`:259`) and validates only `modulePath`, `frames` and
  `blockFrames` (`:261-266`). Zero, negative and non-finite rates are never refused.
- `src/wasm/WasmOfflineRender.cpp:125` (`renderInline` → `sandbox.callProcess(..., sampleRate)`)
  and `:175` (`renderThroughPool` → `worker.submit(..., sampleRate)`) hand the unvalidated
  rate to both render paths.
- `src/wasm/WasmWorker.cpp:151` — the pooled path compares `sampleRate != m_loadedSampleRate`
  on **every** block; `m_loadedSampleRate` is a plain `float` (`src/wasm/WasmWorker.h:178`),
  so a NaN rate is never equal to itself and every block takes the
  `reinstantiate(sampleRate)` branch (`WasmWorker.cpp:156`) — repeated re-instantiation.
- The control surface reaches the same hole unguarded: `src/core/ControlCommandsWasmRender.cpp:214-216`
  reads `sample_rate` with no bounds check and its schema is an unbounded `numberProperty()`
  (`:316`); `src/core/ControlCommandsWasm.cpp:299-300` does the same for `wasm.process`
  (schema `numberProperty()` at `:370`), and `WasmSandboxHost::process`
  (`src/wasm/WasmSandboxHost.cpp:209-251`) forwards it to `callProcess` without validation.

## Why BLOCKED on this box

The WASM DSP sandbox requires the wasmtime C API. It is absent here, so the feature
degrades to OFF at configure time and none of the wasm targets exist in the configured
build tree:

- `build/CMakeCache.txt:767,770` — `Wasmtime_INCLUDE_DIR:PATH=Wasmtime_INCLUDE_DIR-NOTFOUND`,
  `Wasmtime_LIBRARY:FILEPATH=Wasmtime_LIBRARY-NOTFOUND` (`CMakeLists.txt:1015-1025` then
  `SET(WANT_WASM OFF)`; the cache's `WANT_WASM:BOOL=ON` at `:739` is only the option default).
- `src/wasm/WasmSandbox.cpp:27` — `#include <wasmtime.h>` unconditionally: the wasm sources
  cannot even compile without the API.
- `docs/KNOWN-LIMITATIONS.md:389` records this: "Two features are compiled out of these
  builds: the WASM DSP sandbox (needs the wasmtime C API)…" — and the wasm test targets
  are registered under the `WANT_WASM` guard (`tests/CMakeLists.txt:1935-1965`).
- `third_party/wasmtime` (the pinned v48.0.1 prebuilt C API, see `scripts/fetch-wasmtime.sh`)
  is not present, and reconfiguring the build tree is not allowed ("Do not configure").

The affected test family is `WasmWorkerPoolTest` (`tests/src/wasm/WasmWorkerPoolTest.cpp`,
the home of `renderOffline()` — it already calls it at `:208` and `:309`). The target does
not exist in this build tree, so no regression can be run: `ctest -R WasmWorkerPoolTest`
selects 0 tests, which the protocol treats as an error.

## Commands run (copied verbatim, with unpiped exit codes)

| Step | Command | EXIT |
| --- | --- | --- |
| build test target | `cmake --build build --target WasmWorkerPoolTest > LOGS/build-1.log 2>&1; echo EXIT=$?` | `EXIT=2` — log: `gmake: *** No rule to make target 'WasmWorkerPoolTest'.  Stop.` |
| ctest family | `cd build/tests && ctest -R WasmWorkerPoolTest --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` | `EXIT=0` — log: `No tests were found!!!` (0 tests run = error per protocol) |
| complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-1.log 2>&1; echo COMPLEXITY_EXIT=$?` | `COMPLEXITY_EXIT=0` |
| file-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-length-1.log 2>&1; echo LENGTH_EXIT=$?` | `LENGTH_EXIT=0` |

- RED command: **not achieved** — the test target does not exist on this box (see above).
- GREEN command: **not run** — no fix was applied, so there is nothing to verify.
- Affected-family test count: 0 tests executed (`WasmWorkerPoolTest`, `WasmSandboxTest`
  and `WasmAbiConformanceTest` are all absent from this build tree).
- Gate exit codes: complexity `0`, file-length `0` (tree unchanged).
- Files committed: `docs/qwen-fixes-2026-10/cluster-3648.md` only. No production code was
  changed — committing an unverifiable fix would violate the RED→GREEN requirement.

## The fix that a wasmtime-enabled box should apply (NOT applied here)

In `src/wasm/WasmOfflineRender.cpp`, keep the `renderOffline` ratchet green (its baseline
CCN is 22 in `tests/complexity-baseline.tsv:21`) by extracting the guard into a small
helper in the file's anonymous namespace and calling it once:

```cpp
//! The render's argument guard, so renderOffline() refuses bad parameters before any
//! module runs. A sample rate must be finite and positive: a NaN rate never equals the
//! worker's loaded rate (WasmWorker.cpp:151), so the pooled path would re-instantiate
//! on every block.
std::string renderArgsError(const std::string& modulePath, std::uint32_t frames,
    std::uint32_t blockFrames, float sampleRate)
{
    if (modulePath.empty() || frames == 0 || blockFrames == 0
        || blockFrames > WasmWorker::maxBlockFrames)
    {
        return "frames and block_frames must be non-zero and block_frames must fit "
            "WasmWorker::maxBlockFrames";
    }
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0f)
    {
        return "sample_rate must be a finite, positive number";
    }
    return {};
}
```

and in `renderOffline()` replace the existing `if (modulePath.empty() || …)` block with
`const std::string argsError = renderArgsError(modulePath, frames, blockFrames, sampleRate);`
plus `if (!argsError.empty()) { outcome.error = argsError; return outcome; }`. The same
`!std::isfinite(sampleRate) || sampleRate <= 0.0f` refusal belongs at the process entry
point, `WasmSandboxHost::process` (`src/wasm/WasmSandboxHost.cpp:209`), whose only caller
is the `wasm.process` handler (`src/core/ControlCommandsWasm.cpp:305`).

The regression belongs in `tests/src/wasm/WasmWorkerPoolTest.cpp` (add a
`testInvalidSampleRatesAreRefused()` slot calling `renderOffline` with `0.0f`,
`-48000.0f`, `quiet_NaN()` and `infinity()` and requiring `!ok`, a non-empty error and
empty `runs`/`control`), built via `cmake --build build --target WasmWorkerPoolTest` and
run from `build/tests` with `ctest -R WasmWorkerPoolTest`.
