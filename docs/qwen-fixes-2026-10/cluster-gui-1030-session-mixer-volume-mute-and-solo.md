FALSE-POSITIVE

# Cluster gui-1030-session-mixer-volume-mute-and-solo: solo isolation with a second audible channel

Finding: `src/core/Mixer.cpp:1123` (train-24 sweep, "Solo isolation was not verified with a second audible channel").

## Verdict

The mixer's solo logic is correct, and no product code changes. The sweep could not check isolation because its run had only the master channel plus one instrument that was reported quarantined, so there was no second live track to compare against. That is a gap in the sweep's setup, not a defect in `Mixer.cpp`. The finding's expected behaviour (soloing a track silences the others, and a muted track's meter goes dark) is what the code does, and existing tests already assert it. This commit adds one test that walks the owner's exact path with two audible channels, so the behaviour is pinned.

## Evidence (file:line)

- **Master is index 0.** `Mixer::Mixer()` creates the master with `createChannel()` (`src/core/Mixer.cpp:1054-1055`). `activateSolo()` starts its loop at `i = 1` (`src/core/Mixer.cpp:1123`, `:1133`), so it never mutes the master. The finding's line is the first of the two loops, so it is not an off-by-one.
- **Solo mutes every other non-solo-safe channel and saves its pre-solo mute.** `Mixer::activateSolo()` (`src/core/Mixer.cpp:1121-1129`): `m_muteBeforeSolo` is saved (`:1125`), and each channel that is not solo-safe is muted (`:1127`).
- **The soloed channel is unmuted, and its linked channels too.** `Mixer::toggledSolo()` (`src/core/Mixer.cpp:1139-1189`) calls `activateSolo()` (`:1166`), then `unmuteForSolo()` on the soloed channel (`:1184`), or on every VCA member (`:1178`). `MixerChannel::unmuteForSolo()` is at `src/core/Mixer.cpp:544`.
- **Unsolo restores the saved mutes.** `Mixer::deactivateSolo()` (`src/core/Mixer.cpp:1131-1137`) writes back `m_muteBeforeSolo`.
- **A muted channel's meter goes dark on the audio thread.** `ch->m_muted` is copied from the mute model each period (`src/core/Mixer.cpp:2451`), and a muted channel is "processed" as silence (`src/core/Mixer.cpp:2456`). The mute path that feeds a channel's input also returns early (`src/core/Mixer.cpp:2054-2056`). This is read from the code, not observed on a GUI meter (see "Not verified here").
- **Existing engine-level coverage.** `tests/src/core/MixerStripTest.cpp:253-261` (`soloSafeAndSetMany`): soloing channel 1 mutes channel 2 and leaves solo-safe channel 3 audible. `:326-340` (`deletingTheSoloedChannelRestoresTheMutes`): the solo mutes channels 1 and 2, and deleting the soloed channel restores them.

## Change

- `tests/src/core/MixerStripTest.cpp:344` (`soloIsolatesTwoAudibleChannels`): exclusive mode; every non-master channel starts with solo and mute off; solo channel 1 and assert channel 1 audible and channel 2 muted; unsolo and assert both channels back to their pre-solo (audible) state. It resets its own state first, so it does not depend on the order of the other functions.
- No product file changed. `git diff -- src/core/Mixer.cpp` is 0 bytes.
- `tests/fork-sources.txt` already lists `MixerStripTest.cpp` (line 2535, fork-owned), so no registration change is needed.
- A first version of the test was about 20 code lines long and pushed the file to 508 code lines, which tripped the file-length ratchet. I compacted it (removed a no-assert mute toggle and a redundant cleanup) and the file is no longer flagged.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-session-mixer-volume-mute-and-solo.r1/logs`.

| # | Command (run from the worktree unless noted) | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target MixerStripTest MixerOrderByTracksTest MixerInsertLatencyTest MixerAbRegressionTest MixerModesTest -j4` | 2 | `build-1.log` (ccache could not write to the shared cache dir: "Permission denied") |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target ... -j4` (same targets) | 0 | `build-2.log` |
| 3 | `cd build/tests && ctest -R 'MixerStripTest\|MixerOrderByTracksTest\|MixerInsertLatencyTest\|MixerAbRegressionTest\|MixerModesTest' --output-on-failure` (before step 2) | 8 | `test-1.log` ("Not Run": binaries not built yet, so not evidence) |
| 4 | same ctest after step 2 | 0 | `test-2.log` (5/5 passed) |
| 5 | `cd build/tests && ./MixerStripTest soloSafeAndSetMany deletingTheSoloedChannelRestoresTheMutes -v1` | 1 | `solo-funcs-1.log` (see Observations) |
| 6 | `cd build/tests && ./MixerStripTest deletingTheSoloedChannelRestoresTheMutes -v1` | 0 | `solo-funcs-2.log` |
| 7 | `cd build/tests && ./MixerStripTest -v1` (full, before the new test) | 0 | `solo-full-1.log` (25 passed) |
| 8 | `CCACHE_DISABLE=1 cmake --build build --target MixerStripTest -j4` (with the new test) | 0 | `build-3.log` |
| 9 | `cd build/tests && ./MixerStripTest soloIsolatesTwoAudibleChannels -v1` | 0 | `solo-new-1.log` (passes) |
| 10 | `cd build/tests && ./MixerStripTest -v1` (full, 26 cases) | 0 | `solo-full-2.log` (26 passed) |
| 11 | Mutant A: `src/core/Mixer.cpp:1127` `m_muteModel.setValue( true )` -> `setValue( false )` (temporary); rebuild `MixerStripTest`; run `./MixerStripTest soloIsolatesTwoAudibleChannels -v1` | build 0 / run 1 | `build-red-mutant.log`, `solo-mutant-1.log` (`FAIL! ... channel(2)->m_muteModel.value() returned FALSE`). Test first version. |
| 12 | Restore `Mixer.cpp:1127` to `setValue( true )`; `git diff -- src/core/Mixer.cpp` | diff 0 bytes | `build-green-restored.log`, `solo-restored-1.log` (run EXIT 0) |
| 13 | Compacted test: `bash tests/file-length-gate.sh --check` | 1 (train-level red; `MixerStripTest.cpp` not listed) | `gate-file-length-3.log` |
| 14 | Mutant B (compacted test): same `Mixer.cpp:1127` mutation; build; run `./MixerStripTest soloIsolatesTwoAudibleChannels -v1` | build 0 / run 1 | `build-red-mutant-2.log`, `solo-mutant-2.log` (`FAIL! ... '!channel(1)->m_muteModel.value() && channel(2)->m_muteModel.value()' returned FALSE`) |
| 15 | Restore `Mixer.cpp:1127`; `git diff -- src/core/Mixer.cpp` | diff 0 bytes | — |
| 16 | `CCACHE_DISABLE=1 cmake --build build --target zene MixerStripTest MixerOrderByTracksTest MixerInsertLatencyTest MixerAbRegressionTest MixerModesTest -j4` | 0 | `build-6.log` |
| 17 | `cd build/tests && ctest -R '<same five>' --output-on-failure` | 0 | `test-4.log` (5/5 passed) |
| 18 | `cd build/tests && ./MixerStripTest -v1` | 0 | `solo-full-3.log` (26 passed) |
| 19 | Gates (run directly, see below) | see table | `gate-*-pre.log` |

Affected family: `MixerAbRegressionTest`, `MixerStripTest` (26 cases), `MixerOrderByTracksTest`, `MixerModesTest`, `MixerInsertLatencyTest`. 5 of 5 executed and passed (step 17).

### Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` could not run in this write-jail. It writes its logs to `<worktree>-feature-check`, beside the worktree, and failed with `PermissionError` (log `feature-check-1.log`, EXIT 1). I did not edit the script. Instead I ran the same gate commands it lists, from the worktree, with the same logs.

| Gate | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (7 REGRESSION lines, none names `MixerStripTest.cpp`; the same 7 before my change) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (26 REGRESSION lines, none names `MixerStripTest.cpp`; the same 26 before my change) |

The two ratchets are red on the train on purpose. Their baseline comparison is in `feature_check.py`, using a cache outside the write-jail, so I could not run it. I compared the raw REGRESSION lines with the only file I changed instead. No item is in `MixerStripTest.cpp`, and the count is unchanged from HEAD.

## Build environment

ccache could not write its temp files in the shared cache directory (`build-1.log`, "Permission denied"). I rebuilt with `CCACHE_DISABLE=1`. Build flags and sources are unchanged.

## Not verified here

- A live GUI meter check (offscreen, no audio device). The "meter goes dark" claim is supported by the code at `src/core/Mixer.cpp:2054-2056` and `:2451-2456`, not by an observed meter.
- The sweep's own run (single quarantined instrument) is not re-run here. The engine-level isolation is covered by the tests above.

## Observations (not changed)

- `MixerStripTest::deletingTheSoloedChannelRestoresTheMutes` (`tests/src/core/MixerStripTest.cpp:326`) assumes no solo is active when it starts. Running it after `soloSafeAndSetMany` alone fails (step 5, `solo-funcs-1.log`), but it passes alone (step 6) and in the full ordered run (step 7). This is an existing order dependency, not caused by this change, and it is out of scope. My new test resets its own state, so it does not add to the dependency.

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it.** Not applicable as a bug regression: there is no product change, and the test passes on the unmodified product, which is the intended behaviour. To show it is not vacuous, I broke the behaviour it pins (`Mixer.cpp:1127`, solo no longer mutes others) and it fails (steps 11 and 14, EXIT 1, assertion named). With the line restored it passes (steps 12 and 17, EXIT 0).
- **Deterministic: no wall-clock timing, sleeps or races.** The test uses only model `setValue` calls and reads. It has no timing, sleep, or thread. It resets all non-master channels to solo off and mute off first, so it does not depend on other functions' state.
- **The fix is at the cause and complete.** No fix was made. I read every path into solo state: `toggledSolo` (`Mixer.cpp:1139`), `applyAdditiveSolo` (`src/core/MixerStrip.cpp:443`), `deleteChannel` (`Mixer.cpp:1452-1462`), and `unmuteForSolo` (`Mixer.cpp:544`). They are consistent with the behaviour the test pins.
- **No collateral damage.** The only file changed is `tests/src/core/MixerStripTest.cpp` (one new function). Saved projects and their mute and solo flags are not touched. The file-length gate does not flag it.
- **ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or intended contract.** `src/core/Mixer.cpp:1121-1129` (`activateSolo`), `:1139-1189` (`toggledSolo`), `:1131-1137` (`deactivateSolo`), `:2054-2056` and `:2451-2456` (audio-thread mute), and `tests/src/core/MixerStripTest.cpp:253-261` and `:326-340` (existing solo tests). Verdict: FALSE-POSITIVE.
- **Gates (feature_check.py) - blocked by the sandbox, not by code.** `feature_check.py` and `submit_check.py` create `<worktree>-feature-check` beside the worktree, and `mkdir` there returns `Permission denied` (this write-jail; `submit_check` therefore shows its gate item as FAIL for that reason only). The same eight gate commands were run directly with EXIT codes in the Gates section: six pass, and the two ratchets show the unchanged train-level 7 and 26 lines, none in `MixerStripTest.cpp`. The baseline-comparison step of the ratchets needs the cache outside the jail and was not run.
