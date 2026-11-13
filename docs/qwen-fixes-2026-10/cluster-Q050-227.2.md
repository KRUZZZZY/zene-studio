IMPLEMENTED

# Q050-227.2 - Pinned mixer strips (stage 2/2)

Condensed report for the queue unit. Full behaviour and design notes: `docs/050-features/Q050-227.2.md`.
Manual: `data/manual/index.html` (pinned mixer strips paragraph). Builds on stage 1 (`cluster-Q050-227.1.md`).

## What changed

Base `20fa43c90b` to `HEAD` (13 files, +747 / -32):

- `include/MixerZones.h`, `src/gui/MixerZones.cpp`: `mixerzones::Layout` gains `pinned` (an ordered list of
  `ch-<n>` ids; the master is never pinned). `MixerZoneState` holds it beside the dock and size data and emits
  `pinsChanged()`.
- `include/MixerView.h`, `src/gui/MixerView.cpp`: a fixed pinned area left of the scrolling channels; the view
  re-lays itself on `pinsChanged()`. Pinned strips leave the scroll area; unpinned strips return to mixer order.
- `src/gui/MixerChannelView.cpp`: Pin Strip / Unpin Strip entry in the channel strip menu.
- `src/gui/ControlCommandsMixerView.cpp`: `window.mixer_view` takes `pinned_channels`, `pin`, `unpin` and reports
  `pinned_channels`. `window.mixer_zones` gains `pinned`. The same handler serves the menu and agents.
- Persistence rides the existing `mixer_zones` snapshot key, so `window.layout_project_save` and
  `window.layout_project_apply` save and restore pins. No second section, no second format. Layouts saved
  before this stage have no `pinned` key and open with nothing pinned.
- `tests/src/gui/MixerPinnedStripsTest.cpp` (new, 406 lines), registered in `tests/CMakeLists.txt`,
  `tests/all-sources.txt`, `tests/fork-sources.txt`.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json`: regenerated live.
- `data/manual/index.html`, `docs/050-features/Q050-227.2.md`: manual paragraph and feature record.

## Acceptance

| # | Criterion | Test | Command | Exit code |
| --- | --- | --- | --- | --- |
| 1 | A strip menu pins and unpins; pinned strips stay visible while the channel area scrolls; unpinned strips return to mixer order. | `MixerPinnedStripsTest::pinUnpinResizeSaveAndRestoreTogether` (strip menu, scroll) | `cd build/tests && ctest -R '^MixerPinnedStripsTest$' --output-on-failure` | 0 (`p7-green-test.log`) |
| 2 | `window.mixer_view` reports `pinned_channels` as channel IDs; the saved project layout restores those pins. | `MixerPinnedStripsTest::pinUnpinResizeSaveAndRestoreTogether` (save, reopen, restore) | same | 0 |
| 3 | One UI/agent-surface test covers pin, unpin, resize, save and restore together. | `MixerPinnedStripsTest::pinUnpinResizeSaveAndRestoreTogether` | same | 0 |
| 4 | Invalid input is refused (`InvalidArgs` for a non-array or the master; `NotFound` for an unknown id) and a refused call changes nothing. | `MixerPinnedStripsTest::pinningRefusesWhatCannotBePinnedAndToleratesOldLayouts` | same | 0 |
| 5 | A legacy layout without `pinned` opens with nothing pinned. | `MixerPinnedStripsTest::pinningRefusesWhatCannotBePinnedAndToleratesOldLayouts` (legacy snapshot case) | same | 0 |
| 6 | A pinned channel that is deleted does not crash, and the pins no longer name it. | `MixerPinnedStripsTest::pinningRefusesWhatCannotBePinnedAndToleratesOldLayouts` (deleted-channel case) | same | 0 |

Known caveat (test isolation): `pinningRefusesWhatCannotBePinnedAndToleratesOldLayouts` needs at least four
channels (`view->channelCount() >= 4`). `initTestCase()` creates one, and the earlier function adds the rest. Run
on its own (`build/tests/MixerPinnedStripsTest pinningRefusesWhatCannotBePinnedAndToleratesOldLayouts`), it fails
(exit 1, `p7-fn-b.log`). In the binary's normal order, which is what ctest runs, it passes. Run alone,
`pinUnpinResizeSaveAndRestoreTogether` passes (exit 0, `p7-fn-a.log`). Fixing the test to build its own channels is
a follow-up; this unit did not change the test.

## RED / GREEN

Product diff reverted and restored with `git diff 20fa43c90b HEAD -- src include` as the patch (not in the tree).

| Step | Command | Exit code | Log |
| --- | --- | --- | --- |
| RED build | `cmake --build build --target zene MixerPinnedStripsTest -- -j4` | 2 | `p7-red-build.log` (missing `populatePinAction`, `pinnedArea`, `channelScrollArea`) |
| RED ctest | `ctest -R '^MixerPinnedStripsTest$'` | 8 | `p7-red-test.log` (`Not Run`) |
| GREEN build | same build command, product restored | 0 | `p7-green-build.log` |
| GREEN ctest | same ctest command | 0 | `p7-green-test.log` (100% of 1, 3.21 s) |

Full sequence with timings: `red-green.txt`, p7 section.

## Gates

| Gate | Command | Exit code | Log |
| --- | --- | --- | --- |
| doc-figures-gate | `python3 tests/doc-figures-gate.py` | 0 | `p5-doc-figures-gate.log` (`RESULT: PASS`) |
| ReversibilityContractTest | `ctest -R '^ReversibilityContractTest$'` | 0 | `p5-ctest-rct.log` (100% of 1, 0.56 s) |
| Snapshot regenerated | `commands_snapshot.json` regenerated live | 0 | `p5-snapshot.log` |
| feature_check | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <WT>` | 0 | `p6-feature-check.log` (`RESULT: PASS`) |

## Realtime

The realtime path is untouched. Pins are GUI-thread widget state: the code is GUI-thread-only, with no engine
change, and nothing is added to `updateFaders()`, the audio thread or any model notification. TSan is not run
locally, and no cross-thread state was added.

## Portability

Qt5/Qt6-common API only: `QRegularExpression`, `QJsonArray`, `QStringList`, `QObject` signals and `QJsonObject`,
with no version-specific calls. The check is in `feature_check` (`check-namespace` and `no-tautology` both PASS,
`p6-feature-check.log`), so no forbidden identifiers were added.
