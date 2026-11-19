FIXED

# Cluster 3647: bound offline render frames before any load or allocation

Route A, with test-only stubs.

## Defect (before the fix)

- `renderOffline` checked only `frames == 0` and had no upper bound on `frames`.
  Evidence: `git show 7d1d32bd99^:src/wasm/WasmOfflineRender.cpp` line 263 (the parent of the fix commit):
  `if (modulePath.empty() || frames == 0 || blockFrames == 0 || blockFrames > WasmWorker::maxBlockFrames)`.
- The block loops used `std::uint32_t offset`, so `offset += blockFrames` was 32-bit arithmetic.
  Evidence at `7d1d32bd99^`: `WasmOfflineRender.cpp:55` (`fillStimulus` offset parameter; the function starts at
  line 54), `:94` (`renderInline` loop; the function starts at 70), `:173` (`renderThroughPool` loop; the function
  starts at 152).

## Fix

- Cap: `src/wasm/WasmOfflineRenderLimits.h:24` `maxOfflineRenderFrames = 4194304` (2^22). The refusal helper
  `offlineRenderFramesError` is at `:27`.
- The check runs in `renderOffline` before the module-path check, so it comes before any load or allocation:
  `src/wasm/WasmOfflineRender.cpp:266-271`. The module-path check follows at `:273`.
- Loop offsets widened to `std::size_t`: the `fillStimulus` parameter at `WasmOfflineRender.cpp:56`, and the loops
  at `:95` and `:175`. The per-block count is narrowed back to `std::uint32_t` with an explicit `static_cast`
  (`:97-98`) after `std::min<std::size_t>`.
- Schema cap, which is the client-facing contract: `src/core/ControlCommandsWasmRender.cpp:336`
  `integerProperty(1, 4194304)` for `frames`. The call site at `src/core/ControlRegistry.cpp:255-259` runs
  `validateArgs` on the command's schema and returns `InvalidArgs` when it fails.
- The bound lives in `src/wasm` because direct callers of `renderOffline` bypass the control schema.

## Tests

- `tests/src/wasm/WasmOfflineRenderLimitTest.cpp` (new, 124 lines): the limit tests.
- `tests/src/wasm/WasmOfflineRenderLimitStubs.cpp` (new, 86 lines): test-only stubs. The sandbox is a stub,
  not real wasmtime.
- Red/green evidence from the logs (see the table below): 018 and 023 show 3 of 8 tests failing; 020, 025 and 026
  show all tests passing. The log names are "red", "green", "reversed" and "restored".

## Mutation checks

Each mutation was applied as a patch in the log folder (`m1.patch`, `m2.patch`, `m2b.patch`, `m3.patch`), built,
and run with `ctest -R WasmOfflineRenderLimitTest`. Each `ctest` exited 8 (from the run record; the logs show
`Errors while running CTest`).

| Mutation | Change | Failing slots | Log |
|---|---|---|---|
| m1 | `frames > maxOfflineRenderFrames` becomes `>=` | `acceptsExactlyTheLimit`, `validatorBoundsTheFrameCount` | `m1-test.log` |
| m2 | limit doubled (`maxOfflineRenderFrames * 2u`) | `rejectsOneFrameOverTheLimit`, `validatorBoundsTheFrameCount` | `m2-test.log` |
| m2b | comparison done after casting to `int32` | `rejectsTheLargestFrameCount`, `validatorBoundsTheFrameCount` | `m2b-test.log` |
| m3 | validator block moved (not removed) from before the control loop to directly after the first `renderInline` call, inside the loop | `rejectsOneFrameOverTheLimit`, `rejectsTheLargestFrameCount`, both fail the `loadModuleFileCalls` QCOMPARE at `WasmOfflineRenderLimitTest.cpp:76` (expected 0, actual 1): the first render loads the module before the bound is checked | `m3-test.log` |

m2 does not fail `rejectsTheLargestFrameCount`, because a doubled limit still refuses `UINT32_MAX`.

## Limits of verification

- `WasmWorkerPoolTest` and `ControlWasmRenderTest` were NOT built. This tree is `WANT_WASM=OFF`
  (`build/CMakeCache.txt`: `WANT_WASM:BOOL=OFF`), so those suites were not run.
- The `size_t` loop change is unreachable under the 2^22 cap (`offset < frames <= 2^22`, so 32-bit wrap cannot
  happen) and it is untested. It is defensive only.
- The stub sandbox is not real wasmtime. The tests exercise the bound and the loops through stubs, not a real
  module load.
- No threading changed, so TSan was not run.
- Builds: `zene` was relinked only at 019 (`Linking CXX executable ../zene`); at 024 it was already up to date.
  Apart from that, only the test target was built. Only `WasmOfflineRenderLimitTest` was run (`ctest` reports
  `out of 1`), so the wider suite was not run.
- In this tree the real `src/wasm/WasmOfflineRender.cpp` is built only by the new `WasmOfflineRenderLimitTest`
  target. The `WANT_WASM`-guarded targets that also build it (`WasmWorkerPoolTest`, and the library through
  `src/wasm/CMakeLists.txt`) do not exist here, because `WANT_WASM=OFF`.
- The `acceptsExactlyTheLimit` slot shows that the cap does not refuse `2^22` frames. It does not show that the
  output is correct.
- `renderOffline`'s cyclomatic complexity (CCN) is already above 10. A manual count gives about 22 before the fix
  and about 23 after; no CCN tool is installed here. The complexity gate reported no new item (`026-gates.log`),
  so the gate does not flag this.

## Commands and exit codes

Exit codes below were recorded from the command echo of the run (run record `3647.r2`). No log file in `logs/`
contains an exit line, so the exit codes are not verified from the logs. The "What the log shows" column is what
each log does contain.

| Log | Step | EXIT | What the log shows |
|---|---|---|---|
| 016-regen.log | CMake configure | not recorded | ends with "Build files have been written" |
| 017-build-red.log | build `WasmOfflineRenderLimitTest` (red) | 0 | target built, no error lines |
| 018-test-red.log | `ctest` (red) | 8 | `Totals: 5 passed, 3 failed`; `Errors while running CTest` |
| 019-build-green.log | build (green) | 0 | `Linking CXX executable ../zene`; test target built |
| 020-test-green.log | `ctest` (green) | 0 | `100% tests passed out of 1` |
| 021-syntax-after.log | `g++ -std=c++20 -fsyntax-only` on `WasmOfflineRender.cpp` | 0 | empty log (0 bytes). Purpose: the real file compiles without wasmtime |
| 022-build-reversed.log | `git diff -- WasmOfflineRenderLimits.h WasmOfflineRender.cpp > fix.patch`, then `git apply -R fix.patch`, then build | 0, 0, 0 | `Building CXX ... WasmOfflineRender.cpp.o`; target built |
| 023-test-reversed.log | `ctest` (reversed) | 8 | `Totals: 5 passed, 3 failed`; `rejectsOneFrameOverTheLimit` and `rejectsTheLargestFrameCount` fail on the `loadModuleFileCalls` QCOMPARE (Actual 1, Expected 0, `WasmOfflineRenderLimitTest.cpp:76`); `validatorBoundsTheFrameCount` fails |
| (none) | `git apply fix.patch` | 0 | no log |
| 024-build-restored.log | build (restored) | 0 | test target rebuilt; `zene` up to date |
| 025-test-restored.log | `ctest` (restored) | 0 | `100% tests passed out of 1` |
| 026-test-final-green.log | `ctest -R WasmOfflineRenderLimitTest` (final green) | 0 | `100% tests passed out of 1` |
| 026-gates.log | `feature_check.py` (before the commit) | not recorded | `RESULT: PASS`; the eight checks PASS, including complexity and file-length with "no new item in your files" |
| 027-gates-postcommit.log | `feature_check.py` (after the commit) | not recorded | `RESULT: PASS` |
| m1, m2, m2b, m3 | build, then `ctest -R WasmOfflineRenderLimitTest` | build not recorded; `ctest` 8 | `m*-build.log` shows the target built; `m*-test.log` shows `Errors while running CTest` |

## Files changed

- `src/wasm/WasmOfflineRender.cpp` (modified)
- `src/wasm/WasmOfflineRenderLimits.h` (new)
- `tests/src/wasm/WasmOfflineRenderLimitTest.cpp` (new)
- `tests/src/wasm/WasmOfflineRenderLimitStubs.cpp` (new)
- `tests/CMakeLists.txt` (+25)
- `tests/all-sources.txt` (+3)
- `tests/fork-sources.txt` (+3)
- `tests/upstream-modifications.txt` (+1; the inherited CMakeLists change, which registers the test outside the `WANT_WASM` guard)
- `docs/qwen-fixes-2026-10/cluster-3647.md` (new; this report)

No file under `src/core` changed, and no command was added, so the ControlCommandsSnapshot canary is untouched.

## Other findings

- The lane tree lacks `WANT_WASM` (`build/CMakeCache.txt` has it OFF), so the wasm suites that need it cannot run
  in this worktree.
- Direct `renderOffline` callers bypass the control schema. This is why the bound is enforced in `src/wasm`
  and not only in the schema.
