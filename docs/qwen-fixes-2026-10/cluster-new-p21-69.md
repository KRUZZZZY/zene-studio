Verdict: FALSE-POSITIVE

new-p21-69: audio-recording arm dereferences a possibly-null audio engine (src/core/ControlCommandsSessionAudioRecord.cpp:150)

## Finding

Luna reported that `armAndLaunch()` reads `Engine::audioEngine()` at `ControlCommandsSessionAudioRecord.cpp:150` and dereferences it (`engine->inputSampleRate()`, `engine->recordingLatencyFrames()` at lines 152) with no null check, so `session.record_slot` would crash when run without an audio engine.

## Why it cannot happen: the dispatch gate

1. `session.record_slot` is registered with the default `requiresEngine`: `registerRecordSlot` (`src/core/ControlCommandsSessionAudioRecord.cpp:281-299`) sets `cmd.handler = recordSlot` at line 298 and never clears the flag. The default is `true` (`include/ControlRegistry.h:104`; `include/ControlCommandDef.h:53`).
2. `ControlRegistry::invoke` refuses every such command before the handler runs: `if (cmd->requiresEngine && !isEngineReady())` at `src/core/ControlRegistry.cpp:283` returns `Busy`.
3. `ControlRegistry::isEngineReady()` (`src/core/ControlRegistry.cpp:205-207`) returns `s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr`.
4. The mixer is created after the audio engine and destroyed before it: `Engine::init` creates `s_audioEngine` at `src/core/Engine.cpp:71` and `s_mixer` at `:73`; `Engine::destroy` deletes `s_mixer` at `:112` and `s_audioEngine` at `:113`. So whenever the gate passes, `Engine::audioEngine()` is non-null (`Engine::mixer()` non-null implies `Engine::init` ran and teardown has not reached the mixer).
5. Teardown ordering: `Engine::destroy` deletes the song at `:123`, after the audio engine. The handler's first step, `sessionModelOrNull` (`src/core/ControlCommandsSessionShared.h:198-208`), refuses when `Engine::getSong()` is null. The invoke path blocks on the UI thread (`src/core/ControlRegistry.cpp:257-258`, `BlockingQueuedConnection`), so a queued handler cannot run in the middle of `Engine::destroy`. I checked the `destroy` body and the blocking call; I did not audit every callee of `Engine::destroy` for event-loop pumping.

The only other reader of the engine in this file, `withEngineHeld` (`ControlCommandsSessionAudioRecord.cpp:83`), is null-safe, and the file's other engine-dependent commands (51 call sites across `src/core/ControlCommands*.cpp`) rely on the same gate. Adding a null check at line 150 would be dead code on every reachable path, so I did not change the product code.

## Contract pin (test only)

`tests/src/core/SessionAudioRecordTest.cpp:245-254`: `aRecordingIsRefusedBeforeTheEngineIsAddressable` sets `ControlRegistry::setReady(false)`, invokes `session.record_slot`, restores readiness before any assertion, and asserts `Busy` with the cell left `idle`. No timing, no sleeps.

Because the product code is unchanged, there is no RED→GREEN pair for this verdict. The test passes on the unmodified tree, as a pin should. To show it is not vacuous, I removed the gate for this one command (temporary `cmd.requiresEngine = false`) and the test failed; the line was then reverted (see mutation evidence below).

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-69.r1/logs/`.

| Step | Command | EXIT |
| --- | --- | --- |
| Gate wrapper, first try | `python3 …/qwen/feature_check.py <worktree>` (`gate-1.log`) | 1: it could not create its log folder (`PermissionError` on `<worktree>-feature-check`, a sibling outside the write-jail); no gate ran |
| fork-sources | `bash tests/fork-sources-gate.sh` (`gate-fork-sources-3.log`) | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` (`gate-complexity-3.log`) | 1 on base-tree items; **0 items in the changed test file** (grep count 0) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` (`gate-file-length-3.log`) | 1 on base-tree items; none in the changed file |

The two ratchet exits are red on the train (base-tree items, e.g. `WasmSandbox.cpp`, `TakeLane.cpp`), and the branch's only change is the test file, which has no listed item. `feature_check.py` would therefore apply its own rule and pass those two rows, but the wrapper itself could not run here (see first row).

Build and test:

| Step | Command | EXIT |
| --- | --- | --- |
| Build, first try | `cmake --build build --target SessionAudioRecordTest -j4` (`build-1.log`) | 2: ccache could not create temp files under `~/.local/share/zene-bunny-fix/dsh/…` (outside the write-jail) |
| Build, with ccache temp in the build tree | `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target SessionAudioRecordTest -j4` (`build-2.log`) | 0 |
| Build the family's other binary | `… --target SessionAudioRecorderTest -j4` (`build-3.log`) | 0 |
| Test on the unmodified product tree | `ctest -R SessionAudioRecord` (`test-2.log`, after the build in row 3) | 0: 2/2 passed. No separate RED run exists because no product fix was made; the mutation rows below are the RED proof |
| Mutation: gate removed (`requiresEngine = false`), rebuild | `… --target SessionAudioRecordTest -j4` (`build-4-mutant.log`) | 0 |
| Mutation test run | `./SessionAudioRecordTest aRecordingIsRefusedBeforeTheEngineIsAddressable` (`test-4-mutant.log`) | **1**: `FAIL! … refused.errorKind` actual `0` (success), expected `Busy` (`SessionAudioRecordTest.cpp:251`) |
| Mutation reverted, rebuild both targets | `… --target SessionAudioRecordTest SessionAudioRecorderTest -j4` (`build-5-revert.log`) | 0 (product file reverted; `git diff --stat` shows only the test file) |
| Family ctest (final) | `cd build/tests && ctest -R SessionAudioRecord --output-on-failure` (`test-5-green.log`) | 0: 2/2 executed, `SessionAudioRecorderTest` (#586) and `SessionAudioRecordTest` (#587) passed |
| New slot alone | `./SessionAudioRecordTest aRecordingIsRefusedBeforeTheEngineIsAddressable` (`test-6-green-newslot.log`) | 0: `3 passed, 0 failed` (init, new slot, cleanup) |

Note on the ctest run: the `SessionAudioRecorderTest` row showed `Not Run` in an earlier attempt because its binary had not been built; after building it, the family ran 2/2.

## Self-check

- **The regression test fails with the product change reverted and passes with it (exit codes).** No product change exists for this verdict, so there is no revert to make. The equivalent proof is the mutation: removing the gate turns the test RED (`test-4-mutant.log`, EXIT 1); restoring it returns GREEN (`test-5-green.log`, EXIT 0).
- **The test is deterministic.** It uses no clock, sleep or thread. It uses the public `ControlRegistry::invoke` with the injected readiness flag (`setReady(false)`) and restores it before asserting, so a failed assertion cannot leave the registry not ready.
- **The fix is at the cause and complete.** Not applicable: no fix. The guard is at the cause (the registry gate, `ControlRegistry.cpp:283`), and I checked every path into `armAndLaunch` (only `recordSlot`, `ControlCommandsSessionAudioRecord.cpp:298` registration and handler).
- **No collateral damage.** The product tree is unchanged. The test change is additive.
- **For FALSE-POSITIVE: the code (file:line) or intended contract.** `ControlRegistry.cpp:283` (gate), `:205-207` (readiness definition), `Engine.cpp:71-73` and `:112-113` (creation and teardown order), `ControlRegistry.h:104` (default `requiresEngine = true`), `ControlCommandsSessionAudioRecord.cpp:298` (no override).
- **Gate wrapper limitation.** `feature_check.py` and `submit_check.py` try to create their log folder at `<worktree>-feature-check` (a sibling of the worktree, outside the write-jail) and fail there. I ran the same six simple gates and two ratchets directly from the worktree; their results are in the table above.
