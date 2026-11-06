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

## Self-check

- **Each new test fails with the change reverted, passes restored:** yes. `track.add` mapping reverted: 3/3 FAILED, exit 8 (`test-neg-1.log`); toolbar `addAction` reverted: `HybridTrackGuiTest` FAILED, exit 8, 4 cases (`test-neg-2.log`); restored: exit 0 (`test-6.log`).
- **Every criterion proven by a test I ran:** see the acceptance table (criterion 1: `HybridTrackGuiTest::clickingTheControlAddsADrawnNeutralRow`, `HybridTrackRoundTripTest::trackAddCreatesANeutralRowWithAnIdAndAPosition`, `ControlHybridTrackTranscript`, all exit 0; criterion 2: `aNeutralRowRoundTripsThroughSaveAndLoad`, `anOldProjectLoadsAndSavesNoNeutralMark`, `theRowIsDrawnAgainAfterSaveAndLoad`, exit 0).
- **One undo step, honest class:** `HybridTrackGuiTest::aClickIsOneUndoStep` (depth +1, undo removes row and view, redo restores) and `HybridTrackRoundTripTest::oneUndoRemovesTheRowAndOneRedoRestoresIt`; `track.add` stays `true_inverse` (`ReversibilityContractTest` exit 0, `test-5.log`). No new command.
- **Audio-path code the diff touches, by name:** `NeutralTrack::play` (new; returns constant `false`, no allocation, lock or signal - the arm `Track::play` callers already treat as "contributed no handle", like `TrackFolder::play`); `Track::create(Type, TrackContainer*)` (one added `case`; control/GUI-thread only, it brackets with `requestChangeInModel`/`doneChangeInModel` exactly as before); `Track::Type` (an enum value, no code). Nothing else on a realtime path changed.
- **Audio thread:** nothing added. `NeutralTrack::play` returns a constant false; creation, save/load, view and the type-name tables run on the control/GUI thread; no signals, locks or allocation on audio paths. Not machine-checked (TSan does not run here); reasoned from the code.
- **Old projects / new data:** `anOldProjectLoadsAndSavesNoNeutralMark` loads a pre-neutral project unchanged and re-saves it with no `neutraltrack`/`type="8"`; the new row round-trips (two generations). Existing `Type` integers are unchanged (Neutral=8 after Folder=7).
- **Portability:** no Qt-6-only API (`QPainter`, `QPixmap`, `QString::fromLatin1`, `QToolBar::widgetForAction` only); no `small/near/far/interface/min/max` identifiers; no `std::filesystem`; the icon is drawn at runtime, so no theme file or reconfigure is needed. Not built on Qt 5.15/MSVC/macOS here; reasoned.

- **Canary figures:** the submit check's WARN lists `clip.add`, `control.*` as "new ids" because the live snapshot's `captured_at`/`lane`/`version` metadata changed; the surface is still 878 ids / 77 groups, so no README figure moves (`doc-figures-gate.py` exit 0, `docfig-1.log`).
- **`feature_check.py`:** cannot run in this sandbox - it creates `../zene-feat-q050-283.1-feature-check` and a cache under `~/.local/share/zene-bunny-fix/qwen/gate-baselines`, both outside the write area (PermissionError, `gates-1.log`). Its gates were run one by one instead (listed above), so the loop's own run of it at session end is the first time it executes as one table.
