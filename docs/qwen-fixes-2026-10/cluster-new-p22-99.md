Verdict: FIXED

new-p22-99: `ControlDeviceSupport::resolveTrackTarget` dereferenced `Engine::getSong()` with no null check. The finding is real. It is reachable through an engine-free command, `remote.target`, when no song is open.

## Evidence

Before the fix, the unguarded dereference was `src/core/ControlDeviceSupport.cpp:83` (`Engine::getSong()->tracks()`), exactly the finding's line. A grep for `getSong` in that file found no other dereference.

Reachability, read from the code (this path is not run end to end; see "Not verified" below):

- `remote.target` is registered engine-free: `src/core/ControlCommandsRemote.cpp:97` passes `requiresEngine = false`. `ControlRegistry::invoke` gates a command on `isEngineReady()` only when `cmd->requiresEngine` is set (`src/core/ControlRegistry.cpp:283`).
- It hands the caller's id to the resolver: `src/core/ControlCommandsRemote.cpp:72`, `resolveControlTarget(args.value("target")...)`.
- It needs a connected remote. `remote.connect` is also engine-free (`src/core/ControlCommandsRemote.cpp:97`), and `RemoteControls::connect` (`src/core/RemoteControls.cpp:85-99`) does not read the song.
- A null song is a real state, not a theoretical one: `ControlRegistry::isEngineReady()` (`src/core/ControlRegistry.cpp:205-207`) returns false when `Engine::getSong() == nullptr`.

Other callers of the resolver, all checked:

- Every other caller sits behind a command with the default `requiresEngine = true` (`include/ControlCommandDef.h:53`), so the dispatch gate at `src/core/ControlRegistry.cpp:283` keeps a null song out.
- `control.id_contract` is engine-free (`src/core/ControlCommandsIdContract.cpp:79`). Its track-resolver call (`:111`) sits inside `if (song != nullptr)` (`:92`). Its channel-resolver call (`:62`) goes through `resolveChannelTarget`, which never touches the song. Behaviour there is unchanged.

Sibling not changed: `src/core/ControlAutomationSupport.cpp:100` (`songIndexOfTrack`) also dereferences `Engine::getSong()` without a check. It takes a `Track*`, and a track that exists belongs to a song, so I found no no-song path to it. It is outside this finding and I did not change it. I did not check its callers exhaustively.

## Decision

REAL. Reachable through `remote.connect` then `remote.target {target:"trk-N"}` with no song open. The fix is a null guard at the cause.

## Fix

`src/core/ControlDeviceSupport.cpp:81-89`: when `Engine::getSong()` is null, return `NotFound` with the message `no track trk-N (no song is open)`. `NotFound` is the kind the same function already returns for a missing track (`src/core/ControlDeviceSupport.cpp:100`). The statements after the guard are the same as before (`song->tracks()` at `:92`).

## Regression test

`tests/src/core/ControlDeviceVendorTest.cpp:126-136`, slot `trackTargetWithNoSongIsNotFound`. It calls `Engine::destroy()` to reach the no-song state, then requires `resolveControlTarget("trk-1")` to return false with `ControlErrorKind::NotFound`. The slot is declared last, because it tears the engine down. `cleanupTestCase` (`:84-87`) now calls `Engine::destroy()` only if a song still exists, so the engine is not destroyed twice.

Deterministic: no timing, no sleeps, no threads. The no-song state comes from `Engine::destroy()`, which is synchronous.

## Commands and exit codes

Run from the worktree unless noted. LOG = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-99.r1/logs`.

Build environment note: the first build failed at ccache (`ccache: error: failed to create temporary file ... Permission denied`), not in the code. Builds after that set `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p22-99`.

| Step | Command | EXIT | Result / log |
| --- | --- | --- | --- |
| Build, default ccache temp dir | `cmake --build build --target ControlDeviceVendorTest -j4 > LOG/build-1.log 2>&1` | 2 | ccache permission error, not a code error (`build-1.log`) |
| Build, RED (product unfixed) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p22-99 cmake --build build --target ControlDeviceVendorTest -j4 > LOG/build-2.log 2>&1` | 0 | built |
| RED | `cd build/tests && ctest -R '^ControlDeviceVendorTest$' --output-on-failure > LOG/test-red-1.log 2>&1` | 8 | `listedVendorIsThePickerVendor` PASS, then SIGSEGV (signal 11) in `trackTargetWithNoSongIsNotFound` (`test-red-1.log:11`, `:49`) |
| Build, GREEN | `CCACHE_TEMPDIR=... cmake --build build --target ControlDeviceVendorTest -j4 > LOG/build-3.log 2>&1` | 0 | rebuilt `lmmsobjs` including `ControlDeviceSupport.cpp` |
| GREEN | `ctest -R '^ControlDeviceVendorTest$' --output-on-failure > LOG/test-green-1.log 2>&1` | 0 | 1/1 test passed |
| GREEN, verbose | `ctest -R '^ControlDeviceVendorTest$' -V > LOG/test-green-verbose-1.log 2>&1` | 0 | `PASS : trackTargetWithNoSongIsNotFound()`; Totals 4 passed, 0 failed |
| 4b: save fix as patch | `git diff -- src/core/ControlDeviceSupport.cpp > LOG/fix.patch` | 0 | |
| 4b: revert fix | `git apply -R LOG/fix.patch` | 0 | `git diff --stat` then showed only the test file |
| 4b: rebuild, fix reverted | `cmake --build build --target ControlDeviceVendorTest -j4 > LOG/build-4-revert.log 2>&1` | 0 | |
| 4b: test, fix reverted | `ctest -R '^ControlDeviceVendorTest$' --output-on-failure > LOG/test-4b-revert.log 2>&1` | 8 | SIGSEGV again (`test-4b-revert.log:11`), 0% passed |
| 4b: reapply fix | `git apply LOG/fix.patch` | 0 | `git diff --stat`: ControlDeviceSupport.cpp and ControlDeviceVendorTest.cpp |
| 4b: rebuild, fix applied | `cmake --build build --target ControlDeviceVendorTest -j4 > LOG/build-5-fix.log 2>&1` | 0 | |
| 4b: test, fix applied | `ctest -R '^ControlDeviceVendorTest$' --output-on-failure > LOG/test-4b-fix.log 2>&1` | 0 | 100% passed |

Affected family (`ctest -R 'ControlDevice|DelayControlsTest|EffectRackAddTest'`, run from build/tests):

| Step | Command | EXIT | Result / log |
| --- | --- | --- | --- |
| Build family | `cmake --build build --target zene DelayControlsTest EffectRackAddTest ControlDeviceVendorTest -j4 > LOG/build-6-family.log 2>&1` | 0 | |
| Build missing target | `cmake --build build --target ControlDeviceCatalogueTest -j4 > LOG/build-7-catalogue.log 2>&1` | 0 | This target had never been built in this tree, so ctest reported it "Not Run" |
| Family, first run | `ctest -R 'ControlDevice\|DelayControlsTest\|EffectRackAddTest' ... > LOG/test-family-2.log` | 8 | `ControlDeviceCatalogueTest` failed at `tests/src/core/ControlDeviceCatalogueTest.cpp:189`: "Patman plugin is unavailable". Cause: `build/plugins/Patman` had no built shared object (`find` found only moc files). The test never reaches the track resolver at that line |
| Build patman plugin | `cmake --build build --target patman -j4 > LOG/build-8-patman.log 2>&1` | 0 | plugin built |
| Family, final | `ctest -R 'ControlDevice\|DelayControlsTest\|EffectRackAddTest' ... > LOG/test-family-3.log` | 0 | 5/5 passed: ControlDeviceCatalogueTest, ControlDeviceVendorTest, EffectRackAddTest, DelayControlsTest, ControlDevices |

Gates:

| Gate | Command | EXIT | Result |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > LOG/gate-complexity-1.log 2>&1` | 1 | 7 REGRESSION items, none in a file this branch changed (`gate-complexity-1.log`). By the script's rule (fail only on new items in changed files) this row would pass. Not confirmed by the script itself |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > LOG/gate-file-length-1.log 2>&1` | 1 | 22 REGRESSION items, none in a file this branch changed. Same reasoning as complexity |
| feature_check.py | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOG/gates-1.log 2>&1` | 1 | Not run. The script creates `<worktree>-feature-check/` beside the worktree, outside the write-jail: `PermissionError` at `logs.mkdir` (`gates-1.log`). The script has no output override |

The six simple gates were run directly from the worktree, in the script's order, and all exit 0. The two ratchets were run directly; the table above reports their raw exit codes. `feature_check.py` itself could not run in this session, so its single PASS/FAIL table does not exist in this session's output. `submit_check.py` also runs `feature_check.py` and reports its row as FAIL for the same reason.

Files committed: `src/core/ControlDeviceSupport.cpp`, `tests/src/core/ControlDeviceVendorTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p22-99.md`.

Not verified:

- The remote path end to end. I did not run `remote.connect` then `remote.target` through the control socket, because `RemoteControls::open` needs a MIDI port. The regression calls the resolver directly with no song, which is the failing function. The reachability chain above is from reading code.
- `feature_check.py` as a single run (write-jail, see above).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** RED: `ctest` EXIT 8, SIGSEGV in `trackTargetWithNoSongIsNotFound` (`test-red-1.log:11`). GREEN: `ctest` EXIT 0 (`test-green-1.log`); verbose run shows the slot PASS (`test-green-verbose-1.log`). Step 4b repeated the revert and reapply: EXIT 8 with the fix reverted (`test-4b-revert.log:11`), EXIT 0 after reapplying (`test-4b-fix.log`). The revert used `git apply -R` on `fix.patch`, which is the product diff only.
- **The test is deterministic.** No wall-clock timing, sleeps, threads or races. The no-song state comes from `Engine::destroy()` (synchronous, `src/core/Engine.cpp:96-125`). The slot is last in the class, and `cleanupTestCase` no longer tears the engine down twice.
- **The fix is at the cause and complete.** The cause is the unguarded `Engine::getSong()` in `resolveTrackTarget`, the only such dereference in `ControlDeviceSupport.cpp`. Callers: engine-free `remote.target` now gets a typed NotFound (`ControlCommandsRemote.cpp:72`). Engine-gated callers cannot see a null song (`ControlRegistry.cpp:283`, `:207`). `control.id_contract` is guarded (`ControlCommandsIdContract.cpp:92`). Not changed: `ControlAutomationSupport.cpp:100`, a sibling in another file that I did not find a no-song path to (see Evidence).
- **No collateral damage.** The change adds an early return only when no song exists. With a song, the statements run as before. No saved data or project-load path is touched. No limit or check was added. The `NotFound` kind is the one this function already uses. The regression test's engine teardown is guarded, so the other slots and the existing ctest family are unaffected (family 5/5 pass).
- **Verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE proof is needed.**

Realtime, undo and commands: the resolver runs on the control thread, not an audio path. The added code allocates only on the error path (a formatted message), the same as the other error returns in this function. It is not undoable state. No new control command was added, so the `ControlCommandsSnapshot` canary is unchanged.
