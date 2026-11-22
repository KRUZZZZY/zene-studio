Verdict: FALSE-POSITIVE

Cluster new-p21-42: "createChannelFor dereferences mixer and channel without checks" (`src/core/ControlCommandsPluginBuses.cpp:248-250`, finding line 248).

## Claim

Luna's finding says `Engine::mixer()` and `mixer->mixerChannel(index)` are used without null checks, and that a failed `createChannel()` (returning -1) makes `mixerChannel(-1)` invalid. Two premises: (a) `createChannel()` can return -1, (b) `Engine::mixer()` can be null when this runs. Neither holds on branch `bugs/qwen-new-p21-42` (base `639e1d23e`).

## Evidence

### (a) `Mixer::createChannel()` never returns -1 and never returns an index without a channel

- `src/core/Mixer.cpp:1062` `int Mixer::createChannel()`. Its only return is `return index;` (`src/core/Mixer.cpp` after `doneChangeInModel()`, end of the function).
- `src/core/Mixer.cpp:1073` `const int index = m_mixerChannels.size();` is taken **before** `src/core/Mixer.cpp:1075` `m_mixerChannels.push_back( new MixerChannel( index, this ) );`. So the returned index is the position of the channel just pushed: always `>= 1` (channel 0 is the master, which exists from construction, `src/core/Mixer.cpp:1030`), and never -1.
- `include/Mixer.h:634-637` `mixerChannel(int)` returns `m_mixerChannels[_ch]` for that index: a valid, non-null pointer.
- Therefore `channel` at `ControlCommandsPluginBuses.cpp:250` cannot be null, and the "-1 then `mixerChannel(-1)`" sequence cannot occur.

### (b) `Engine::mixer()` cannot be null when a command handler runs

- `include/Engine.h:66-69` `Engine::mixer()` returns `s_mixer`; `src/core/Engine.cpp:73` sets it in init; `src/core/Engine.cpp:112` nulls it only on destroy.
- `src/core/ControlRegistry.cpp:205-208` `isEngineReady()` is `s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr`.
- `include/ControlRegistry.h:104` `bool requiresEngine = true;` is the default, and `src/core/ControlRegistry.cpp:283` refuses any command with `requiresEngine && !isEngineReady()` before its handler runs. `plugin.output_buses_create_channels` (`ControlCommandsPluginBuses.cpp:324`) and `plugin.output_bus_route` (`ControlCommandsPluginBuses.cpp:214`) do not override it. `grep -rn "requiresEngine" src include` lists every override; none is in this file.
- The undo/redo redo lambda (`ControlCommandsPluginBuses.cpp:276-281`, which calls `createChannelFor` again) runs only via `control.undo` / `control.redo`, which keep `requiresEngine = true` (`src/core/ControlCommandsControl.cpp:512`, `:549`). So it also goes through the same gate. (Checked: `control.undo` and `control.redo` have no `requiresEngine = false` override; the `false` lines near them, `ControlCommandsControl.cpp:601`, belong to `control.quit`/`control.commands_list`/`control.transactions`.)

### The failure path that *does* exist is already handled

`createChannelFor` can return -1 for a real reason: `InstrumentOutputBuses::route` refuses the bus (`src/core/InstrumentOutputBuses.cpp:151-162`, false for an out-of-range bus or an inactive VST3 aux bus the host will not enable). In that case the function already removes the channel it just made and returns -1:

- `src/core/ControlCommandsPluginBuses.cpp:252-254`: `if (buses.route(bus, channel->id())) { return channel->id(); } mixer->deleteChannel(index); return -1;`
- The caller `createChannels` (`ControlCommandsPluginBuses.cpp:293-306`) turns -1 into a typed `ControlErrorKind::Refused` ("the plug-in did not activate output bus N") and rolls back earlier channels.

So the crash-instead-of-refusal described in the finding does not happen. The only way `createChannelFor` returns -1 is the handled route refusal.

## Not changed

No product code was changed. Adding null checks for `Engine::mixer()` or for `createChannel()` would be dead code: both are guaranteed by the code above. The regression-test step is therefore not applicable (there is no behaviour to fix); no test was added.

Observation, not part of this finding: on redo, `createChannelFor` may return -1 (route refused) and `recordCreation`'s redo lambda (`ControlCommandsPluginBuses.cpp:276-281`) silently keeps `channelId = -1` without reporting it. This is a redo-path behaviour question, not the reported crash, and it is out of scope here.

## Commands and results

Commands run in the worktree (`zene-qwen-new-p21-42`); logs in the LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-42.r1/logs`.

| Command | Log | EXIT |
| --- | --- | --- |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `feature-check-1.log` | 1 (script crashed: `PermissionError` creating `<worktree>-feature-check/` outside the write-jail, before any gate ran) |
| `bash tests/fork-sources-gate.sh` | `gate-fork-sources-1.log` | 0 |
| `bash tests/all-sources-reproduce.sh` | `gate-all-sources-1.log` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-1.log` | 0 |
| `tests/scripted/check-namespace` | `gate-check-namespace-1.log` | 0 |
| `bash tests/no-tautology-gate.sh` | `gate-no-tautology-1.log` | 0 |
| `python3 tests/doc-figures-gate.py` | `gate-doc-figures-1.log` | 0 |
| `bash tests/complexity-gate.sh --check` | `gate-complexity-1.log` | 1 (7 `REGRESSION` lines, all in other files, e.g. `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`; none in `ControlCommandsPluginBuses.cpp` or in this report) |
| `bash tests/file-length-gate.sh --check` | `gate-file-length-1.log` | 1 (22 `REGRESSION` lines, none in files this branch changed; this change touches only `docs/qwen-fixes-2026-10/cluster-new-p21-42.md`) |

`feature_check.py` could not run from inside the write-jail, so its gates were run one by one with the same commands it uses (`feature_check.py` `SIMPLE` and `RATCHET` lists). The two red ratchets are the train's known red on purpose; the feature check passes them only when a red item is in a file the branch changed, and this branch changes no product file.

## Self-check

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-42` → EXIT=1 (log `submit-check-1.log`). Result: PASS commits on the branch, PASS working tree clean, PASS report present, PASS "tests added or changed" (claim-only closure, see below); FAIL `gates (feature_check.py)` only; the four "?" items answered here.

- **Gates row (FAIL), an environment blocker, not a code failure.** `submit_check.py` runs `feature_check.py`, which crashes with `PermissionError` creating `<worktree>-feature-check/` in `projects/zene-studio-program/`, outside the write-jail (`feature-check-1.log`). The gates were run directly, each with exit 0 except the two ratchets (see table above). Neither ratchet lists an item in a file this branch changed: the branch's only change is the docs file `docs/qwen-fixes-2026-10/cluster-new-p21-42.md`, and `gate-complexity-1.log` / `gate-file-length-1.log` list items in other files only (grep of both logs for `ControlCommandsPluginBuses` returns nothing). The gate runner needs to be run from outside the jail (or with its log folder allowed) to turn this row green.
- **Regression test fails with the change reverted / passes with it.** Not applicable: no product change, so there is no fix to revert and no test. The verdict is FALSE-POSITIVE on the code evidence above.
- **Deterministic.** Not applicable (no test added).
- **Fix at cause and complete; callers and siblings.** No fix. The two callers of `createChannelFor` were checked: `createChannels` (`ControlCommandsPluginBuses.cpp:293`) and the undo-redo lambda (`ControlCommandsPluginBuses.cpp:280`). Both run behind the dispatch gate (`ControlRegistry.cpp:283`). `createChannels` checks the -1 result (`ControlCommandsPluginBuses.cpp:294-300`); the redo lambda stores it without a check (`:280`, the observation above). The `deleteChannelById` sibling (`:257-264`) does not dereference a null channel: it checks `mixer->mixerChannel(i)` against a valid index range.
- **No collateral damage.** No product code changed; saved projects and command behaviour are unchanged.
- **Verdict evidence.** `Mixer.cpp:1073,1075` (index is the pushed position); `Mixer.h:634-637` (valid pointer); `Engine.h:66-69` + `ControlRegistry.cpp:205-208,283` + `ControlRegistry.h:104` (mixer non-null on dispatch); `ControlCommandsControl.cpp:512,549` (undo/redo keep the gate); `InstrumentOutputBuses.cpp:151-162` + `ControlCommandsPluginBuses.cpp:252-254` (the real -1 path already cleans up).
