FIXED

# Cluster 3647-port: bound offline render frames before any load or allocation

## Verdict and defect

**REAL.** The finding is in the WASM offline-render path, not in the export/render
queue. At the pre-fix commit `e4194e0bf5`, `renderOffline` checked only
`frames == 0` and had no upper bound:

- `e4194e0bf5:src/wasm/WasmOfflineRender.cpp:263` was
  `if (modulePath.empty() || frames == 0 || blockFrames == 0 || blockFrames > WasmWorker::maxBlockFrames)`.
- `renderInline` then did `render.output.assign(frames, 0.0f)` at
  `e4194e0bf5:src/wasm/WasmOfflineRender.cpp:90`, so `frames = UINT32_MAX`
  asks for about 16 GiB after a module with a `process()` export loads.
- Both block loops used a 32-bit offset
  (`e4194e0bf5:src/wasm/WasmOfflineRender.cpp:94` and `:173`). With
  `frames` near `UINT32_MAX`, `offset += blockFrames` wraps, so the loop can
  never terminate.

The control schema already capped this argument at `4194304`
(`src/core/ControlCommandsWasmRender.cpp:336`), enforced by
`ControlRegistry::dispatch` through `validateArgs`
(`src/core/ControlRegistry.cpp:255-259`). That protects the control command
but not a direct caller of the exported `renderOffline` API.

## Fix

- New `src/wasm/WasmOfflineRenderLimits.h:24` defines
  `maxOfflineRenderFrames = 4194304` (2^22), and
  `offlineRenderFramesError()` at `:27-35` returns the refusal text.
- `src/wasm/WasmOfflineRender.cpp:266-271` checks that bound before the existing
  argument check, before either render path, and before any output allocation
  or module load.
- The block offsets were widened to `std::size_t`: the `fillStimulus`
  parameter at `:55-56` and the loops at `:95` and `:175`. The per-block count
  is narrowed back to `std::uint32_t` with an explicit `static_cast`
  (`:97-98`, `:177-178`), so the frame count passed into the module remains
  the documented 32-bit ABI type.
- The control-facing schema is unchanged. It already publishes the same
  4194304 limit, so the new guard rejects no control call that was accepted
  before.

## Regression test and matrix

`tests/src/wasm/WasmOfflineRenderLimitTest.cpp` compiles the real
`src/wasm/WasmOfflineRender.cpp` against test-only `WasmSandbox`/`WasmWorker`
stubs (`tests/src/wasm/WasmOfflineRenderLimitStubs.cpp`). It is registered
outside the `WANT_WASM` guard at `tests/CMakeLists.txt:5607-5630`, so it runs
on this box (`WANT_WASM:BOOL=OFF` in `build/CMakeCache.txt`).

| Path | Test slot |
|---|---|
| Normal path | `normalRenderStillReachesTheSandbox` sends 64 frames and sees the sandbox load |
| Boundary accept | `acceptsExactlyTheLimit` sends exactly 4194304 frames and is not refused by the cap |
| Failure | `rejectsOneFrameOverTheLimit` sends 4194305 frames; the sandbox must not be loaded |
| Exhaustion | `rejectsTheLargestFrameCount` sends `UINT32_MAX`; refused before the sandbox |
| Existing validation | `keepsTheExistingRejections` covers zero frames and an oversized block |
| Reconfiguration | Not applicable: `renderOffline` has no persisted or reconfigurable state; the cap is a pure argument check |
| Audio thread | Not applicable: `renderOffline` is a control-thread API; no process/render-period code changed |
| Platform build | The target uses Qt Test and test-only stubs without wasmtime. It sets `LMMS_STATIC_DEFINE` because it compiles functions declared with `LMMS_EXPORT` directly into the test executable. |

The seam is the stub's `loadModuleFileCalls` counter. The refusal cases assert
it stays 0, which distinguishes a real refusal from a failure later in a render.
The test uses integer frame counts, a stub switch, and no sleeps, clocks,
threads or random values.

## Commands and exit codes

Round 4 is a review-only pass pinned to HEAD `6ee78fcac2` (no product or test
code changed). Every result below was re-run in this round; all logs are under
`/home/kruzzzzy/.config/kads/zene/state/runs/3647-port.r4/logs`. Commands run
from the worktree root, ctest from `build/tests`.

| Step | Command | EXIT | Evidence |
|---|---|---|---|
| Pinned HEAD | `git rev-parse HEAD` | 0 | `head-001.log`: `6ee78fcac26b843d05673361c4a2247cb6b7b1d2` |
| GREEN build | `cmake --build build --target WasmOfflineRenderLimitTest -j4` | 0 | `build-green-001.log` |
| GREEN run | `ctest -R WasmOfflineRenderLimitTest --output-on-failure` | 0 | `test-green-001.log`: `100% tests passed out of 1` |
| 4b patch | `git diff e4194e0bf5 6ee78fcac2 -- src/wasm/WasmOfflineRender.cpp src/wasm/WasmOfflineRenderLimits.h > logs/fix.patch` | 0 | `fix.patch` (111 lines) |
| 4b reverse | `git apply -R logs/fix.patch` | 0 | `apply-reverse-001.log` |
| RED build | `cmake --build build --target WasmOfflineRenderLimitTest -j4` | 0 | `build-red-001.log` |
| RED run | `ctest -R WasmOfflineRenderLimitTest --output-on-failure` | 8 | `test-red-001.log`: `Totals: 5 passed, 2 failed`; `rejectsOneFrameOverTheLimit` and `rejectsTheLargestFrameCount` fail with `loadModuleFileCalls` Actual 1, Expected 0 at `WasmOfflineRenderLimitTest.cpp(74)` |
| 4b restore | `git apply logs/fix.patch` | 0 | `apply-forward-001.log` |
| RESTORED build | `cmake --build build --target WasmOfflineRenderLimitTest -j4` | 0 | `build-restored-001.log` |
| RESTORED run | `ctest -R WasmOfflineRenderLimitTest --output-on-failure` | 0 | `test-restored-001.log`: `100% tests passed out of 1` |
| Family | `ctest -R Wasm --output-on-failure` | 0 | `test-family-001.log`: one test, `100% tests passed out of 1` |
| Clean tree | `git status --short` | 0 | `status-001.log`: empty |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `feature-check-001.log`: all eight rows PASS, `RESULT: PASS` |

`zene` itself was not rebuilt in round 4: no product file changed since the
round-3 commits, whose `zene` build passed.

## Round 4: MSVC C2491 finding resolved by 6ee78fcac2

- Finding: `eff5c001c7:tests/CMakeLists.txt` built `WasmOfflineRenderLimitTest`
  without `LMMS_STATIC_DEFINE`, while `src/wasm/WasmSandbox.h:67` declares the
  class `LMMS_EXPORT` and `tests/src/wasm/WasmOfflineRenderLimitStubs.cpp:42`
  defines its constructor in the executable. On MSVC, `LMMS_EXPORT` expands to
  `__declspec(dllimport)` there, and defining such a function is error C2491.
- Resolution at HEAD `6ee78fcac2`: `tests/CMakeLists.txt:5628` is
  `target_compile_definitions(WasmOfflineRenderLimitTest PRIVATE LMMS_STATIC_DEFINE LMMS_TESTING)`.
- The generated export header (`build/src/lmms_export.h`, CMake
  `GenerateExportHeader`) starts with `#ifdef LMMS_STATIC_DEFINE` /
  `#define LMMS_EXPORT` (empty) before any platform branch. So on every
  compiler, all `LMMS_EXPORT` declarations seen by this target become plain
  declarations, and the stub definitions are ordinary definitions. The test
  links only Qt, not `lmmsobjs`, so no dllimport/dllexport mix remains.
- The regression still builds and passes with the define (GREEN and RESTORED
  rows above), and still fails without the product fix (RED row).
- Limit: no Windows CI lane is reachable from this machine, so the MSVC check
  is by source inspection of the generated header, not an MSVC build.
- No blocking entries remain open.

## Callers and siblings checked

- Control caller: `src/core/ControlCommandsWasmRender.cpp:241` calls
  `renderOffline` behind the schema cap at `:336`. Its largest accepted value
  is exactly 4194304, which the new boundary test proves is still accepted.
- Direct test callers: `tests/src/wasm/WasmWorkerPoolTest.cpp` uses 2 frames or
  `TestFrames` (8192), far below the cap.
- Siblings in the changed file: both `renderInline` and `renderThroughPool`
  call the widened `fillStimulus` and both use `std::size_t` offsets. No other
  offline-render loop remains with a 32-bit frame offset.
- Saved data and undo: no project format, loader, session, undo or command
  registration changed.

## Limits of verification

- `WANT_WASM` is OFF on this box, so the wasmtime-backed `WasmWorkerPoolTest`
  and `ControlWasmRenderTest` were not built or run. The new test uses test-only
  stubs, not a real module load.
- The `std::size_t` widening is unreachable under the 2^22 cap and is defensive.
  The cap itself is what the failure and exhaustion slots prove.
- The test proves refusal before the sandbox and the exact boundary acceptance;
  it does not prove audio correctness of a real wasmtime render.

## Self-check

1. **Reverse/restore:** the test fails with the product patch reversed
   (round 4 `test-red-001.log`, EXIT=8, two failures) and passes restored
   (`test-restored-001.log`, EXIT=0, `100% tests passed out of 1`).
2. **Deterministic:** inputs are literal integer counts (`4194305`,
   `UINT32_MAX`, `4194304`, `64`); the seam is the stub's
   `loadModuleFileCalls` counter. The test file contains no sleeps, clocks,
   threads or random values.
3. **Cause and completeness:** the cap is checked first in `renderOffline`
   before both render paths; both block loops and `fillStimulus` use
   `std::size_t`; the control schema already has the same 4194304 cap. The
   direct callers in the tree were checked above.
4. **No collateral damage:** the cap equals the published schema cap, so no
   control call is newly rejected; `acceptsExactlyTheLimit` proves the boundary
   is inclusive. No project/save/undo data changed; old projects load exactly
   as before.
5. **Realtime:** no audio-thread or render-period code changed. `renderOffline`
   runs on the control thread and the test only drives it there. No control
   commands were added, so `commands_snapshot.json` is untouched.
6. **Undo/GUI:** no model edit, no undo step and no GUI path changed.
7. **Portability:** the new target and test use Qt Test plus standard C++20
   types; no Qt-6-only API, no Windows headers, no filesystem assumptions.
8. **Sibling commands:** this cluster commit changes no
   `ControlCommands*`, `ControlReversibility*` or `ControlRegistry` file. The
   `app.*` and `arrangement.*` ids in the completion-check prompt come from the
   branch's base-to-HEAD train delta, not from this fix. The only relevant
   command is `wasm.render_offline`, whose schema cap remains the unchanged
   4194304 at `src/core/ControlCommandsWasmRender.cpp:336`, equal to the cap
   added here; its handler and the other command paths are unaffected.

## Delegation

Delegation: round 4 used one Haiku run for the pinned-HEAD build, the
reverse/restore RED-GREEN runs and the `Wasm` family run (logs above, checked
against the log files by the lead); the lead ran the gates, reviewed the
CMake line and export header, and wrote this report. No Sonnet or DeepSeek was
needed: round 4 changes no code.

## Other findings

- `docs/qwen-fixes-2026-10/cluster-3648.md` records a separate unvalidated
  `sample_rate` hole in the same path. It is outside this finding and was not
  changed here.
