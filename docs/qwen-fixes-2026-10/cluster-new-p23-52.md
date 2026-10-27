Verdict: FIXED

new-p23-52: ControlRegistry::destroy() leaves s_ready true, so a recreated registry reports the engine ready before it is.

`destroy()` deleted and nulled the singleton but never reset the static readiness flag, so the next `instance()` inherited "startup complete". The finding's **crash** claim does not hold: `isEngineReady()` still requires the song and the mixer, and `invoke()` still refuses engine commands as busy. The real defect is the readiness report: `startupComplete()` returned true and `readinessReport()` reported `engine_missing` instead of `engine_starting`. The fix resets the flag in `destroy()`.

## Evidence (file:line on branch bugs/qwen-new-p23-52, base 5c1c6715d9)

| Claim | Evidence |
| --- | --- |
| `destroy()` deleted the instance without touching readiness | `src/core/ControlRegistry.cpp:171-175` (before the fix): `delete s_instance; s_instance = nullptr;`. The flag is `static bool s_ready` at `:53` |
| The flag is read by the readiness report and `startupComplete()` | `src/core/ControlRegistry.cpp:205-208` `isEngineReady()` = `s_ready && Engine::getSong() && Engine::mixer()`; `:215-218` `startupComplete()` returns `s_ready`; `src/core/ControlSession.cpp:64` `report.ready = isEngineReady()`, `:71` `engine_starting` when `!s_ready`, `:79` `engine_missing` otherwise |
| The crash path is not reachable | `src/core/ControlRegistry.cpp:283` gates `requiresEngine && !isEngineReady()` to `busy` before any handler; `isEngineReady()` still needs the song, which `Engine::getSong()` covers |
| No production caller of `destroy()` | `rg "ControlRegistry::destroy"` over `src` and `include` finds only the definition; callers are tests: `tests/src/core/SafeStartTest.cpp:200`, `tests/src/core/ControlShutdownHookTest.cpp:489,522`. So the defect is latent in production and reachable through the registry's own lifecycle |
| Existing callers already work around it | `tests/src/core/SafeStartTest.cpp:199` calls `setReady(false)` right before `destroy()`, which is the contract the fix makes automatic |
| Fix at the cause | `src/core/ControlRegistry.cpp:171-178`: `destroy()` now sets `s_ready = false` after deleting the instance, with a comment saying readiness belongs to the instance the application told |
| Regression (new cases) | `tests/src/core/ControlRegistryNestingTest.cpp:578` `recreatedRegistryDoesNotInheritReadiness` (the bug: `startupComplete()` true after destroy and recreate) and `:593` `applicationReadinessReachesTheRecreatedRegistry` (the application's `setReady(true)` still reaches the recreated registry) |

Why the cases live in `ControlRegistryNestingTest.cpp`: the brief's suggested file `ControlRegistryTest.cpp` sat at 499 code lines against the 500 file-length limit, so my 12-line case pushed it to 511 (measured by `tests/file-length-gate.sh`'s counting rule). That was reverted. `ControlShutdownHookTest.cpp` (672 code lines) and `SafeStartTest.cpp` (612) are over the limit already, and a new test target needs a CMake reconfigure, which the lane build tree forbids (`bunny-guard: configuring is the parent's job`), so a new suite was not an option. The nesting file (467 code lines before, 488 after) is one of the brief's suggested files and has room.

## Matrix covered

| Path | Test |
| --- | --- |
| normal: app sets ready, then registry is ready | `initTestCase` sets ready; the existing `ControlRegistryTest` cases |
| destroy then recreate before startup completes (the finding) | `recreatedRegistryDoesNotInheritReadiness` |
| reconfiguration: the application makes the recreated registry ready again | `applicationReadinessReachesTheRecreatedRegistry` |
| existing destroy callers (test-only in this tree) | `SafeStartTest`, `ControlShutdownHookTest`, `ControlRegistryTest` (run below) |
| audio thread | not applicable: `destroy()`, `setReady()`, `startupComplete()` are control-thread only, no allocation or lock added |
| undo / commands | not applicable; no command added, no snapshot change (`docs/` canary untouched) |
| platform | Linux build; `ControlShutdownHookTest` skips one Windows-only named-pipe case, pre-existing, not changed |

## Commands run and exit codes

Lane build: the default ccache temp dir (`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not writable from this lane, so every build below ran with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-p23-52` (first attempt, `build-1.log`, EXIT=2 with "Permission denied").

| # | Command (run from the worktree / `build/tests`) | Log (LOG FOLDER) | EXIT |
| --- | --- | --- | --- |
| B1 | `cmake --build build --target ControlRegistryTest -j4` (before the fix; first attempt, unwritable ccache) | build-1.log | 2 |
| B2 | same, with `CCACHE_TEMPDIR` set | build-2.log | 0 |
| R1 | `ctest -R '^ControlRegistryTest$' --output-on-failure` (fix absent, first placement in ControlRegistryTest.cpp) | test-red-1.log | 8 (`28 passed, 1 failed`, `recreatedRegistryDoesNotInheritReadiness` at `!startupComplete()` returned FALSE) |
| G1 | fix applied; rebuild ControlRegistryTest; `ctest -R '^ControlRegistryTest$' -V` | build-3.log, test-green-1.log, test-green-2.log | 0, 0 (`29 passed`) |
| — | `cmake --build build --target ControlRegistryNestingTest StartHubTest ControlShutdownHookTest SafeStartTest -j4` | build-4.log | 0 |
| — | `ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|StartHubTest\|ControlShutdownHookTest\|SafeStartTest)$' -V` | test-green-2.log | 0 (5/5 tests) |
| 4b.1 | `git diff -- src/core/ControlRegistry.cpp > logs/fix.patch` | fix.patch | 0 |
| 4b.2 | `git apply -R logs/fix.patch` (fix removed) | — | 0 |
| 4b.3 | rebuild ControlRegistryTest, run (RED for the first placement) | build-5.log, test-red-2.log | 0, 8 (`28 passed, 1 failed`) |
| 4b.4 | `git apply logs/fix.patch` (fix restored) | — | 0 |
| 4b.5 | rebuild ControlRegistryTest, run (GREEN) | build-6.log, test-green-3.log | 0, 0 (`29 passed`) |
| — | `zene` target rebuilt with the fix | build-7.log | 0 |
| F1 | the first placement was removed; the two cases moved to `ControlRegistryNestingTest.cpp` | — | — |
| R2 | fix removed again (`git apply -R logs/fix.patch`); `cmake --build build --target ControlRegistryNestingTest -j4` | build-8.log (EXIT 2: a new CMake target could not be configured, see above), build-10.log (EXIT 0) | 2, 0 |
| R2 | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (RED, fix absent) | test-red-4.log | 8 (`15 passed, 2 failed`: `recreatedRegistryDoesNotInheritReadiness` and `applicationReadinessReachesTheRecreatedRegistry`, both at `!ControlRegistry::startupComplete()` returned FALSE) |
| 4b.4 | `git apply logs/fix.patch` (fix restored) | — | 0 |
| G2 | rebuild ControlRegistryNestingTest (build-11.log), `ctest -R '^ControlRegistryNestingTest$' -V` | build-11.log, test-green-4.log | 0, 0 (`17 passed`) |
| G3 | `cmake --build build --target zene ControlRegistryTest ControlRegistryNestingTest StartHubTest ControlShutdownHookTest SafeStartTest -j4`, then `ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|StartHubTest\|ControlShutdownHookTest\|SafeStartTest)$' -V` | build-12.log, test-green-5.log | 0, 0 |

Family results (test-green-5.log, final tree):

| Test (ctest) | Passed | Failed | Skipped |
| --- | --- | --- | --- |
| ControlRegistryTest | 28 | 0 | 0 |
| ControlRegistryNestingTest | 17 | 0 | 0 |
| ControlShutdownHookTest | 13 | 0 | 1 (Windows-only named-pipe case, pre-existing) |
| SafeStartTest | 20 | 0 | 0 |
| StartHubTest | 3 | 0 | 0 |
| **Total executed** | **81** | **0** | **1** |

The RED and GREEN files are the same test cases on the same product code; only the fix diff differs (`logs/fix.patch`).

Gates. `feature_check.py` could not run: it writes its logs to `<worktree>/../<name>-feature-check`, which is outside the lane's write-jail (`PermissionError`, see `feature-check-1.log`). I ran each gate command the script runs, from the worktree, with the same cwd and arguments, logging to the LOG FOLDER:

| Gate | Command | Log | EXIT |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | gate2-fork-sources.log | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | gate2-all-sources-reproduce.log | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | gate2-no-upstream-regression.log | 0 |
| check-namespace | `tests/scripted/check-namespace` | gate2-check-namespace.log | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | gate2-no-tautology.log | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | gate2-doc-figures.log | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | gate2-complexity.log | 1 (train-wide; see below) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | gate2-file-length.log | 1 (train-wide; see below) |

The two ratchets exit 1 on the base train's own violations: 28 `REGRESSION:` lines in total, none naming a file this branch changes. The lines name `ControlShutdownHookTest.cpp`, `ArrangerTimelineTest.cpp`, `CollabTransportTest.cpp`, `WasmSandbox.cpp` and similar, all untouched here. `feature_check.py` fails a ratchet only on items in changed files, so by its rule both rows pass. Code-line counts with the gate's rule (`tests/file-length-gate.sh`, blank and comment-only lines excluded): `src/core/ControlRegistry.cpp` 495 (limit 500), `tests/src/core/ControlRegistryNestingTest.cpp` 488 (limit 500).

## Files committed

- `src/core/ControlRegistry.cpp` (3 lines: the `s_ready = false` reset in `destroy()` with its comment)
- `tests/src/core/ControlRegistryNestingTest.cpp` (29 lines: the two cases)
- `docs/qwen-fixes-2026-10/cluster-new-p23-52.md` (this report)

Not changed on purpose: `tests/CMakeLists.txt` and `tests/fork-sources.txt` (briefly edited for a new test file, then reverted). `tests/upstream-modifications.txt` (both product and test files are fork files, not inherited upstream files).

## Not checked

- Other statics that `destroy()` does not reset (`s_quitPending`, `s_quitAnswer`, `s_quitGuard`, `ControlRegistry.h:362-366`). They may have the same survival question. Out of scope for this finding, not verified here.
- No production path calls `destroy()` in this tree, so the fix changes nothing in the running app. It corrects the registry's own contract for any future or test caller.
