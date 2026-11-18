# Cluster new-p11-63: WASM output peak hides NaN samples

Verdict: **FIXED**

## Evidence

- `src/core/ControlCommandsWasm.cpp:256-267` detects non-finite output while retaining the existing peak calculation for finite samples.
- `src/core/ControlCommandsWasm.cpp:283-285` reports `output_has_non_finite: true` and a JSON `null` peak when any output sample is non-finite.
- `src/core/ControlCommandsWasm.cpp:395-396` declares those response properties in the strict result schema; `output_peak` accepts null.
- `tests/control-wasm-sandbox.py:65-85` builds a deterministic WASM module that writes two NaNs. `:332-347` checks the serialized samples, non-finite flag, and peak through `wasm.process`; `:503-504` runs the check in the socket transcript.

The old `std::max` path left the peak at zero for NaN samples. Qt already serialized those samples as JSON null, but the reported peak concealed that invalid output. The new response keeps finite peak behavior unchanged and explicitly marks an undefined peak.

## Regression and verification

The affected ctest family is `ControlWasmSandbox`: **1 test executed** in each recorded run.

| Evidence | Exact command (run from `build/tests` where noted) | Exit | Result |
|---|---|---:|---|
| RED on original production code, after baseline rebuild | `ctest -R ControlWasmSandbox --output-on-failure` | 8 | `test-3.log`: 1/1 failed at “wasm.process marks the output peak as non-finite”. |
| GREEN with fix | `ctest -R ControlWasmSandbox --output-on-failure` | 0 | `test-5.log`: 1/1 passed. |
| Reversal proof, product patch removed and rebuilt | `ctest -R ControlWasmSandbox --output-on-failure` | 8 | `test-6.log`: same regression check failed, 1/1. |
| Reversal proof, patch restored and rebuilt | `ctest -R ControlWasmSandbox --output-on-failure` | 0 | `test-7.log`: 1/1 passed. |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 0 | `gate-1-complexity.log`. |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 0 | `gate-2-file-length.log`. |

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-63.r2/logs`.

### Command record

- `cmake --build build --target zene` — **143** (`build-1.log`): the build shim stayed queued in polling sleeps; I terminated that wrapper before CMake started. The direct configured build succeeded afterward.
- `/usr/bin/cmake --build build --target zene -- -j4` — **0** for the baseline build (`build-2.log`), behavior change (`build-3.log`), schema-complete fix (`build-4.log`), reversed patch (`build-5.log`), and restored patch (`build-6.log`).
- `ctest -R ControlWasmSandbox --output-on-failure` — **8** (`test-1.log`): discarded, because the first WAT fixture spelling did not assemble. The fixture was corrected to reinterpret the canonical NaN bit pattern.
- `ctest -R ControlWasmSandbox --output-on-failure` — **8** (`test-2.log`): corrected fixture showed the missing report on the original binary; the official rebuilt baseline result is `test-3.log` above.
- `ctest -R ControlWasmSandbox --output-on-failure` — **8** (`test-4.log`): intermediate implementation was rejected by the strict result schema. The schema was updated before the green run.
- `git diff -- src/core/ControlCommandsWasm.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-63.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-63.r2/logs/fix.patch` — **0** (`reversal-1.log`).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-63.r2/logs/fix.patch` — **0** (`reversal-2.log`).

## Files committed

- `src/core/ControlCommandsWasm.cpp`
- `tests/control-wasm-sandbox.py`
- `docs/qwen-fixes-2026-10/cluster-new-p11-63.md`
