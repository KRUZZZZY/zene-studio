IMPLEMENTED

# Q050-068 - Keyboard traversal through mixer controls

Branch `feat/050-Q050-068-oc`, base `2da2ed356` (`050/m0-train-8`). GUI focus/navigation only:
no command, project data, undo step or persistence is added.

## Acceptance

| # | Criterion | Command / test | What it proves | Exit |
|---|---|---|---|---|
| 1 | Faders and LED buttons accept focus and keyboard adjustment and appear in the accessibility walk | `ctest -R '^MixerModesTest$' --output-on-failure` (log `test-MixerModesTest-6.log`), case `mixerControlsTakeFocusAndAnswerTheKeys` | Mute/solo/fader/pan are `Qt::TabFocus`; the strip's visible Tab stops are walked through `QAccessible` with non-empty names; roles are Slider/Dial/Button; names are "Fader Lead", "Mute Lead", etc.; End/Home/PageUp/Up move the fader model and Space toggles Mute. | 0 |
| 2 | Ctrl+Tab / Ctrl+Shift+Tab move to the same control on adjacent channels; Tab moves within a strip; focus and the name are announced | same ctest run, cases `tabMovesThroughOneStripsControls`, `ctrlTabMovesTheSameControlAcrossChannels` | Tab/Shift+Tab walk the strip forward and in reverse (same set, reverse order) and wrap inside it; Ctrl+Tab takes Mute→Mute and Fader→Fader on the next channel, Ctrl+Shift+Tab comes back; the selected channel follows the focus; each focused control's confirmed accessible name is checked. | 0 |
| 3 | Regression covers forward, reverse and boundary behaviour | same ctest run, case `ctrlTabDoesNotWrapAtTheMixerEnds` | First proves forward/reverse movement still works into the last/first channel, then that Ctrl+Tab at the last channel and Ctrl+Shift+Tab at Master keep both focus and selection. | 0 |
| - | Accessible walk still holds | `ctest -R '^(AccessibilityWalkTest|KnobKeyboardTest)$' --output-on-failure` (log `test-a11y-knob-2.log`) | The product-wide walk finds no unnamed focusable widget; the knob/fader/button keyboard contract is unchanged. | 0 |
| - | Mixer neighbours | `ctest -R '^(MixerSendsOnFadersTest|MixerSelectionIdentityTest)$' --output-on-failure` (log `test-mixer-neighbours-1.log`) | Send-on-faders and selection identity still pass with the new FocusIn selection. | 0 |
| - | Build | `cmake --build build --target zene AccessibilityWalkTest KnobKeyboardTest -- -j4` (log `build-6.log`) and `... MixerModesTest` (`build-green-1.log`) | The product and tests compile. | 0 |

Red/green (product hunks reversed, test change kept):

| State | Command | Exit |
|---|---|---|
| Product reverted | build `MixerModesTest` (`build-red-2.log`); `./MixerModesTest mixerControlsTakeFocusAndAnswerTheKeys tabMovesThroughOneStripsControls ctrlTabMovesTheSameControlAcrossChannels ctrlTabDoesNotWrapAtTheMixerEnds` (`test-red-2.log`) | build 0; test **4** (all four failed) |
| Product restored | build (`build-green-1.log`); same four cases (`test-green-1.log`) | build 0; test **0** |

Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`feature-check-1.log`):
fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures,
complexity (no new item) and file-length (no new item) all PASS; overall exit 0.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored.** Yes. Reversing
  only the non-test hunks (`include/MixerView.h`, `include/MixerChannelView.h`,
  `src/gui/MixerChannelView.cpp`), rebuilding and running the four cases gave exit **4** with four
  failures (`test-red-2.log`): missing channel-qualified names, Tab leaving the strip, and no
  Ctrl+Tab movement. Restoring the hunks and rebuilding gave exit **0** (`test-green-1.log`).
  The boundary case was strengthened with a working move into the boundary channel, so it also
  fails without the product change instead of passing vacuously.
- **Every acceptance criterion is proven by a test I ran.** Criterion 1:
  `MixerModesTest::mixerControlsTakeFocusAndAnswerTheKeys`, ctest exit 0. Criterion 2:
  `MixerModesTest::tabMovesThroughOneStripsControls` and
  `MixerModesTest::ctrlTabMovesTheSameControlAcrossChannels`, same ctest run exit 0.
  Criterion 3: `MixerModesTest::ctrlTabDoesNotWrapAtTheMixerEnds`, same ctest run exit 0.
- **One undo step per edit; every new/changed command has an honest reversibility class.**
  Not applicable: this unit adds/changes no command and performs no project edit. Focus movement is
  GUI state; the existing mixer edit commands and their undo steps are untouched.
- **Nothing added runs on the audio thread.** The diff is GUI-only (`MixerChannelView` event
  filter/key handling and accessible names, one `MixerView::channelCount` accessor). It allocates
  only on the GUI thread in response to focus/key events; no audio-path function calls it and it
  emits no signals from audio code.
- **Old projects load and render as before; new data round-trips.** No serialization, model or
  render path changed; there is no new data. `MixerModesTest` loads and exercises a real project
  fixture, and the neighbour mixer tests above pass.
- **Qt 5.15 / MSVC / macOS < 10.15 portability.** Uses only Qt 5.15-era APIs (`QList`,
  `QAccessible`, `QTest::qWaitForWindowActive`, `std::clamp` from C++20), no Qt-6-only call, no
  windows.h macro identifiers, no `std::filesystem`, no POSIX calls.
- **GUI and agent paths.** Per the scoping brief this action is GUI focus state only and explicitly
  out of scope for an agent command; no user-facing mixing edit was added, so the existing
  GUI/agent command pairs are unaffected.
- **Snapshot/doc figures.** No command metadata changed, so the live snapshot is not stale; the
  `doc-figures` gate passes (exit 0). No A16 histogram change was reported by
  `ReversibilityContractTest` (not touched).
