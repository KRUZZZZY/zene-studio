FALSE-POSITIVE

# Cluster new-p21-128: resetMasterForEmptyMixerModel dereferences mixer->mixerChannel(0)

Finding: `src/core/DawProjectSession.cpp:215` (`MixerChannel* master = mixer->mixerChannel(0);`, the line matches the current tree) can null-dereference when the mixer has zero channels and the imported model has an empty `mixerChannels` list.

Verdict: FALSE-POSITIVE. A Mixer always holds at least one channel, the master at index 0, and every code path in this tree that removes a channel keeps index 0. The state the finding needs ("a mixer with zero channels") cannot be reached, so `mixerChannel(0)` at `DawProjectSession.cpp:215` always has a valid channel. No product code is changed, so there is no regression test. This is a claim-only closure: an independent QA should verify the cited code and contract below.

## Evidence

**1. The master is created by construction.** `Mixer::Mixer()` calls `createChannel()` with the comment "create master channel" ([src/core/Mixer.cpp:1046](src/core/Mixer.cpp:1046), [1052](src/core/Mixer.cpp:1052), [1053](src/core/Mixer.cpp:1053)). `createChannel()` gives the new channel `index = size()` before the push ([Mixer.cpp:1098](src/core/Mixer.cpp:1098)), so the first channel is index 0.

**2. Clearing keeps the master.** `Mixer::clear()` deletes only while `size() > 1` ([Mixer.cpp:2573](src/core/Mixer.cpp:2573), [2575](src/core/Mixer.cpp:2575)), always calling `deleteChannel(1)`. The destructor ([Mixer.cpp:1066-1082](src/core/Mixer.cpp:1066)) runs only at teardown, after which no project is applied.

**3. The import path never empties the mixer.** `applyDawProjectModel` creates channels up to the model's count ([src/core/DawProjectSession.cpp:499-509](src/core/DawProjectSession.cpp:499)) and calls `resetMasterForEmptyMixerModel` ([DawProjectSession.cpp:510](src/core/DawProjectSession.cpp:510)). Then `removeMixerChannelsPast(mixer, qMax(1, ...))` ([DawProjectSession.cpp:511](src/core/DawProjectSession.cpp:511)) keeps at least one channel. The intent is written at [DawProjectSession.cpp:237-238](src/core/DawProjectSession.cpp:237): "Never below one: the master is index 0."

**4. `Mixer::deleteChannel(int)` has no index-0 check of its own** ([Mixer.cpp:1361](src/core/Mixer.cpp:1361)). It is safe only because every caller keeps index 0:

| Caller | Guard |
| --- | --- |
| `mixer.remove_channel` ([ControlCommandsMixer.cpp:331-334](src/core/ControlCommandsMixer.cpp:331)) | refuses index 0 ("the master channel ch-0 cannot be removed") |
| `bus.remove` (`ControlCommandsBus.cpp` `handleBusRemove`) | refuses any channel without the bus flag; the master is never a bus (below) |
| `MixerView::deleteChannel` ([MixerView.cpp:603](src/gui/MixerView.cpp:603)) | `index == 0` returns |
| `TrackFolder` ([TrackFolder.cpp:139](src/tracks/TrackFolder.cpp:139), [395](src/tracks/TrackFolder.cpp:395)) | `m_mixerChannel > 0` |
| Plugin-bus delete by id ([ControlCommandsPluginBuses.cpp:260](src/core/ControlCommandsPluginBuses.cpp:260)) | loop `i > 0` |
| Mixer-tools return removal ([ControlCommandsMixerTools.cpp:186](src/core/ControlCommandsMixerTools.cpp:186)) | loop from `i = 1` |
| Transplant channel lookup and delete ([ControlProjectTransplantChannels.cpp:119](src/core/ControlProjectTransplantChannels.cpp:119), [135](src/core/ControlProjectTransplantChannels.cpp:135), [146](src/core/ControlProjectTransplantChannels.cpp:146)) | `i = 1` / `index > 0` |
| Drum-rack undo ([ControlCommandsDrumRack.cpp:117](src/core/ControlCommandsDrumRack.cpp:117)) | deletes only channels from `createChannel()`, index ≥ 1 |
| DawProject import undo ([ControlCommandsDawProject.cpp:202](src/core/ControlCommandsDawProject.cpp:202), [245](src/core/ControlCommandsDawProject.cpp:245)) | the snapshot captures from index 0, so its size is ≥ 1, and the loop stops at that size |
| Undo of a just-created channel ([ControlCommandsMixer.cpp:281](src/core/ControlCommandsMixer.cpp:281), `ControlCommandsBus.cpp:110`, [ScriptDawEdit.cpp:204](src/core/ScriptDawEdit.cpp:204)) | deletes `numChannels() - 1` after a create, so index ≥ 1 |
| Mixer-view trim loop ([MixerView.cpp:800](src/gui/MixerView.cpp:800)) | `i > 0` |

**5. The master never gets the bus flag, so `bus.remove` cannot reach it.** The bus flag is set only on created channels: [ControlCommandsMixerTools.cpp:159](src/core/ControlCommandsMixerTools.cpp:159), [Mixer.cpp:1706](src/core/Mixer.cpp:1706), and [ControlProjectTransplantChannels.cpp:106](src/core/ControlProjectTransplantChannels.cpp:106) (applied at [126](src/core/ControlProjectTransplantChannels.cpp:126) to a new index, or to an existing one from [119](src/core/ControlProjectTransplantChannels.cpp:119) which starts at 1). The project loader skips `num <= 0` ([Mixer.cpp:2885](src/core/Mixer.cpp:2885), comment "Zero is the master, which cannot be a bus").

**6. Sibling risk, not reachable today.** `Mixer::deleteChannel` has no internal index-0 guard. A future caller that passes 0 on a one-channel mixer would produce the state in the finding. That is a hardening question for `Mixer::deleteChannel`, not a defect in `resetMasterForEmptyMixerModel`. It is out of scope here and was not changed.

The finding's suggested fix ("create the master channel first if needed") is also unnecessary, because the constructor creates the master before any project code can run.

## Commands run

| Step | Command | Result |
| --- | --- | --- |
| Invariant audit | `rg -n "<pattern set>" src/core/DawProjectSession.cpp src/core/Mixer.cpp ...ControlCommands*... src/tracks/TrackFolder.cpp src/gui/MixerView.cpp > logs/001-invariant-audit.log 2>&1; echo EXIT=$?` | EXIT=0, 23 matches, saved as `logs/001-invariant-audit.log` |
| Feature gate, script as written | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/002-feature-check.log 2>&1; echo EXIT=$?` | EXIT=1 before any check ran: `PermissionError` creating the sibling directory `zene-qwen-new-p21-128-feature-check`, which is outside the write-jail. The script was not edited. |

Because the script could not run, I ran each gate command it lists directly in the worktree, from `bash -c` so the commands were not word-split by the shell. Logs are in the LOG FOLDER, `logs/011` to `logs/017`, and `logs/006`:

| Gate (`feature_check.py` SIMPLE/RATCHET) | Command | Result |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | EXIT=0 (`logs/011`) |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | EXIT=0 (`logs/012`) |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | EXIT=0 (`logs/013`) |
| check-namespace | `tests/scripted/check-namespace` | EXIT=0 (`logs/006`) |
| no-tautology | `bash tests/no-tautology-gate.sh` | EXIT=0 (`logs/014`) |
| doc-figures | `python3 tests/doc-figures-gate.py` | EXIT=0 (`logs/015`) |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | EXIT=1 (`logs/016`): `FAIL: complexity ratchet regressed`; the REGRESSION items are in `src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsSessionRecordLand.cpp`. None is a file this commit changes. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | EXIT=1 (`logs/017`): REGRESSION items are in `plugins/Vst3Effect/Vst3Host.cpp`, `src/core/TakeLane.cpp`, `src/wasm/WasmSandbox.cpp`, and several test files. None is a file this commit changes. |

The ratchets are red on the train on purpose. `feature_check.py` only fails on items in files the commit changes, and this commit changes only a doc. My inference that the script would therefore have passed is not a run: its baseline comparison needs a cache outside the write-jail, which I did not touch.

## Self-check

- **Regression test FAILS without the fix and PASSES with it:** not applicable. The verdict is FALSE-POSITIVE and no product code changed, so there is no fix to revert. Procedure step 2 skips the regression test for a not-real verdict.
- **Test is deterministic:** not applicable. No test was added or changed.
- **Fix at the cause and complete:** not applicable. No fix was made. All callers and siblings of `mixerChannel(0)` and `deleteChannel` were checked (table above).
- **No collateral damage:** no product code changed, so no behaviour changed. Old projects and saved data are unaffected.
- **Evidence for FALSE-POSITIVE:** cited above at file:line, plus the intended contract ("Never below one: the master is index 0", `DawProjectSession.cpp:237-238`; "create master channel", `Mixer.cpp:1052`).
- **Audio-thread constraints:** no code changed.
- **Qt 5.15 / MSVC / macOS portability:** no code changed.
- **Commands snapshot and canary:** no command added or changed; the snapshot is not touched.
- **Scope:** the only file in this commit is this report.
- **Gate result:** `feature_check.py` as written could not run in this sandbox: it creates a log directory beside the worktree, which the write-jail blocks (`logs/002`, EXIT=1). The six simple gates each exit 0 when run directly. The two ratchets exit 1 in raw check mode, with failures in files this commit does not change. Full table in "Commands run" above.
- **Completion check (`submit_check.py`):** run after this commit; its output is in the LOG FOLDER (`logs/018`).
