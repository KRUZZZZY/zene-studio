IMPLEMENTED

# Q050-066.2 - Drum-rack pad workflow, stage 2/4: visible pad editor and independent chains

Branch `feat/050-Q050-066.2-oc`, cut from `050/m0-train-21` (`37eaf07557`). Feature record:
`docs/050-features/Q050-066.2.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-066.2.r1/logs/`.

Commits: `4afb550326` (feature + tests), `672762d39d` (live snapshot), `da7ff9e862` (manual + feature record), and this
report.

**What changed.** `track.drum_rack_get_state` now reads back each pad's `chain` (the effects on the pad's own mixer
channel: id, plugin, display_name, enabled) and `chain_shared`, both through the shared `DrumPadState` contract. In the
pad panel, each row shows its chain and has an Effects menu (Add Effect > built-in effect, Bypass / Switch On, Remove)
that runs `plugin.load` / `plugin.bypass` / `plugin.unload` with `target` = the pad's channel, the same commands an agent
sends. Add Effect is disabled when the pad's chain is shared. There are no new commands and no format change.

## Acceptance

| # | Criterion | Test | Exit with the change | Exit reverted |
| --- | --- | --- | --- | --- |
| 1 | Create and edit pads from the GUI through the same commands agents use | `DrumRackPanelTest` (`thePanelEditsPadsThroughTheCommands` from stage 1 + new `thePanelEditsAPadsChainThroughTheCommands`, which triggers the row's Effects menu actions and reads the result through `track.drum_rack_get_state`) | `ctest -R ^DrumRackPanelTest$` 0 [test-7] | build 2: the panel has no `effectsButton`/`chainLabel` [build-revert-1]; ctest 8, Not Run [test-revert-2] (the stale binary from the earlier build was removed first, so the "Passed" in test-revert-1 is not counted) |
| 2 | Add an effect to one pad and prove it is independent of another pad's chain; verify state and undo/redo | `DrumRackPadsTest::{anEffectOnOnePadIsNotOnAnother, aSharedChannelIsNotAPadsOwnChain}`; `ControlDrumRackChains` (socket transcript + render: amplifier at 0 % on the kick's chain makes the rack render equal to the snare alone; negative control; undo/redo of the setting and of the effect, one step each; save/reopen in state and sound) | `ctest -R ^DrumRackPadsTest$` 0, `ctest -R ^ControlDrumRackChains$` 0 [test-7] | DrumRackPadsTest 8, both new cases FAIL [test-revert-1]; ControlDrumRackChains 8, 5 named checks FAILED, no traceback [test-revert-3] |

Commands (from `build/tests`, environment as in the brief):
`ctest -R "^(DrumRackPadsTest|DrumRackPanelTest|ControlDrumRackChains|ControlDrumRackPads|DeviceCopyTest|TrackFolderMenuTest)$" --output-on-failure` exit 0 [test-7];
`ctest -R "^(ReversibilityContractTest|ControlCommandsSnapshot)$"` exit 0 [test-8].

On the reverted transcript: its render checks pass on the base as well. Every pad already had a channel of its own
(stage 1 / `track.drum_rack`). What this unit adds, and the revert removes, is reading the chain back per pad and editing
it from the panel. The render checks prove that the independence the read-back reports is real in the sound.

## Self-check

- **Each new test fails reverted, passes restored.** `git apply -R` of the product diff (`include/`, `src/`), rebuild
  [build-revert-1 exit 2: DrumRackPanelTest does not compile; build-revert-2 exit 0 for zene + DrumRackPadsTest], then:
  DrumRackPadsTest exit 8 (`anEffectOnOnePadIsNotOnAnother`, `aSharedChannelIsNotAPadsOwnChain` FAIL) [test-revert-1];
  DrumRackPanelTest exit 8 Not Run [test-revert-2]; ControlDrumRackChains exit 8 (5 read-back checks FAILED)
  [test-revert-3]. Restored (`git apply`), rebuilt [build-6 exit 0]: all exit 0 [test-7].
- **Every criterion proven by a test I ran.** Criterion 1: DrumRackPanelTest exit 0 [test-7]. Criterion 2:
  DrumRackPadsTest exit 0, ControlDrumRackChains exit 0 [test-7] (all 16 transcript checks "ok", run verbosely in
  [test-6]).
- **One undo step per edit, honest classes.** Each panel action is one registered command and one step: the GUI test
  undoes the bypass, the remove, pad 2's add and the agent's unload one `control.undo` each, and checks the redos. The
  transcript undoes and redoes the param setting and the load one step each. No new commands. The only metadata change is
  `track.drum_rack_get_state`'s description (still `not_mutating`; it writes nothing). The `plugin.load` (snapshot),
  `plugin.bypass` (true_inverse) and `plugin.unload` (true_inverse) classes are unchanged. ReversibilityContractTest exit
  0 with no histogram change [test-8]. Known limit, found and documented and not changed here: a redo of `plugin.load`
  (or an undo of `plugin.unload`) makes a new device instance, so an older recorded edit on the old instance does not
  reach it across that step (`src/core/ControlStructuralSupport.cpp:769`).
- **Realtime.** The new code is all UI/control-thread: read-back walks the channel's effect vector and the song's tracks,
  and the panel builds menus and invokes commands. Nothing was added to any audio path. TSan is not available on this
  box; this is by reading.
- **Old projects and round trip.** No format change: a pad's chain is its channel's effect chain, which projects already
  save. ControlDrumRackPads (stage 1: an old-style rack saves, reopens and renders equal, and has no new XML element)
  exit 0 [test-7]. The new chain round-trips: DrumRackPadsTest reopen case (amplifier, bypassed, back on the kick only)
  and the transcript reopen checks (state + render) exit 0 [test-7].
- **Portability.** Qt 5.15-era API only (`QToolButton::setPopupMode`, `QMenu::addMenu`, `QJsonDocument::toJson(Compact)`,
  `QCoreApplication::sendPostedEvents`). No Qt-6-only call, no `std::filesystem`, no POSIX call, and no
  `min`/`max`/`small`/`near`/`far`/`interface` identifiers.
- **Gates.** `submit_check.py` reports "gates (feature_check.py)" FAIL because `feature_check.py` writes its logs to
  `<worktree>/../zene-feat-q050-066.2-feature-check`, outside this lane's write-jail (PermissionError, [gate-feature-1]).
  I ran an unmodified copy from `/tmp` with only that log path (and its own `ROOT`) changed to point into the run folder:
  all 8 rows PASS, RESULT PASS, exit 0 [gate-feature-2; per-gate logs in `logs/feature-check/`].
- **Canary WARN.** `submit_check` lists "new command ids" such as `bassdrum01.ogg` and `clip.add`. These are strings in
  the test transcript, not commands. The surface stays at 871 ids / 76 groups, and `doc-figures-gate.py` exits 0
  [gate-docfig-1].
- **Snapshot.** Regenerated live with `snapshot_live.py` (exit 0, 871 ids / 76 groups) [snapshot-1]; the diff is the
  `track.drum_rack_get_state` description plus the capture metadata.
