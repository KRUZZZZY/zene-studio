Verdict: FIXED

new-p23-168: FIXED. `MidiSurfaceInput` dereferenced `Engine::audioEngine()` without a null check in two places. Both now check first, and the regression test reproduces the crash on the original header (SIGSEGV) and passes with the fix.

Finding line 87 has moved. In the current tree `subscribed()` is at [MidiSurfaceInput.h:89](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/include/MidiSurfaceInput.h) (before the fix) and [MidiSurfaceInput.h:92](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/include/MidiSurfaceInput.h) (after it).

## Evidence (file:line, on `cf3618e61` + this change)

- **Null engine is real.** `Engine::audioEngine()` returns `s_audioEngine` ([include/Engine.h:61-64](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/include/Engine.h)). That pointer is null before `Engine::init` and is cleared by `deleteHelper(&s_audioEngine)` in `Engine::destroy` ([src/core/Engine.cpp:113](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/Engine.cpp), helper at [include/Engine.h:134-139](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/include/Engine.h)).
- **Teardown keeps surfaces alive.** `Engine::destroy` closes the MCU surface ([src/core/Engine.cpp:99](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/Engine.cpp)) but does not close the launcher or remote ports. Those are released only by `LaunchBindings::disconnect` ([src/core/LaunchBindings.cpp:142](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/LaunchBindings.cpp)) and `RemoteControls::disconnect` ([src/core/RemoteControls.cpp:101](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/RemoteControls.cpp)), so an instance survives `Engine::destroy` with a `subscribed()` that dereferences a null engine.
- **Reachable through commands that do not require the engine.** `remote.state` is registered with `requiresEngine=false` ([src/core/ControlCommandsRemote.cpp:97](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/ControlCommandsRemote.cpp), registered at :120). `remoteState` calls `remote->state()` ([:88-91](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/ControlCommandsRemote.cpp)), and `state()` calls `subscribed()` ([src/core/RemoteControls.cpp:189](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/RemoteControls.cpp)). The engine-ready gate ([src/core/ControlRegistry.cpp:283](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/ControlRegistry.cpp)) applies only to commands with `requiresEngine`, so this path is not gated. (Not exercised end to end: whether the control socket still answers during `MainWindow` teardown, see residual risks.)
- **Sibling deref in `open()`, reached first on `session.bindings_connect`.** `session.bindings_connect` is also `requiresEngine=false` ([src/core/ControlCommandsLaunchBindings.cpp:162](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/ControlCommandsLaunchBindings.cpp)). Its handler calls `LaunchBindings::connect` ([src/core/LaunchBindings.cpp:134-140](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/LaunchBindings.cpp)), which calls `open()`. The original `open()` dereferenced `Engine::audioEngine()->midiClient()` at [MidiSurfaceInput.h:61](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/include/MidiSurfaceInput.h) (HEAD). So with no engine, the crash happens in `open()` before `subscribed()` is ever reached. Same class of bug, so fixed here too.
- **The fix (this commit).** [MidiSurfaceInput.h:62](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/include/MidiSurfaceInput.h): `open()` returns early when there is no engine. The surface stays closed (`isOpen()` false), and the requested input name is still recorded, so `connected:false, subscribed:false` is reported. Nothing changes when an engine exists. [MidiSurfaceInput.h:92](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/include/MidiSurfaceInput.h): `subscribed()` takes the client as `nullptr` when there is no engine, so it returns false. The repo's own idiom is `Engine::audioEngine() != nullptr ? ... : ...` (for example [src/core/ControlCommandsMcu.cpp:56](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/ControlCommandsMcu.cpp)).
- **Why `subscribed()` did not crash for a never-opened surface.** With `isOpen()` false, the original `subscribed()` short-circuits. The load of `m_midiClient` is only used when `isOpen()` is true, so the compiler can move it past the check. This is inferred from the run where a never-opened surface with no engine passed on the original header; the disassembly was not inspected. The regression test therefore uses an open surface whose engine has been destroyed (the real teardown order), which does crash.
- **Test.** New [tests/src/core/MidiSurfaceInputTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/tests/src/core/MidiSurfaceInputTest.cpp) with three cases: `subscribedAfterEngineTeardownReportsNotSubscribed` (the crash), `openWithoutAudioEngineLeavesSurfaceClosed` (the sibling crash, no `Engine::init`), and `unknownInputIsOpenButNotSubscribed` (the engine-present path, unchanged behaviour). The teardown case leaks its surface on purpose: destroying an open `MidiPort` after its client is gone runs `~MidiPort`, which dereferences the freed client ([src/core/midi/MidiPort.cpp:120-132](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/midi/MidiPort.cpp)).

## Residual risks (not fixed here)

- **Surfaces outlive the engine.** `Engine::destroy` never closes the launcher or remote ports. Their eventual destruction reaches `~MidiPort` with a freed `MidiClient` ([src/core/midi/MidiPort.cpp:128,131](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/midi/MidiPort.cpp)). This is a use-after-free that this change does not touch. The fix to make is to close those surfaces before the engine goes, the way `McuSurface::disconnect()` is called at [src/core/Engine.cpp:99](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-168/src/core/Engine.cpp). Not verified at runtime.
- **`remote.state` after teardown.** After the fix, `subscribed()` is safe, but `RemoteControls::state()` also calls `parametersOf` → `resolveControlTarget` for a set target. Whether that path touches engine objects after `Engine::destroy` is unverified. Only the `subscribed()` path was checked.
- **Whether the control socket answers during teardown** is unverified; I did not drive a live instance.

## Commands and EXIT codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-168.r1/logs/`.

| Step | Command (abridged) | EXIT | Log |
| --- | --- | --- | --- |
| Build attempt, stale tree | `cmake --build build --target MidiSurfaceInputTest -j2` | 2 (`No rule to make target`) | build-1-red.log |
| Regenerate build system via build tool | `cmake --build build --target zene -j2` | 2 (ccache temp dir `Permission denied`, sandbox) | build-2-zene-check.log |
| Rebuild with ccache temp on disk | `CCACHE_TEMPDIR="$TMPDIR/ccache-tmp-new-p23-168" cmake --build build --target zene -j2` | 0 | build-3-zene.log |
| RED build (test before fix) | `CCACHE_TEMPDIR=... cmake --build build --target MidiSurfaceInputTest -j2` | 0 | build-4-red-test.log |
| RED run (test before fix, first version) | `cd build/tests && ctest -R '^MidiSurfaceInputTest$' --output-on-failure` | 8 (SIGSEGV in `openWithoutAudioEngineLeavesSurfaceClosed`) | test-1-red.log |
| RED build after test revision | `CCACHE_TEMPDIR=... cmake --build build --target MidiSurfaceInputTest -j2` | 0 | build-5-red-test.log |
| RED run (before fix) | `cd build/tests && ctest -R '^MidiSurfaceInputTest$' --output-on-failure` | 8 (SIGSEGV in `subscribedAfterEngineTeardownReportsNotSubscribed`, load of `AudioEngine::m_midiClient` at offset 0x2428) | test-2-red.log |
| Fix applied; zene rebuilt | `CCACHE_TEMPDIR=... cmake --build build --target zene -j2` | 0 | build-6-zene-fix.log |
| Test and dependent targets rebuilt | `cmake --build build --target MidiSurfaceInputTest DeviceCopyTest MidiClockSourceTest MidiClockTest RecordingPassTest DevicePanelTest MidiClockMenuTest PerformViewTest -j2` | 0 | build-7-dependents.log |
| GREEN run (with fix) | `cd build/tests && ctest -R '^MidiSurfaceInputTest$' --output-on-failure` | 0, 100% | test-3-green.log |
| 4b revert | `git diff -- include/MidiSurfaceInput.h > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | (fix.patch) |
| 4b RED build | `cmake --build build --target MidiSurfaceInputTest -j2` | 0 | build-8-4b-reverted.log |
| 4b RED run | `ctest -R '^MidiSurfaceInputTest$' --output-on-failure` | 8 (SIGSEGV) | test-4-4b-reverted.log |
| 4b re-apply | `git apply .../logs/fix.patch` | 0 | — |
| 4b GREEN build | `cmake --build build --target MidiSurfaceInputTest -j2` | 0 | build-9-4b-fixed.log |
| 4b GREEN run | `ctest -R '^MidiSurfaceInputTest$' --output-on-failure` | 0, 100% | test-5-4b-fixed.log |
| Final rebuild of all dependents | `cmake --build build --target zene MidiSurfaceInputTest DeviceCopyTest MidiClockSourceTest MidiClockTest RecordingPassTest DevicePanelTest MidiClockMenuTest PerformViewTest -j2` | 0 | build-10-final.log |
| Affected family | `ctest -R '^(MidiSurfaceInputTest\|DeviceCopyTest\|MidiClockSourceTest\|MidiClockTest\|RecordingPassTest\|DevicePanelTest\|MidiClockMenuTest\|PerformViewTest\|.*Remote.*\|.*Launch.*\|.*Midi.*Clock.*)$' --output-on-failure` | 8 (14 tests: 11 passed, 3 `Not Run`, see below) | test-6-family.log |
| Final verbose GREEN | `ctest -R '^MidiSurfaceInputTest$' -V` | 0 (`Totals: 5 passed` = init, 3 cases, cleanup) | test-7-green-verbose.log |
| Gates, stock script | `python3 qwen/feature_check.py <worktree>` | 1 (`PermissionError` creating `<worktree>-feature-check`, outside the write-jail) | gates-1.log |
| Gates, same functions, logs in run folder | `python3 -I .../logs/gate_runner.py <worktree> .../logs/gates` (first run) | 1 (`fork-sources` and `all-sources-reproduce`: new test line not in its sorted position) | gates-2.table.log |
| Gates, after staging the test file | same, second run | 1 (same two; position wrong) | gates-3.table.log |
| Manifest regenerated from its recipe, then gates | `all-sources.txt` = header (lines 1-145) + output of the header recipe (`git ls-files ... \| LC_ALL=C sort -u`), then same runner | **0** (all 8 rows PASS) | gates-4.table.log |

Feature-check gate table (final, `gates-4.table.log`):

| Gate | Result | Note |
| --- | --- | --- |
| fork-sources | PASS | exit 0 |
| all-sources-reproduce | PASS | exit 0 |
| no-upstream-regression | PASS | exit 0 |
| check-namespace | PASS | exit 0 |
| no-tautology | PASS | exit 0 |
| doc-figures | PASS | exit 0 |
| complexity | PASS | no new item in changed files |
| file-length | PASS | no new item in changed files |

Gates: `RESULT: PASS`, runner exit 0.

Affected-family result: 11 of 14 tests passed. The three `Not Run` are `RemotePluginAudioPortsTest`, `RemotePluginClientE2ETest` and `RemotePluginShmFifoTest`. Their binaries are not built in this tree, and no file under `tests/src` that includes `RemoteControls.h`, `LaunchBindings.h`, `MidiClock.h` or `MidiSurfaceInput.h` is a RemotePlugin test (grep, exit 1). They were matched only by the `.*Remote.*` name pattern.

Environment notes: `cmake` on PATH is a shim that refuses to configure ("configuring is the parent's job"). I did not call the real CMake binary. The new test target appeared after `cmake --build build --target zene` ran the build system's own check. The ccache temp directory denied writes inside the sandbox, so I set `CCACHE_TEMPDIR` to a directory under `$TMPDIR` for the build commands only. `feature_check.py` cannot create its log folder outside the worktree in this write-jail, so I ran its exact functions (`SIMPLE`, `RATCHET`, the base cache for `cf3618e61`, and the same table logic) from `logs/gate_runner.py`, with logs in the run folder. The gate definitions and limits are unchanged.

## Files committed for this verdict

- `include/MidiSurfaceInput.h` (fix: `open()` and `subscribed()` null-engine guards)
- `tests/src/core/MidiSurfaceInputTest.cpp` (new regression test)
- `tests/CMakeLists.txt` (test registration and offscreen environment)
- `tests/fork-sources.txt` (new product-new test registered)
- `tests/all-sources.txt` (regenerated from its recipe; the only change is the new test line)
- `docs/qwen-fixes-2026-10/cluster-new-p23-168.md` (this report)

## Self-check

Completion check (`submit_check.py`, log `submit-check-1.log`, EXIT 1): commits on the branch, clean tree, report, test added, test registered - PASS. Two items still fail, answered below.

- **Regression FAILS reverted, PASSES with the fix.** Reverted: `test-4-4b-reverted.log` (EXIT 8, SIGSEGV) after `git apply -R fix.patch` and `build-8-4b-reverted.log` (EXIT 0). Fixed: `test-5-4b-fixed.log` (EXIT 0, 100%) after `git apply fix.patch` and `build-9-4b-fixed.log` (EXIT 0). The original header also crashed on the first test-run version (`test-1-red.log`, EXIT 8, `openWithoutAudioEngineLeavesSurfaceClosed`) and on the final version (`test-2-red.log`, EXIT 8, `subscribedAfterEngineTeardownReportsNotSubscribed`).
- **Deterministic.** No sleeps, no wall-clock, no timers are waited on, no races. The test uses no injected values. Its seam is the engine lifecycle: `Engine::init(true)` and `Engine::destroy()` in `tests/src/core/MidiSurfaceInputTest.cpp` decide whether `Engine::audioEngine()` is null. The MIDI client is the real one in render-only mode, with a port name that does not exist. The one non-obvious choice is leaking the teardown test's surface, which avoids destroying a port after its client is freed (`src/core/midi/MidiPort.cpp:128,131`).
- **Fix at the cause, callers and siblings checked.** Every caller of the changed code was read:
  - `open()` callers: `LaunchBindings::connect` (`src/core/LaunchBindings.cpp:137`) and `RemoteControls::connect` (`src/core/RemoteControls.cpp:95`). An unopened surface keeps `m_port` null. `RemoteControls::setTarget` → `sendPage()` returns early when not open (`src/core/RemoteControls.cpp:165`), and the base `send()` checks `m_port` (`include/MidiSurfaceInput.h:105`). `close()` resets a null port safely.
  - `subscribed()` callers: `src/core/ControlCommandsLaunchBindings.cpp:132` and `src/core/RemoteControls.cpp:189` (via `state()`). Both still return a boolean, now false when there is no engine.
  - The other engine dereference in the class is in `open()` only; `send()`, `close()`, `drain()` and `decode()` do not touch the engine.
  - Not fixed (residual, see above): surfaces that outlive the engine, and `~MidiPort` after teardown.
- **No collateral damage.** The only behaviour change is the no-engine case, which previously faulted. With an engine, `open()` and `subscribed()` run the same code as before. `unknownInputIsOpenButNotSubscribed` covers the engine-present path and passes with the fix (`test-7-green-verbose.log`). It was not run on the original header: the earlier crash in a preceding test aborts the executable before it runs, so its pass-on-both status is unverified. Nothing is saved or loaded from these classes, and no limit or check was added. Realtime: `open()`/`subscribed()` run on the UI or control thread, not on the audio thread, and the change adds no allocation, locking or logging on any audio path. No control command was added (no `ControlCommandsSnapshot` change).
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE**, so no contract citation is needed beyond the `file:line` evidence above.

Gate status: the stock `feature_check.py` exits 1 before running any gate, with `PermissionError` creating `<worktree>-feature-check` in the projects directory (`gates-1.log`). That location is outside the write-jail, so it cannot pass from this lane. Its functions, run with logs in the run folder, pass all eight rows (`gates-4.table.log`, EXIT 0). The `gates (feature_check.py)` row of `submit_check.py` will keep failing for this reason until the stock script can write its logs here.
