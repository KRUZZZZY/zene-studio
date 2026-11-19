FIXED

# Cluster 3647: bound offline render frames before any load or allocation

Route A, with test-only stubs.

## Defect (before the fix)

- `renderOffline` checked only `frames == 0` and had no upper bound on `frames`.
  Evidence: `git show HEAD:src/wasm/WasmOfflineRender.cpp` line 263:
  `if (modulePath.empty() || frames == 0 || blockFrames == 0 || blockFrames > WasmWorker::maxBlockFrames)`.
- The block loops used `std::uint32_t offset`, so `offset += blockFrames` was 32-bit arithmetic.
  Evidence at HEAD: `WasmOfflineRender.cpp:55` (`fillStimulus` offset parameter), `:94` (`renderInline` loop),
  `:173` (`renderThroughPool` loop).

## Fix

- Cap: `src/wasm/WasmOfflineRenderLimits.h:24` `maxOfflineRenderFrames = 4194304` (2^22). The refusal helper
  `offlineRenderFramesError` is at `:27`.
- The check runs in `renderOffline` before the module-path check, so it comes before any load or allocation:
  `src/wasm/WasmOfflineRender.cpp:266-272`.
- Loop offsets widened to `std::size_t` at `WasmOfflineRender.cpp:56`, `:95`, `:175`. The per-block count is
  narrowed back to `std::uint32_t` with an explicit `static_cast` after `std::min<std::size_t>`.
- Schema cap, which is the client-facing contract: `src/core/ControlCommandsWasmRender.cpp:336`
  `integerProperty(1, 4194304)` for `frames`. It is enforced by `validateArgs` in
  `src/core/ControlRegistry.cpp:255-259`, which returns `InvalidArgs` on failure.
- The bound lives in `src/wasm` because direct callers of `renderOffline` bypass the control schema.

## Tests

- `tests/src/wasm/WasmOfflineRenderLimitTest.cpp` (new, 124 lines): the limit tests.
- `tests/src/wasm/WasmOfflineRenderLimitStubs.cpp` (new, 86 lines): test-only stubs. The sandbox is a stub,
  not real wasmtime.
- Red/green evidence from the logs (see the table below): 018 and 023 show 3 of 8 tests failing; 020, 025 and 026
  show all tests passing. The log names are "red", "green", "reversed" and "restored".

## Limits of verification

- `WasmWorkerPoolTest` and `ControlWasmRenderTest` were NOT built. This tree is `WANT_WASM=OFF`
  (`build/CMakeCache.txt`: `WANT_WASM:BOOL=OFF`), so those suites were not run.
- The `size_t` loop change is unreachable under the 2^22 cap (`offset < frames <= 2^22`, so 32-bit wrap cannot
  happen) and it is untested. It is defensive only.
- The stub sandbox is not real wasmtime. The tests exercise the bound and the loops through stubs, not a real
  module load.
- No threading changed, so TSan was not run.

## Commands and exit codes

Logs 016-025 record tool output only. They do not record the command lines or EXIT codes. The outcome per log is
what the log shows; no exit code is inferred from it.

| Log | Shown in log |
|---|---|
| 016-regen.log | CMake configure ends with "Build files have been written" |
| 017-build-red.log | target `WasmOfflineRenderLimitTest` built |
| 018-test-red.log | `Totals: 5 passed, 3 failed`; `0% tests passed, 1 tests failed out of 1` |
| 019-build-green.log | target built |
| 020-test-green.log | `100% tests passed out of 1` |
| 021-syntax-after.log | empty (0 lines); purpose not recorded |
| 022-build-reversed.log | target built |
| 023-test-reversed.log | `Totals: 5 passed, 3 failed`; `0% tests passed, 1 tests failed out of 1` |
| 024-build-restored.log | target built |
| 025-test-restored.log | `100% tests passed out of 1` |
| 026-test-final-green.log | `100% tests passed out of 1` |

Commands run in this session (with exit codes):

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/026-gates.log 2>&1; echo EXIT=$?`
  gives `EXIT=0`, `RESULT: PASS`. All eight checks PASS: fork-sources, all-sources-reproduce,
  no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"),
  file-length ("no new item in your files"). This run is before the commit.
- `quiet-run p4-test-final -- sh -c 'cmake --build build --target WasmOfflineRenderLimitTest && cd build/tests && ctest -R WasmOfflineRenderLimitTest --output-on-failure'`
  gives `exit 0`, `100% tests passed out of 1`.
- The post-commit gate re-run is logged at `.../logs/027-gates-postcommit.log`.

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
- Earlier, the round-2 scaffold was lost from the worktree. This is taken from the packet and was not re-verified
  in this session.
- Direct `renderOffline` callers bypass the control schema. This is why the bound is enforced in `src/wasm`
  and not only in the schema.
