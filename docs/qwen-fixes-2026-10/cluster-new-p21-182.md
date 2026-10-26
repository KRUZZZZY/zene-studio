FIXED

# cluster new-p21-182: AVX per-sample gain pointer

## Decision and evidence

REAL. In `plugins/SfzPlayer/sfizz/src/sfizz/simd/HelpersAVX.cpp:48-50`, the aligned loop loads eight gains on every iteration but previously advanced only `input` and `output`. The aligned gain pointer therefore stayed at the first vector. The fixed line 50 advances `gain`, `input`, and `output` together. The 25-sample regression failed at sample 8 before the fix (`expected 11.25, got 1.25`).

The matching SSE loop at `plugins/SfzPlayer/sfizz/src/sfizz/simd/HelpersSSE.cpp:114-117` already advances all three pointers. The scalar path at `plugins/SfzPlayer/sfizz/src/sfizz/simd/HelpersScalar.h:39-44` advances gain per sample; `gain1AVX` uses a scalar gain value and has no gain pointer to advance. A source search found no production call beyond the AVX declaration and definition. `SIMDHelpers.cpp:100-105` currently has no AVX dispatch cases, so the regression directly calls the declared helper to verify its elementwise contract.

## Regression matrix

| Path | Coverage and result |
| --- | --- |
| Aligned vector path and tail | 25 aligned samples: three AVX blocks and one scalar tail; every output is compared with its corresponding gain and input. Fails before the fix at sample 8. |
| Scalar-only path | Seven samples, shorter than one AVX block. |
| Alignment prologue | Gain, input, and output pointers share a one-float offset; seven scalar prologue samples align them before three vector blocks and a two-sample tail. |
| Failure, exhaustion, reconfiguration | Not applicable: `gainAVX` is stateless and has no resource, capacity, or configuration failure path. |
| Realtime constraints | The production change only advances an existing pointer inside the DSP loop. It adds no allocation, free, lock, wait, I/O, log, or signal. The test calls the helper off the audio thread with fixed stack arrays. |
| Platform support | CMake registers the test only when `sfzplayer_sfizz` builds its x86 GNU/Clang AVX kernel (`-mavx`). The test checks runtime AVX support and returns CTest skip code 77 otherwise. Other architectures retain their existing paths. |

No project format, saved data, undo, or control command changes are involved. No buffer limit or new check was added.

## Commands and results

All build, test, and gate logs are new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs`.

| Stage | Exact command | Exit / result |
| --- | --- | --- |
| Initial root target lookup | `cmake --build build --target zene SfzSimdGainAVXTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 2; `zene` built, but this Makefiles generator exposes the test target in `build/tests`. |
| Initial root target lookup | `cmake --build build --target zene tests/SfzSimdGainAVXTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 2; the subdirectory target name is not a root Make target. |
| Build original regression | `make -C build/tests SfzSimdGainAVXTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| RED on original code | `ctest -R SfzSimdGainAVXTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd `build/tests`) | 8; 1/1 failed at sample 8. |
| Build with fix | `(cmake --build build --target zene -j2 && make -C build/tests SfzSimdGainAVXTest -j2) > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| GREEN with fix | `ctest -R SfzSimdGainAVXTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd `build/tests`) | 0; 1/1 passed. |
| Build SFZ family | `(cmake --build build --target zene -j2 && make -C build/tests SfzPlayerTest SfzPlayerRealtimeTest -j2) > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| SFZ family | `ctest -R Sfz --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd `build/tests`) | 0; 4/4 passed: `SfzPlayerTest`, `SfzPlayerRealtimeTest`, `SfzSimdGainAVXTest`, and `SfzRenderThreadGate`. |
| Reverse product patch | `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/simd/HelpersAVX.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Build with product patch reversed | `(cmake --build build --target zene -j2 && make -C build/tests SfzSimdGainAVXTest -j2) > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| RED proof | `ctest -R SfzSimdGainAVXTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd `build/tests`) | 8; 1/1 failed at sample 8 with the same mismatch. |
| Reapply saved fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Rebuild after restoring fix | `(cmake --build build --target zene -j2 && make -C build/tests SfzSimdGainAVXTest -j2) > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| GREEN proof | `ctest -R SfzSimdGainAVXTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd `build/tests`) | 0; 1/1 passed. |
| Final source build | `(cmake --build build --target zene -j2 && make -C build/tests SfzSimdGainAVXTest -j2) > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/build-8.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Final focused test | `ctest -R SfzSimdGainAVXTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd `build/tests`) | 0; 1/1 passed. |
| First feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-182 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 1; the whole-tree manifest still listed vendored sfizz sources despite its published exclusion recipe. |
| Recheck after re-deriving `tests/all-sources.txt` | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-182 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0; all checks passed. |
| Final feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-182 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0; all checks passed, including complexity and file length with no new failing items. |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-182 new-p21-182 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-182.r1/logs/submit-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0; report, registration, commit, clean-tree, and internal feature-gate checks passed. |

The source inventory was regenerated from the command in `tests/all-sources.txt`; its reproduction and the gate passed without changing any gate or baseline.

## Self-check

- **RED/GREEN proof:** `SfzSimdGainAVXTest` exits 8 on the original code, exits 8 with the saved production patch reversed, and exits 0 after restoring it. The aligned multi-block/tail case fails at sample 8; the scalar-only and unaligned-prologue cases also pass in the final test run (exit 0). The exact runs are recorded above and in `test-1.log`, `test-2.log`, `test-4.log`, `test-5.log`, and `test-6.log`.
- **Deterministic test:** Inputs are fixed values derived from array indices. The test has no clock, sleep, race, or synchronization dependency.
- **Cause, callers, and siblings:** The stale pointer was in the AVX aligned loop. The helper declaration/definition and dispatcher were checked; the current AVX dispatcher has no cases. The SSE and scalar siblings already advance the per-sample gain pointer, while `gain1AVX` takes a scalar gain. The fix makes the AVX helper consistent with those contracts.
- **Realtime:** `gainAVX` is DSP code; the production change only advances a pointer and adds no allocation, free, lock, wait, I/O, log, or signal. The regression invokes it on the test thread.
- **No collateral behavior:** The helper has no persistent or serialized state and no capacity/configuration behavior. The change advances only its existing gain pointer; project loading, rendering formats, undo, and controls are untouched. Verdict is FIXED, so the conditional ALREADY-FIXED/FALSE-POSITIVE evidence item does not apply.

## Files in the commit

- `plugins/SfzPlayer/sfizz/src/sfizz/simd/HelpersAVX.cpp`
- `tests/src/plugins/SfzSimdGainAVXTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `tests/all-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p21-182.md`
