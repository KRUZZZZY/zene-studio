Verdict: FIXED

new-p22-99: `ControlDeviceSupport::resolveTrackTarget` dereferenced `Engine::getSong()` with no null check. The finding is real. It is reachable through an engine-free command, `remote.target`, when no song is open.

## Round 2 (review of round 1)

The review's negative control (logs: `new-p22-99.r1/negctl`) reverted the product fix and ran `DelayControlsTest` and `EffectRackAddTest`. Both passed. That was correct: neither test ever reached a no-song state, because each initialises the engine with a song. My round-1 regression lived only in `ControlDeviceVendorTest`, which the negative control did not run.

Round 2 adds the same no-song regression to `DelayControlsTest` and `EffectRackAddTest`, so each of the three tests reaches the changed branch. Each one fails with the fix reverted and passes with it. The product code is unchanged from round 1.

## Evidence

Before the fix, the unguarded dereference was `src/core/ControlDeviceSupport.cpp:83` (`Engine::getSong()->tracks()`), exactly the finding's line. A grep for `getSong` in that file found no other dereference.

Reachability, read from the code (this path is not run end to end; see "Not verified" below):

- `remote.target` is registered engine-free: `src/core/ControlCommandsRemote.cpp:97` passes `requiresEngine = false`. `ControlRegistry::invoke` gates a command on `isEngineReady()` only when `cmd->requiresEngine` is set (`src/core/ControlRegistry.cpp:283`).
- It hands the caller's id to the resolver: `src/core/ControlCommandsRemote.cpp:72`, `resolveControlTarget(args.value("target")...)`.
- It needs a connected remote. `remote.connect` is also engine-free (`src/core/ControlCommandsRemote.cpp:97`), and `RemoteControls::connect` (`src/core/RemoteControls.cpp:85-99`) does not read the song.
- A null song is a real state, not a theoretical one: `ControlRegistry::isEngineReady()` (`src/core/ControlRegistry.cpp:205-207`) returns false when `Engine::getSong() == nullptr`.

Other callers checked:

- `PatcherCanvas` (`src/gui/PatcherCanvas.cpp:77,251`) and `RemoteControls::parametersForTarget` (`src/core/RemoteControls.cpp:50`) also call the resolver directly. Both handle a failed resolution; the null guard makes a no-song track lookup return `NotFound` there too.
- `control.id_contract` is engine-free (`src/core/ControlCommandsIdContract.cpp:79`). Its track-resolver call sits inside `if (song != nullptr)` (`:92`). Its channel-resolver call (`:62`) goes through `resolveChannelTarget`, which never touches the song. Behaviour there is unchanged.
- The remaining control-command callers use the default `requiresEngine = true` (`include/ControlCommandDef.h:53`), so the dispatch gate at `src/core/ControlRegistry.cpp:283` keeps a null song out of those handlers.

Sibling not changed: `src/core/ControlAutomationSupport.cpp:100` (`songIndexOfTrack`) also dereferences `Engine::getSong()` without a check. It takes a `Track*`, and a track that exists belongs to a song, so I found no no-song path to it. It is outside this finding and I did not change it. I did not check its callers exhaustively.

## Decision

REAL. Reachable through `remote.connect` then `remote.target {target:"trk-N"}` with no song open. The fix is a null guard at the cause.

## Fix

`src/core/ControlDeviceSupport.cpp:81-89`: when `Engine::getSong()` is null, return `NotFound` with the message `no track trk-N (no song is open)`. `NotFound` is the kind the same function already returns for a missing track (`src/core/ControlDeviceSupport.cpp:100`). The statements after the guard are the same as before (`song->tracks()` at `:92`).

## Regression tests

The same slot, `trackTargetWithNoSongIsNotFound`, is in three files. Each one calls `Engine::destroy()` to reach the no-song state, then requires `resolveControlTarget("trk-1")` to return false with `ControlErrorKind::NotFound`. Each slot is declared last, because it tears the engine down. Each file's `cleanupTestCase` now calls `Engine::destroy()` only if a song still exists, so the engine is not destroyed twice.

- `tests/src/core/ControlDeviceVendorTest.cpp:126-134` (`cleanupTestCase` at `:82-87`)
- `tests/src/plugins/DelayControlsTest.cpp:94-102` (`cleanupTestCase` at `:57-61`)
- `tests/src/gui/EffectRackAddTest.cpp:156-164` (`cleanupTestCase` at `:95-100`)

These slots are not the natural home for the test. `DelayControlsTest` and `EffectRackAddTest` cover other features. The slots are there because the brief named these files as covering this resolver, and the review asked that each one fail without the fix.

Deterministic: no timing, no sleeps, no threads. The no-song state comes from `Engine::destroy()`, which is synchronous.

## Commands and exit codes

Run from the worktree unless noted. Round-2 logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-99.r2/logs` (LOG below). Round-1 logs remain in `new-p22-99.r1/logs`.

Build environment note: the first build in round 1 failed at ccache (`ccache: error: failed to create temporary file ... Permission denied`), not in the code. Builds set `CCACHE_TEMPDIR` to a folder on disk.

| Step | Command | EXIT | Result / log |
| --- | --- | --- | --- |
| GREEN build, fix applied | `cmake --build build --target DelayControlsTest EffectRackAddTest ControlDeviceVendorTest -j4 > LOG/build-green-1.log 2>&1` | 0 | built |
| GREEN | `cd build/tests && ctest -R '^(DelayControlsTest\|EffectRackAddTest\|ControlDeviceVendorTest)$' -V > LOG/test-green-1.log 2>&1` | 0 | 3/3 pass; each `trackTargetWithNoSongIsNotFound` PASS (4 slots passed in each file) |
| Revert fix | `git diff 639e1d23e afc9cd3e2 -- src/core/ControlDeviceSupport.cpp > LOG/product.patch`, then `git apply -R LOG/product.patch` | 0, 0 | product file reverted; test files untouched |
| RED build, fix reverted | `cmake --build build --target DelayControlsTest EffectRackAddTest ControlDeviceVendorTest -j4 > LOG/build-red-1.log 2>&1` | 0 | `ControlDeviceSupport.cpp.o` recompiled (`build-red-1.log`) |
| RED | `ctest -R '^(DelayControlsTest\|EffectRackAddTest\|ControlDeviceVendorTest)$' -V > LOG/test-red-1.log 2>&1` | 8 | 0/3 pass. Each file segfaults (signal 11) in `trackTargetWithNoSongIsNotFound`: ControlDeviceVendorTest at `test-red-1.log:24`, EffectRackAddTest at `:1218`, DelayControlsTest at `:2734` |
| Reapply fix | `git apply LOG/product.patch` | 0 | `git diff --stat` showed only the two test files, then the product file restored |
| GREEN build, fix reapplied | `cmake --build build --target DelayControlsTest EffectRackAddTest ControlDeviceVendorTest -j4 > LOG/build-fix-2.log 2>&1` | 0 | `ControlDeviceSupport.cpp.o` recompiled |
| GREEN, fix reapplied | `ctest -R '^(DelayControlsTest\|EffectRackAddTest\|ControlDeviceVendorTest)$' -V > LOG/test-fix-2.log 2>&1` | 0 | 3/3 pass |

Affected family (`ctest -R 'ControlDevice|DelayControlsTest|EffectRackAddTest'`, run from build/tests):

| Step | Command | EXIT | Result / log |
| --- | --- | --- | --- |
| Build missing-from-round-1 target | `cmake --build build --target ControlDeviceCatalogueTest -j4 > LOG/build-family-1.log 2>&1` | 0 | |
| Family | `ctest -R 'ControlDevice\|DelayControlsTest\|EffectRackAddTest' -V > LOG/test-family-1.log 2>&1` | 0 | 5/5 pass: ControlDeviceCatalogueTest, ControlDeviceVendorTest, EffectRackAddTest, DelayControlsTest, ControlDevices |

The family's `ControlDeviceCatalogueTest` depends on the patman plugin. Round 1 built it (`cmake --build build --target patman`, EXIT 0). It is still built in this tree.

Gates (run directly from the worktree, logs in LOG):

| Gate | Command | EXIT | Result |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh > LOG/gate-fork-sources-1.log 2>&1` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > LOG/gate-all-sources-1.log 2>&1` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > LOG/gate-no-upstream-1.log 2>&1` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace > LOG/gate-check-namespace-1.log 2>&1` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh > LOG/gate-no-tautology-1.log 2>&1` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py > LOG/gate-doc-figures-1.log 2>&1` | 0 | PASS |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > LOG/gate-complexity-1.log 2>&1` | 1 | 7 REGRESSION items, none in a file this branch changed (grep count 0 for the four changed files). By the script's rule this row would pass. Not confirmed by the script itself |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > LOG/gate-file-length-1.log 2>&1` | 1 | 22 REGRESSION items, none in a changed file (grep count 0). Same reasoning as complexity |
| feature_check.py | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 (round 1) | Not run. The script creates `<worktree>-feature-check/` beside the worktree, outside the write-jail: `PermissionError` at `logs.mkdir` (round-1 `gates-1.log`). The script has no output override |

The round-1 `submit_check.py` run also reported the feature_check row as FAIL for the same reason.

Files committed in round 2: `tests/src/core/ControlDeviceVendorTest.cpp` (already committed in round 1; unchanged this round), `tests/src/plugins/DelayControlsTest.cpp`, `tests/src/gui/EffectRackAddTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p22-99.md`. The product file `src/core/ControlDeviceSupport.cpp` is unchanged from round 1.

Not verified:

- The remote path end to end. I did not run `remote.connect` then `remote.target` through the control socket, because `RemoteControls::open` needs a MIDI port. The regression calls the resolver directly with no song, which is the failing function. The reachability chain above is from reading code.
- `feature_check.py` as a single run (write-jail, see above).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Each of the three tests, with the fix reverted: `ctest` EXIT 8, SIGSEGV in `trackTargetWithNoSongIsNotFound` (`test-red-1.log`). With the fix applied: `ctest` EXIT 0 for all three (`test-green-1.log`, `test-fix-2.log`). The revert used `git apply -R` on `product.patch`, which is the product diff only. Round 1's `ControlDeviceVendorTest` evidence is in `new-p22-99.r1/logs`.
- **The test is deterministic.** No wall-clock timing, sleeps, threads or races. The no-song state comes from `Engine::destroy()` (synchronous, `src/core/Engine.cpp:96-125`). Each slot is last in its class, and each `cleanupTestCase` no longer tears the engine down twice.
- **The fix is at the cause and complete.** The cause is the unguarded `Engine::getSong()` in `resolveTrackTarget`, the only such dereference in `ControlDeviceSupport.cpp`. Callers: engine-free `remote.target` now gets a typed NotFound (`ControlCommandsRemote.cpp:72`). Engine-gated callers cannot see a null song (`ControlRegistry.cpp:283`, `:207`). `control.id_contract` is guarded (`ControlCommandsIdContract.cpp:92`). Not changed: `ControlAutomationSupport.cpp:100`, a sibling in another file that I did not find a no-song path to (see Evidence).
- **No collateral damage.** The change adds an early return only when no song exists. With a song, the statements run as before. No saved data or project-load path is touched. No limit or check was added. The `NotFound` kind is the one this function already uses. The new slots run last and their cleanup is guarded, so the existing slots in each file are unaffected. The family is 5/5.
- **Verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE proof is needed.**

Realtime, undo and commands: the resolver runs on the control thread, not an audio path. The added code allocates only on the error path (a formatted message), the same as the other error returns in this function. It is not undoable state. No new control command was added, so the `ControlCommandsSnapshot` canary is unchanged.
