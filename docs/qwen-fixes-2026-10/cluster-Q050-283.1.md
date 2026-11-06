IMPLEMENTED

# Q050-283.1 - neutral arrangement tracks, stage 1 of 3 (integrated on train 25, `081630835b`)

Integration round (r2): the accepted patch (`5ff39d0efc`) was three-way merged onto `050/m0-train-25`. Full detail:
`docs/050-features/Q050-283.1.md`. Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-283.1.r2/logs`.

## Integration

- Conflicts: `src/core/ControlCommandsArrangement.cpp` (the `track.add` description: kept both the `neutral` wording and the
  train's `output`/`instrument` wording; the schema enum and `trackTypeForName` merged cleanly), `tests/CMakeLists.txt`
  (both `ControlTransientRepeatTranscript` and `ControlHybridTrackTranscript` registered and in the `set_tests_properties`
  list), `tests/fork-sources.txt` (both sides kept), `commands_snapshot.json` (regenerated live, see below).
- Semantic difference found and covered: the train's `track.add` now takes `output`/`instrument` and honours
  `app/autotrackchannel` (`ControlTrackRouting.cpp`, `hasChannelModel`). A neutral row has no mixer channel, so `output` and
  `instrument` are `InvalidArgs` refusals (no row, no undo step) and the preference gives it no channel. New test
  `HybridTrackRoundTripTest::aNeutralRowTakesNoOutputInstrumentOrAutoChannel` pins that; no product change was needed.
- No other `Track::Type` switch in the train lacks a `Neutral` name (the name table is `static_assert`-guarded).

## Acceptance

| Criterion | Test | Command | Exit |
|---|---|---|---|
| GUI and a registered command create a neutral row with stable id and position | `HybridTrackGuiTest` (real button click, 4 cases), `HybridTrackRoundTripTest` (10 cases), `ControlHybridTrackTranscript` | `ctest -R "^(HybridTrackRoundTripTest\|HybridTrackGuiTest\|ControlHybridTrackTranscript)$"` from `build/tests` (`test-2.log`) | 0 (3/3) |
| A neutral row round-trips save/load; typed tracks still load | `aNeutralRowRoundTripsThroughSaveAndLoad`, `anOldProjectLoadsAndSavesNoNeutralMark`, `HybridTrackGuiTest::theRowIsDrawnAgainAfterSaveAndLoad`, transcript section D | same run | 0 |
| Each new test fails with the product change reverted | `track.add` `neutral` mapping removed -> all 3 fail (`test-neg-1.log`); toolbar `addAction` removed + `hasChannelModel` widened to Neutral -> `HybridTrackGuiTest` (4 cases) and the new integration case fail (`test-neg-2.log`) | `ctest -R ...` after rebuild | 8 / 8 (expected); restored, rebuilt (`build-6.log` EXIT 0), then 0 |
| Neighbours (fresh binaries) | ReversibilityContract/Undo, ScriptBindings, StableTrackIds, TrackFolder, UnclaimedElements, DawProjectInterchangeRoundTrip, AutomationLaneChild{,Gui}, ControlCommandsSnapshot | `ctest -R ...` (`test-4.log`, built by `build-9.log` EXIT 0) | 0 (10/10) |
| SessionAudioRecordTest (touches `ControlCommandsSessionRecordLand.cpp`) | same | `test-5.log` (`build-10.log` EXIT 0) | 0 (1/1) |

Build: `cmake --build build --target zene HybridTrackRoundTripTest HybridTrackGuiTest -- -j4` EXIT=0 (`build-3.log`, `build-6.log`).
Snapshot regenerated live: `snapshot_live.py` exit 0 (917 ids / 78 groups, same as the train); `python3 tests/doc-figures-gate.py` exit 0 (`docfig-1.log`).
Environment note: the box's ccache `temporary_dir` is outside the write area, so builds used `CCACHE_TEMPDIR=/tmp/q283-ccache-tmp` (`build-2.log` failed on it, EXIT=2, then passed in `build-3.log`).

## Gates

`feature_check.py` cannot run in this sandbox (`PermissionError` creating `<worktree>-feature-check`, `gates-1.log`), so its gates
were run one by one (logs `gate-*.log`): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology, doc-figures: exit 0 each. complexity `--check` (exit 1, 7 listed) and file-length `--check` (exit 1, 26 listed) are red
on the train by design; compared with the cached base listing for `081630835b` using `feature_check.py`'s own `key`/`item_file`
functions, no listed item is new in a file this branch changed (both: `[]`). The loop's own run at session end is the first one-table run.

## Self-check

- **Each new test fails with the change reverted, passes restored:** yes, on the merged tree. `track.add` `neutral` mapping removed: 3/3 FAILED,
  exit 8 (`test-neg-1.log`). Toolbar `addAction` removed and the train's `hasChannelModel` widened to Neutral: `HybridTrackGuiTest`
  (4 cases) and `aNeutralRowTakesNoOutputInstrumentOrAutoChannel` FAILED, exit 8 (`test-neg-2.log`). All restored, rebuilt (`build-6.log` EXIT 0): exit 0 (`test-2.log`).
- **Every criterion proven by a test I ran:** criterion 1 (GUI + command create a row with stable id and position):
  `HybridTrackGuiTest::clickingTheControlAddsADrawnNeutralRow`, `HybridTrackRoundTripTest::trackAddCreatesANeutralRowWithAnIdAndAPosition`,
  `ControlHybridTrackTranscript`, exit 0 (`test-2.log`). Criterion 2 (round-trip; typed tracks still load): `aNeutralRowRoundTripsThroughSaveAndLoad`,
  `anOldProjectLoadsAndSavesNoNeutralMark`, `HybridTrackGuiTest::theRowIsDrawnAgainAfterSaveAndLoad`, exit 0.
- **One undo step, honest class:** `HybridTrackGuiTest::aClickIsOneUndoStep`, `HybridTrackRoundTripTest::oneUndoRemovesTheRowAndOneRedoRestoresIt`,
  `creatingARowIsATrueInverseStep`; `track.add` stays `true_inverse`, no new command (`ReversibilityContractTest`, `ReversibilityUndoTest` exit 0, `test-4.log`).
- **Audio thread:** the diff's audio-path code is `NeutralTrack::play` (new; returns the constant `false`: no allocation, lock, wait, I/O or signal;
  `TrackFolder::play` does the same) and `Track::create(Type, ...)` (one added `case`; control/GUI thread only). Creation, save/load, view and
  type-name tables run on the control/GUI thread. Not machine-checked (TSan does not run here); reasoned from the code.
- **Old projects / new data:** `anOldProjectLoadsAndSavesNoNeutralMark` loads a pre-neutral project unchanged and re-saves it with no
  `neutraltrack`/`type="8"`; the new row round-trips over two generations. Existing `Type` integers unchanged (Neutral = 8 after Folder = 7).
- **Portability:** no Qt-6-only API, no `small/near/far/interface/min/max` identifiers, no `std::filesystem`; the icon is drawn at runtime. The new test uses only
  `ConfigManager::value/setValue` (QString overloads already used by `ControlTrackRouting.cpp`) and `Mixer::numChannels`. Not built on Qt 5.15/MSVC/macOS here; reasoned.
- **Canary figures:** the submit check's WARN lists `clip.add`, `control.*` as "new ids" only because the snapshot's metadata block moved; the surface is
  917 ids / 78 groups, the same as the train, so no README figure changes (`doc-figures-gate.py` exit 0, `docfig-1.log`).
- **Neighbours on fresh binaries:** `test-4.log` 10/10 passed (ReversibilityContract/Undo, ScriptBindings, StableTrackIds, TrackFolder, UnclaimedElements,
  DawProjectInterchangeRoundTrip, AutomationLaneChild{,Gui}, ControlCommandsSnapshot); `SessionAudioRecordTest` exit 0 (`test-5.log`).
  An earlier neighbour run (`test-3.log`) used a bad target list and partly stale binaries; it is superseded by `test-4.log` and not relied on.
