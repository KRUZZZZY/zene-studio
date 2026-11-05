IMPLEMENTED

Integrated onto `050/m0-train-25` (`081630835b`): every acceptance criterion is met and proven on the merged tree
(built and tested there; commands and exit codes in "Round 7" below).

# Q050-129 — Follow arranger selection in the mixer

The mixer's **Selected** filter now follows the focused arranger track (or the
track that owns a focused clip) while **Follow Selection** is on, and the
preference is the user-level `ui/followchannel` state the shell already read.
The manual Selected set is never overwritten, so turning following off restores
it unchanged. Projects gain no new saved data.

## Round 7 (integration onto `050/m0-train-25`)

The accepted branch (`feat/050-Q050-129-oc`) was merged into the train in this integration worktree. The three-way merge
conflicted in three files:

| File | Resolution |
|---|---|
| `src/gui/MixerViewOptions.cpp` | Include block only: the train added `ConsoleStripSection.h` and `ControlMeterSupport.h`, this unit adds `ShellSelection.h`. Kept all three. No semantic difference from the accepted patch. |
| `tests/upstream-modifications.txt` | Took the train's file and re-added this unit's two lines (`tests/control-mixing-routing.py`, `tests/src/gui/MixerMeteringTest.cpp`). |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | Derived file: took the train's, then regenerated from a live instance with `snapshot_live.py` (**exit 0**, 917 commands / 78 groups, equal to the train; the diff to the train is `window.mixer_view`'s `follow_selection` / `visible_channels` schema and description plus the capture header). |

No product-code change was needed beyond the include. The merged tree exposed a test-fixture fault in this unit's own
tests, fixed in `tests/src/gui/MixerMeteringTest.cpp` only:

- **The shortcut test failed after this unit's tests** (`speakMeterReportsTheFocusedChannelAndMasterLufs`,
  `MixerMeteringTest.cpp`: `triggered.count()` 0, exit 8 in `test-1.log`/`test-2.log`; it passed alone). Two causes, found by
  bisecting the test order: (1) both new tests left the view at `filter=selected` with a manual set, so the strip
  that test focuses was hidden - they now call `restoreViewDefaults()` (all channels, nothing picked, follow off) last;
  (2) a full `QCoreApplication::processEvents()` turn after `Track::create` ran window activation, which that test then
  never gets back (`QApplication::activeWindow()` was null). The fixture now calls `buildTrackViews()`, which delivers
  only the queued calls (`sendPostedEvents(nullptr, QEvent::MetaCall)`) the arranger needs to build its track views.
- `theFollowedFilterSurvivesEmptyAndStaleSelections` read `m_other` from the previous test and failed when run alone; it
  now creates its own manually picked channel.

- **file-length gate:** the one-line Mixer View menu entry took `src/gui/shell/ShellCommandMenus.cpp` from 500 to 501
  code lines (`feature-check/file-length.log`: "new file over 500 lines"). The six "Show ..." toggle entries in
  `viewOptionsMenu` are now one table and a loop (same labels, same command keys, same order), which brings it back to 500.

The assertions are unchanged and none was weakened or removed.

| Step | Command (logs in `runs/Q050-129.r7/logs`) | Exit |
|---|---|---|
| Build | `cmake --build build --target zene MixerMeteringTest MixerModesTest ReversibilityContractTest -- -j4` (`build-12.log`) | 0 |
| Focused tests | `ctest -R '^(MixerMeteringTest\|ControlMixingRouting\|ControlCommandsSnapshot\|ReversibilityContractTest\|MixerModesTest)$' --output-on-failure` from `build/tests` (`test-8-final.log`), 5/5 | 0 |
| Stability | `ctest -R '^MixerMeteringTest$' --repeat until-fail:5` (`test-4.log`) | 0 |
| Each new test alone | `./MixerMeteringTest followSelectionDefaultsOff` / `theSelectedFilterFollowsTheArrangerSelection` / `theFollowedFilterSurvivesEmptyAndStaleSelections`, each 3 passed | 0 |
| `ShellSelection` connection removed from `MixerView.cpp` | `ctest -R '^MixerMeteringTest$'` (`test-6-noconnect.log`): both follow tests fail at their first post-selection visibility assertion | 8 |
| Follow branch removed from `shows()` | `ctest -R '^(MixerMeteringTest\|ControlMixingRouting)$'` (`test-7-nofollow.log`): both fail | 8 |
| Canary | `python3 tests/doc-figures-gate.py` (`figures-1.log`), 917 ids / 78 groups | 0 |

Both reverted sources were restored and rebuilt before the final run. `ccache` needed
`CCACHE_TEMPDIR=/tmp/ccache-tmp-r7`, because the configured temporary directory is outside this session's write-jail.
The figures quoted from rounds 5 and 6 below (833 commands / 76 groups) are historical; the train is now 917 / 78.
Qt 5.15, MSVC and macOS builds were not run locally (no such toolchain here); the diff uses only API both Qt versions have.

## Round 6 (review: the GUI test bypassed the selection refresh; brief: merge the train)

**Merge NOT done - refused by the lane's git guard.** `git merge 050/m0-train-24` exited with "bunny-guard: 'git merge
050/m0-train-24' is not allowed in a Bunny lane" (`~/.local/share/zene-bunny-fix/bunny/shims/git:7`), and the brief's own
Commits rule also says never to merge. I did not call the real git binary or build a merge commit from plumbing, which
would go around the guard. `freshen()` (`qwen/qwenfix.py:1094`) needs an actor allowed to merge. A read-only
`git merge-tree --write-tree HEAD 050/m0-train-24` shows exactly two conflicted files, every other file auto-merging:

| File | Conflict | Resolution |
|---|---|---|
| `src/gui/MixerViewOptions.cpp` | Include block: this branch adds `ShellSelection.h`, the train adds `ConsoleStripSection.h` and `ControlMeterSupport.h`. | Keep all three, sorted. |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | Capture header (`captured_at`, `source_socket`, `version`) and `window.mixer_view`'s description. | Derived file: take the train's, then regenerate from a live instance with `snapshot_live.py`. |

Unverified: the merged tree has not been compiled or tested.

**The review's finding is fixed.** `MixerMeteringTest` no longer calls `syncMixer()` between a selection and its
assertions on the strips. `deliverQueuedRefresh()` runs `QCoreApplication::sendPostedEvents(mixerView, QEvent::MetaCall)`:
only what is queued for the view, with no event-loop turn, so the 500 ms sync timer cannot fire and mask a missing
connection. Each selection also asserts the strip has NOT moved before delivery. The fixture removes tracks through
`TrackContainerView::deleteTrackView` (view first, while the model is alive), so the deferred `~InstrumentTrackView`
crash the reviewer hit has nothing to fire on. New test `theFollowedFilterSurvivesEmptyAndStaleSelections` covers no
selection, an unmapped track, a track with no channel, rapid selections, and removing the selected clip's owner or a track
with its refresh still queued. `control-mixing-routing.py` now checks that refused channels (unknown, master) change
nothing. No product code changed in this round.

Commands and exit codes (logs in `runs/Q050-129.r6/logs`):

| Step | Command | Exit |
|---|---|---|
| Build | `cmake --build build --target zene MixerMeteringTest ReversibilityContractTest MixerModesTest -- -j4` (`build-6-restored.log`) | 0 |
| Focused tests | `ctest -R '^(MixerMeteringTest\|ControlMixingRouting\|ControlCommandsSnapshot\|ReversibilityContractTest\|MixerModesTest)$'` from `build/tests` (`test-5-restored.log`), 5/5 | 0 |
| Stability | `ctest -R '^MixerMeteringTest$' --repeat until-fail:10` (`test-6-repeat.log`) | 0 |
| Connection removed from `MixerView.cpp` | `ctest -R '^MixerMeteringTest$'` (`test-3-noconnect.log`): fails at the first post-selection visibility assertion | 8 |
| Follow branch removed from `shows()` | `ctest -R '^(MixerMeteringTest\|ControlMixingRouting)$'` (`test-4-nofollow.log`): both fail | 8 |
| Canary | `python3 tests/doc-figures-gate.py` (`figures-1.log`) | 0 |
| Gate table | `feature_check.py` through a `/tmp` copy with only its log path and `ROOT` changed, because its log directory is outside the write-jail (`fc-2.log`); all 8 rows PASS | 0 |

The commands below in the round 5 tables were not all rerun in round 6; the table above is the current evidence for
the five focused tests. `ccache` needed `CCACHE_TEMPDIR=/tmp/ccache-tmp-q129`, because its inherited temp directory is
outside this session's write-jail.

## Scope (round 5)

Round 4's review sent this branch back because it also carried five other
queue units (Q050-341 master crossfader, Q050-302 Inspector focus strip,
Q050-297 batched VCA assignment, Q050-338 persistent fader/pan gang, Q050-301
return-bus sorting) and the command-source refactor only they needed. The
tree is now narrowed to Q050-129 alone. The earlier commits stay in the log
(this lane may not rewrite history), and one commit on top returns every file
those units touched to the base `2da2ed356` and deletes the files they added;
the result is that `git diff 2da2ed356 HEAD --stat` lists only these files:

`data/manual/index.html`, `docs/050-features/Q050-129.md`, this report,
`include/MixerModeBar.h`, `include/MixerViewOptions.h`,
`src/gui/ControlCommandsMixerView.cpp`, `src/gui/MixerModeBar.cpp`,
`src/gui/MixerView.cpp`, `src/gui/MixerViewOptions.cpp`,
`src/gui/shell/ShellCommandMenus.cpp`, `tests/control-mixing-routing.py`,
`tests/src/gui/MixerMeteringTest.cpp`, and the command snapshot
(`tools/mcp-zene-control/zene_control/commands_snapshot.json`).

The gang-bookkeeping realtime fix from round 3 (`Mixer.cpp` / `MixerGangTest`)
was only needed because Q050-338's gang was on this branch; with the gang gone
no Mixer code is touched, so nothing in this diff runs on the audio thread.
No new source or test file is added, so `tests/fork-sources.txt`,
`tests/all-sources.txt` and `tests/CMakeLists.txt` equal the base.

## Acceptance

| Criterion | Test (command and exit code I saw) | What it proves |
|---|---|---|
| The follow-channel preference is user-visible, saved, and reflected by `ui/followchannel` state. | `ctest -R '^(ControlMixingRouting\|ControlCommandsSnapshot\|MixerMeteringTest)$' --timeout 300` from `build/tests` — **exit 0**, 3/3 (`test-2.log`); `MixerMeteringTest` methods `followSelectionDefaultsOff`, `theSelectedFilterFollowsTheArrangerSelection`. | An absent key reads off. Clicking the mode bar's **Follow Selection** button goes through `window.mixer_view`, writes `ui/followchannel=1`, and the command answers `follow_selection: true`; a second click writes `0`. |
| With Selected filtering enabled, changing arranger selection updates the visible mixer strips; disabling follow preserves manual mixer selection. | Same `MixerMeteringTest` run — **exit 0**. | With manual set `[other]`, focusing track A shows A's channel (master stays visible), a clip on track B shows B's channel, then turning follow off shows `[other]` again and a later arranger selection does not move it. |
| A headless agent-surface test verifies setting, read-back, and selection/filter behavior. | `ctest -R '^ControlMixingRouting$'` — **exit 0** (same run). | The real `zene` binary over the control socket sets `follow_selection`, reads it back with the manual set, selects a track and then a clip, reads `visible_channels` after each, turns follow off and reads the restored manual set. |

## Fail-without-change proof

See "Revert proof" below.

## Commands, metadata and gates

| Check | Command and exit code |
|---|---|
| Build | `cmake --build build --target zene MixerMeteringTest MixerModesTest ReversibilityContractTest ControlVcaCommandsTest VcaStripViewTest MixerAbRegressionTest -- -j4` — **exit 0** (`build-1.log`). |
| Command snapshot | `python3 research/reviews/m1-session-2026-10-12/scratch/m0/snapshot_live.py $PWD` — **exit 0**, 833 commands / 76 groups (equal to the base; no command was added). The diff to the base snapshot is `window.mixer_view`'s `follow_selection` / `visible_channels` schema, its description, and the capture header. `ctest -R '^ControlCommandsSnapshot$'` — **exit 0**. |
| Canary | `python3 tests/doc-figures-gate.py` — **exit 0**, `833 ids / 76 groups` (README/release-note figures equal the base). |
| Other mixer tests | `ctest -R '^(MixerAbRegressionTest\|ReversibilityContractTest\|ControlVcaCommandsTest\|MixerModesTest)$'` — all four **Passed** in the first run of `test-1.log`; `VcaStripViewTest` passed alone, exit 0 (`test-3-vca.log`) after hanging once inside the full run (see below). |
| Gate table | `feature_check.py` through a copy whose log directory is under the run folder (see Self-check) — **exit 0**, all 8 rows PASS on the 13-file diff, including `no-upstream-regression`, complexity and file-length with no new item (`fc-2.log`). `mixerView` was kept under the complexity target by the `applyFollowSelection` helper. |
| Final build and tests (after the helper) | `cmake --build build --target zene MixerMeteringTest -- -j4` — **exit 0**; `ctest -R '^(MixerMeteringTest\|ControlMixingRouting\|ControlCommandsSnapshot\|ReversibilityContractTest\|MixerModesTest)$' --timeout 300` from `build/tests` — **exit 0**, 5/5 (`test-6.log`). |

## Self-check

- **? Each new test FAILS with the product change reverted, then PASSES restored.**
  Round 6: exit 8 without the `ShellSelection` connection, exit 8 without the `shows()` follow branch,
  exit 0 restored (table in "Round 6"). See also "Revert proof" below.
- **? Every acceptance criterion is proven by a test I ran.** The table above names the
  command and exit code for each criterion: `MixerMeteringTest` exit 0 (criteria 1 and 2),
  `ControlMixingRouting` exit 0 (criterion 3).
- **? Each edit is ONE undo step, and every new or changed command has an honest
  reversibility class.** The only changed command is `window.mixer_view`; the preference is
  interface state, journals nothing and keeps its existing `not_mutating` A16 row
  (`ControlReversibilityTableMixing.cpp`). `ReversibilityContractTest` passes (exit 0); the
  A16 histogram is unchanged from the base.
- **? Nothing added runs on the audio thread.** The diff touches only GUI code:
  `MixerViewOptions::shows/followSelection/followedChannel`, the mode bar,
  `window.mixer_view`, `MixerView::syncWithMixer` and a menu entry. `ShellSelection` reaches
  the mixer view on the GUI thread; no `Mixer`, `AudioEngine` or model code is changed.
- **? Old projects still load and render exactly as before; new data round-trips.** The
  preference lives in `ConfigManager` (`ui/followchannel`, off when absent); no project
  save/load field was added, so project files and renders are unchanged.
- **? Qt 5.15 / MSVC / macOS < 10.15 portability.** Only `QPushButton`, `QJsonObject/Array`,
  `QSignalBlocker` and `ConfigManager` are used; no Qt-6-only API, no `std::filesystem`, no
  POSIX call, and no `small`/`near`/`far`/`interface`/`min`/`max` identifier. The hosted
  jobs were not available locally.
- **Realtime/races.** Local TSan does not run here. By design everything added runs on the GUI thread:
  `ShellSelection::changed` reaches `syncWithMixer` by a queued connection.
- **Gate note.** `feature_check.py` writes its logs to a sibling directory outside this
  sandbox's write jail (`PermissionError`), so I ran a copy with only that log path moved
  into the run folder; the gate commands are unchanged. `submit_check.py` calls the original
  and therefore reports the `gates` row as FAIL for the same reason.
- **Adjacent flake.** `VcaStripViewTest` (Q050-297's test; its code is the base's here)
  hung once in the combined ctest run and passed alone. It touches no Q050-129 file.

## Revert proof

I removed the follow branch of `MixerViewOptions::shows()` (the line
`if (followSelection()) { return followedChannel() == index; }`), rebuilt
`zene MixerMeteringTest` (**exit 0**, `build-2-reverted.log`) and ran
`ctest -R '^(MixerMeteringTest|ControlMixingRouting)$'`: **exit 8**, both tests
**Failed** (`test-4-reverted.log`). I restored the line, rebuilt (**exit 0**,
`build-3-restored.log`) and the same tests, plus `ControlCommandsSnapshot` and
`ReversibilityContractTest`, **Passed**, **exit 0** (`test-5-restored.log`).
