# Cluster new-p12-291: modules with start functions run out of fuel during loading

Verdict: **FIXED**

## Finding

`src/wasm/WasmSandbox.cpp` enables fuel consumption in the engine
(`wasmtime_config_consume_fuel_set(config, true)`, `WasmSandbox.cpp:104`) but
only primes the store's fuel inside `callWithFuel()`
(`WasmSandbox.cpp:271-...`), which runs for `process()`/probe calls *after*
instantiation. `loadModuleBytes()` instantiated the module with no fuel budget
on the store, so a valid module whose `(start ...)` function executes any wasm
code trapped immediately with "all fuel consumed by WebAssembly" and the module
was rejected while loading.

This is REAL. Reproduced directly against the production source and the pinned
wasmtime C API (v48.0.1, `third_party/wasmtime`): the original code returned

```
load=0 hasProcess=0 error=instantiation trapped: wasm trap: all fuel consumed by WebAssembly
```

for a module exporting `memory`, `channels`, `latency`, a `start` function, and
`process`.

## Fix

`src/wasm/WasmSandbox.cpp`:

- New `WasmSandbox::Impl::primeFuelBudget()` (`WasmSandbox.cpp:271-280`) calls
  `wasmtime_context_set_fuel(context, fuelBudget)` and discards a non-null error
  (fuel is enabled in the engine config, so it cannot fail; the error is deleted
  rather than leaked).
- `loadModuleBytes()` calls `m_impl->primeFuelBudget()` immediately before
  `wasmtime_linker_instantiate()` (`WasmSandbox.cpp:372-377`), so the module's
  start function runs with the configured budget.

Behaviour is otherwise unchanged: `callWithFuel()` still re-primes the budget
before every call, so per-call metering is identical.

The helper was used rather than an inline `if` because an inline branch raised
`loadModuleBytes`'s CCN from the grandfathered 15 to 16 (a complexity-ratchet
regression); the helper keeps that function at CCN 15 and is itself CCN 2.

## Regression test

`tests/src/wasm/WasmSandboxTest.cpp`: new slot
`loaderRunsStartFunctionWithinFuelBudget()` assembles an inline WAT module with
a nonempty `(start $init)` loop plus the DSP ABI exports, loads it, and asserts
the load succeeds and `process()` runs. On the original code the load fails
(`loadModuleBytes with start function: ... all fuel consumed ...`).

The ctest target `WasmSandboxTest` is registered only under `if(WANT_WASM)`
(`tests/CMakeLists.txt:2944`). **This lane's build was configured with
`-DWANT_WASM:BOOL=OFF`** (recorded in the run's `build.log` and in
`build/CMakeCache.txt`), the lane shim refuses `cmake` configuration
(`bunny-guard: configuring is the parent's job`), and no wasm test target exists
in `build/tests`. `ctest -R '^WasmSandboxTest$'` therefore reports
`No tests were found!!!` (0 executed). To obtain real RED/GREEN evidence the
same production translation unit was compiled directly with the pinned wasmtime
C API and driven by a deterministic harness (see Evidence).

## Evidence

Logs under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-291.r1/logs`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| RED (original `WasmSandbox.cpp`) | `g++ -std=c++20 -O1 wasm_start_harness.cpp src/wasm/WasmSandbox.cpp -I src/wasm -I build/src -I build -I third_party/wasmtime/include -L third_party/wasmtime/lib -lwasmtime -Wl,-rpath,$PWD/third_party/wasmtime/lib -o /tmp/opencode/wasm_start_harness && /tmp/opencode/wasm_start_harness` | 1 | `harness-red.log`: `instantiation trapped: wasm trap: all fuel consumed by WebAssembly` |
| GREEN (fix applied) | same build + `/tmp/opencode/wasm_start_harness` | 0 | `harness-final-green.log`: `load=1 hasProcess=1`, `process ok=1` |
| Reversal: `git diff -- src/wasm/WasmSandbox.cpp > logs/fix.patch` | `git apply -R logs/fix.patch` | 0 | patch removed, rebuilt |
| Reversal RED | rebuild + run | 1 | `harness-final-red.log`: same out-of-fuel trap |
| Restore | `git apply logs/fix.patch` | 0 | patch restored, rebuilt, run | 
| Restored GREEN | rebuild + run | 0 | `harness-final-green.log` result reproduced |
| ctest family | `ctest -R '^WasmSandboxTest$' --output-on-failure` (from `build/tests`) | 0 | `test-1-ctest.log`: `No tests were found!!!` — 0 tests (target absent: `WANT_WASM=OFF`) |
| Complexity ratchet (required, fork scope) | `bash tests/complexity-gate.sh --check` | 1 | `gate-2-complexity.log`: no regression from this change (`loadModuleBytes` stays CCN 15); 6 pre-existing regressions unrelated to it |
| File-length ratchet (required, fork scope) | `bash tests/file-length-gate.sh --check` | 1 | `gate-4-file-length.log`: no regression from this change (`WasmSandbox.cpp` 511 ≤ baseline 512); 14 pre-existing regressions unrelated to it |

**Affected-family test count:** the `WasmSandboxTest` family has 0 registered /
0 executed tests in this lane (target compiled out). The deterministic
regression is pinned in `tests/src/wasm/WasmSandboxTest.cpp` and was exercised
RED/GREEN through the direct harness above.

### Gate caveat (pre-existing, not caused by this change)

Both required ratchets are already red at the branch HEAD with my change
removed. Confirmed by reversing the product patch and re-running:
`gate-1b-complexity-head.log` reports the identical 6 "new function over target"
entries. The file-length fork baseline holds only 5 entries while 14 fork
sources exceed 500 code lines. Re-anchoring a gate is forbidden by the task, so
these are reported, not changed. This change adds no new complexity regression
(`loadModuleBytes` unchanged at 15) and does not grow its file past the baseline
(511 ≤ 512).

## Files committed

- `src/wasm/WasmSandbox.cpp` (already registered in `tests/fork-sources.txt:2058`)
- `tests/src/wasm/WasmSandboxTest.cpp` (already registered in `tests/all-sources.txt:2963`)
- `docs/qwen-fixes-2026-10/cluster-new-p12-291.md` (this report)

No new files and no inherited-file edits, so no registration-file change was
required.
