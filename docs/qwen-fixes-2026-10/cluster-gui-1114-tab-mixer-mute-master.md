FALSE-POSITIVE

# cluster gui-1114-tab-mixer-mute-master — "Master mute click shows Devices but does not visibly enable mute"

Verdict: **FALSE-POSITIVE.** The sweep's click never reached the Master mute button. At the sweep's window
size the button's top **4 px** are the only part of it above the footer; the point the sweep clicked — the
centre of the AT-SPI box — is 6 px below that, on the footer's **Devices** switch. Clicking the button where
it actually is on screen toggles the model and the button's checked state, and opens nothing.
`ShellSelection::eventFilter` is not the cause: it returns `false` and does not consume the press.

No product code changed. This commit adds one guard test (`tests/src/gui/MixerSelectionIdentityTest.cpp`),
this report and the logs it cites.

## 1. What the sweep did (evidence run `m0-train-29`)

`~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-29/`:

| file | record |
| --- | --- |
| `inv-tab-mixer.jsonl:102` | `Mute Master`, box `x 292, y 976, w 20, h 20`, `showing: true`, `checked: false` |
| `results.jsonl:116` | click at **292,976** — the box's centre (`inventory.py` records `box[1] + box[3] // 2`) |
| `shots/0115-before.png` | 1600 x 1000 |

So the press was `gui.py click 292 976` on a button whose box is y 966-986.

## 2. What is actually at that point (measured, this lane)

Temporary probe slot in `MixerSelectionIdentityTest` (removed before this commit), fresh 1600x1000 offscreen shell, Arrange view, Mixer in the
bottom region (`01-probe-strip-and-region.log` (EVIDENCE.md), `02-probe-footer-hit.log` (EVIDENCE.md)):

```
PROBE base: win=1600x1000 winMin=867x416 mute=282,966 shown=282,966 20x4 bottomH=300 bottomMin=120 bottomMinHint=474 rows=583,300 rowsMinHint=301
PROBE sizes: stripLayoutMin=361 stripLayoutHint=361 stripMinHint=361 mixerLayoutMin=418 mixerMinHint=418 modeH=74
PROBE at(292,976) class=lmms::gui::ShellButton acc=Devices tip=Devices (Alt+D) geom=268,973 77x24
PROBE mute=282,966 20x20 modelIsMasterMute=1
```

- The mute button's AT-SPI box (282,966 20x20) matches the sweep's, so the two frames agree on where the
  control is.
- The bottom region is **300 px** (`src/gui/shell/Shell.cpp:211`, `include/DesignTokens.h:127`
  `bottomHeight = 300`) while the mixer panel inside it needs **474 px**
  (`shellBottom->minimumSizeHint().height()`, `03-probe-fit-at-heights.log` (EVIDENCE.md)) and its strip needs 361. The
  strip's lower controls are therefore below the panel's edge: `shown=282,966 20x4` — 4 of 20 px visible.
- `win->childAt(292,976)` is a `ShellButton` at `268,973 77x24` with accessible name **Devices**
  (`src/gui/shell/ShellChrome.cpp:478` builds the bottom switches, `:497` `panelButton`). The footer is
  `30 px` (`tokens::size::footerHeight`) and sits at the bottom of the window.
- Pressing that footer switch opens Devices and leaves the mute model off — the exact pair the finding
  reports (`02-probe-footer-hit.log` (EVIDENCE.md): `after footer click: devicesVisible=1 mute=0`).

When the region is tall enough for its panel, the same button is wholly on screen and a real click toggles
it (`03-probe-fit-at-heights.log` (EVIDENCE.md), all window heights, `shown=20x20`):
`rows=...,474 bottom=474 muteY=792 onScreen=1`, `mute=1 checked=1 devices=0` (`01-probe-strip-and-region.log` (EVIDENCE.md)).

## 3. The code path: the button is correctly bound and the filter does not swallow the press

- `src/gui/MixerChannelView.cpp:146-150`: `m_muteButton` has `setObjectName("btn-mute")`,
  `setToolTip(tr("Mute this channel"))`, `setCheckable(true)` and `setModel(&mixerChannel->m_muteModel)`.
  `guimain`'s Master channel is `Engine::mixer()->mixerChannel(0)`, so option (a) of the finding —
  "Mute Master" being some other control — is excluded: the probe asserts
  `mute->model() == &master->m_muteModel` and it holds.
- `src/gui/widgets/AutomatableButton.cpp:114-127`: `mousePressEvent`, left button, checkable → `toggle()`
  synchronously, then `_me->accept()`. No queued path, no model round-trip that could be lost.
- `src/gui/shell/ShellSelection.cpp:169-191`: the app-wide filter walks the widget's parents and calls
  `setChannel(...)` for a mixer strip, then **`return false;`** (`:190`). Returning `false` from an event
  filter does not consume the event; Qt still delivers the press to the button. The finding's stated root
  cause ("the filter treats clicks on descendants of a mixer strip as channel selection") is true as far
  as it goes, but it does not explain a missing toggle: the filter's own `changed()` signal rebuilds the
  Devices *panel contents*, it does not open the panel. The panel the sweep saw open was opened by the
  footer switch the click actually landed on.
- `src/gui/shell/DevicePanel.cpp` and `InspectorPanel.cpp` react to `ShellSelection::changed()` by
  rebuilding, not by becoming visible; visibility comes from `window.toggle` / `ShellFooter`'s buttons.
  Measured directly, the finding's frame shows the Devices panel is *closed* before a press on the button
  and after it (`07-test-guard.log` (EVIDENCE.md), both assertions pass).

## 4. The guard tests (two new slots, existing file)

`tests/src/gui/MixerSelectionIdentityTest.cpp`:

- `:286` `theMasterMuteButtonTogglesInTheMixerView` — the control's own contract, in Mix view where the
  mixer is the centre panel: the button's whole 20x20 box survives clipping by every ancestor up to the
  window, `childAt` its centre is the button, a real `QTest::mouseClick` turns `m_muteModel` on and the
  button checked (the press also selects the strip), Devices stays closed, and a second click turns both
  off with Devices still closed.
- `:346` `aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt` — the finding's frame (Arrange view,
  Mixer in the bottom region): presses the centre of the part of the button that is actually on screen,
  asserts it reaches the button and toggles the same model without opening Devices, and asserts that the
  sweep's AT-SPI centre (the centre of the whole box) is **not** inside the button while the strip is
  clipped — i.e. the sweep's point was the footer.
- Both evaluate their booleans only after the shell is put back (`setView(Arrange)` + `revealPanel(detail)`,
  mute model off), so a failure cannot leak into the next case in the file.

They pass on this tree, as a FALSE-POSITIVE guard must (the product is unchanged): `07-test-guard.log`
(EVIDENCE.md) — `Totals: 12 passed, 0 failed`. This is not a proof-of-fix; §5 is the proof the tests can
fail, done by mutating the line the finding blames.

## 5. Proof the tests are sensitive to the alleged cause (mutation, then restore)

The finding names `ShellSelection.cpp:171`. The mutation changes the filter's `return false;` (`:190`) to
`return true;` — the "filter consumes the click" behaviour the finding describes — and nothing else
(`08-mutation.patch` (EVIDENCE.md), one line).

| step | command (LOGS = `docs/qwen-fixes-2026-10/gui-1114-logs/EVIDENCE.md`) | EXIT |
| --- | --- | --- |
| build, unchanged tree | `CCACHE_DISABLE=1 cmake --build build --target MixerSelectionIdentityTest -j8 > build/qwen-1114-logs/120-build-split.log` (`LOGS/06-build-guard.log`) | 0 |
| test, unchanged tree (guard) | `cd build/tests && ./MixerSelectionIdentityTest -v1 > ../qwen-1114-logs/121-test-split.log` (`LOGS/07-test-guard.log`) | **0** (12 passed) |
| apply mutation | `git apply` of the one-line hunk in `08-mutation.patch` | 0 |
| rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene MixerSelectionIdentityTest -j8 > build/qwen-1114-logs/123-build-mutation.log` (`LOGS/09-build-mutation.log`) | 0 |
| test, mutated | `cd build/tests && ./MixerSelectionIdentityTest -v1 > ../qwen-1114-logs/124-test-mutation.log` (`LOGS/10-test-mutation.log`) | **4** — both new cases fail (`theMasterMuteButtonTogglesInTheMixerView` `'on' returned FALSE. (a real press on Master mute did not turn the mute model on)`, `aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt` `'on' returned FALSE. (a press on the visible part of the bottom region's mute did not toggle it)`), plus the pre-existing `pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment` and `aNewReturnStripIsLabelledReturnAtOnce`; 8 passed, 4 failed |
| reverse | `git apply -R` of the same hunk | 0 |
| rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene MixerSelectionIdentityTest -j8 > build/qwen-1114-logs/125-build-restored.log` (`LOGS/11-build-reapplied.log`) | 0 |
| test, restored | `cd build/tests && ./MixerSelectionIdentityTest -v1 > ../qwen-1114-logs/126-test-restored.log` (`LOGS/12-test-reapplied.log`) | **0** (12 passed) |

The mutated filter also fails the file's pre-existing
`pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment` and
`aNewReturnStripIsLabelledReturnAtOnce` — independent confirmation that a consuming filter breaks the
mixer's controls, i.e. the current `return false` is the contract those cases rely on.

A stricter variant of the first case that also required the whole button to be on screen in the **bottom**
region was written and rejected: it fails on this tree (`04-build-strict-variant.log`,
`05-test-strict-variant.log` in EVIDENCE.md — `only 20x4 of the 20x20 mute at 282,966 is on screen in the
bottom region`). That is the clipping issue of §2, not this finding's control, and making it a required
assertion would be asking this bug fix to change the shell's default bottom height (a design decision, §6).
It is recorded here so the measurement is not lost.

## 6. Side finding: the bottom region clips the mixer (not fixed here)

The bottom region is a fixed 300 px, its panel's minimum is 474 px, and `Shell` neither raises the row to
the panel's minimum nor makes the region scroll. The same clipping affects the strip's solo, pan, fader and
meter (`03-probe-fit-at-heights.log` (EVIDENCE.md); in the earlier probe at 600 px all were fully visible). It is the
reason the sweep's coordinate was outside the button. Fixing it means choosing between a taller default
bottom region and a scrolling one, and it changes the first-run layout and every saved `rows` size, so it
is a shell design decision outside this finding. Two earlier accepted reports recorded the same thing and
left it out of scope: `docs/qwen-fixes-2026-10/cluster-gui-1105-tab-mixer-add-effect.md` §1 (the mixer's
minimum height is taller than the default bottom panel; "needs a design decision (dock default height or
scrolling)") and `cluster-gui-1030-tab-mixer-new-mixer-channel.md` (bottom height noted as out of scope).
The clipping does not make the Master mute control broken: it makes its AT-SPI centre un-clickable, which
is what this false positive is.

## 7. Commands and exit codes (all logs in `docs/qwen-fixes-2026-10/gui-1114-logs/`)

LOGS = `docs/qwen-fixes-2026-10/gui-1114-logs/EVIDENCE.md`, committed next to this report: each
`NN-name.log` below is that file's `## NN-name.log` section, verbatim. The lane's LOG FOLDER
(`~/.local/share/zene-bunny-fix/qwen/runs/gui-1114-tab-mixer-mute-master.r1/logs`, already `drwxrwxrwx`) is
not writable to this process: a plain write, a `chmod`-then-write and a Python `open()` all fail with
`PermissionError(13, 'Permission denied')` while `os.access(dir, os.W_OK)` says `True`
(`15-log-folder-write.log` in EVIDENCE.md) — the denial is the session sandbox, not the mode, and this lane
cannot lift it. The logs are therefore kept in the worktree, next to the report. Every command below was run
from the worktree root unless it says `cd build/tests`.

| step | command | EXIT |
| --- | --- | --- |
| probe: strip, region and hit target | `CCACHE_DISABLE=1 cmake --build build --target MixerSelectionIdentityTest -j8` then `cd build/tests && ./MixerSelectionIdentityTest probeSplitterHints -v1` | build 0, run 0 |
| probe: what is at 292,976 | `cd build/tests && ./MixerSelectionIdentityTest probeFooterSwitch -v1` | 0 |
| probe: fit at several window heights | `cd build/tests && ./MixerSelectionIdentityTest probeSizesFit -v1` | 0 |
| guard build | `CCACHE_DISABLE=1 cmake --build build --target MixerSelectionIdentityTest -j8 > build/qwen-1114-logs/120-build-split.log` (`LOGS/06-build-guard.log`) | 0 |
| guard run | `cd build/tests && ./MixerSelectionIdentityTest -v1 > ../qwen-1114-logs/121-test-split.log` (`LOGS/07-test-guard.log`) | 0 (12 passed) |
| mutation build | `CCACHE_DISABLE=1 cmake --build build --target zene MixerSelectionIdentityTest -j8 > build/qwen-1114-logs/123-build-mutation.log` (`LOGS/09-build-mutation.log`) | 0 |
| mutation run | `cd build/tests && ./MixerSelectionIdentityTest -v1 > ../qwen-1114-logs/124-test-mutation.log` (`LOGS/10-test-mutation.log`) | 4 (8 passed, 4 failed) |
| restore + build | `git apply -R` of the `08-mutation.patch` hunk, then `CCACHE_DISABLE=1 cmake --build build --target zene MixerSelectionIdentityTest -j8 > build/qwen-1114-logs/125-build-restored.log` (`LOGS/11-build-reapplied.log`) | apply 0, build 0 |
| restore + run | `cd build/tests && ./MixerSelectionIdentityTest -v1 > ../qwen-1114-logs/126-test-restored.log` (`LOGS/12-test-reapplied.log`) | 0 (12 passed) |
| family | `CCACHE_DISABLE=1 cmake --build build --target ShellTest ShellFloatButtonTest ShellHomesTest MixerModesTest MixerZonesTest MixerPinnedStripsTest PanelShortcutsTest -j8` then `cd build/tests && ctest -R '^(MixerSelectionIdentityTest\|MixerZonesTest\|MixerModesTest\|ShellTest\|ShellHomesTest\|ShellFloatButtonTest\|PanelShortcutsTest\|MixerPinnedStripsTest)$' --output-on-failure > LOGS/13-family-ctest.log` | build 0, ctest **0** (8/8) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/14-feature-check.log` | **0** — RESULT: PASS, all 8 rows |

`ShellTest`, `ShellFloatButtonTest`, `ShellHomesTest`, `MixerModesTest`, `MixerZonesTest`,
`MixerPinnedStripsTest` and `PanelShortcutsTest` were not built in this lane's tree at first, so ctest
reported them "Not Run"; after building them the same selection is 8/8 (`13-family-ctest.log` (EVIDENCE.md)).

Gates note: `feature_check.py` writes inside the worktree (`build/feature-check/`) by default and did so
here; no redirected copy and no changed limits or baselines were used.

Files committed: `tests/src/gui/MixerSelectionIdentityTest.cpp`, this report, and the evidence log
`docs/qwen-fixes-2026-10/gui-1114-logs/EVIDENCE.md`.

## Self-check

- **? The regression test FAILS with your product change reverted and PASSES with it (exit codes).** The
  product change is *none* (verdict FALSE-POSITIVE), so the equivalent proof is a mutation of the exact
  line the finding blames: `ShellSelection.cpp:190` `return false;` → `return true;` (`08-mutation.patch` (EVIDENCE.md)).
  With the mutation both new cases fail (`theMasterMuteButtonTogglesInTheMixerView` `'on' returned FALSE.
  (a real press on Master mute did not turn the mute model on)` and
  `aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt` `'on' returned FALSE. (a press on the visible
  part of the bottom region's mute did not toggle it)`), test EXIT **4** (`10-test-mutation.log` in
  EVIDENCE.md). Reversed and rebuilt, the same cases and the whole file pass, EXIT **0**
  (`12-test-reapplied.log` in EVIDENCE.md: `Totals: 12 passed, 0 failed`). A stricter variant that also
  demanded the whole button be visible in the bottom region was run and fails on the unmodified tree — the
  clipping of §6 — and was not shipped as the guard (`05-test-strict-variant.log` (EVIDENCE.md)).
- **? The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it
  uses).** No `qWait`, `sleep`, timer, retry or `QTRY_*` in either slot (`grep` over the file: no matches).
  It reads only synchronously-updated values: `AutomatableButton::mousePressEvent` calls `toggle()` before
  returning (`AutomatableButton.cpp:114-127`), `Shell::setView`/`revealPanel` call `apply()` synchronously,
  and the two `processEvents`/`sendPostedEvents(LayoutRequest)` calls only flush Qt's own queued layout
  work, with no deadline. Frames are compared after `win->resize(1600,1000)` on the offscreen platform,
  whose window size is fixed by the shell's showEvent; the test's own numbers (mute at 282,966, 20x4
  visible, `bottomMinHint=474`) are identical across runs and match the sweep's inventory.
- **? The fix is at the cause and complete: every caller and sibling of what you changed was checked and
  stays consistent.** No product file changed. The two candidate causes were checked and excluded with
  evidence: (a) the press filter — `ShellSelection.cpp:190` returns `false`, and mutating it to `true`
  breaks the button in *both* views and also breaks two pre-existing cases in the same file
  (`10-test-mutation.log` (EVIDENCE.md)); (b) the model binding — `MixerChannelView.cpp:146-150` binds `btn-mute` to
  `mixerChannel->m_muteModel`, and the probe asserts `mute->model() == &master->m_muteModel` for channel 0,
  the Master. The sibling controls built the same way (`btn-solo`, pan, fader) are covered by the same
  filter behaviour: with the strip fully shown (600 px region probe, `01-probe-strip-and-region.log` (EVIDENCE.md)) every
  one of them hits itself (`mute`, `solo`, `pan`, `fader`, `meterInfo` `hit=self`).
- **? No collateral damage: old projects and saved data still load as before, and any limit or check you
  added does not hit other callers that legitimately exceed it.** No product data, model, save/load,
  command or layout code changed, so there is nothing new to load or round-trip. The test only touches
  `m_muteModel` (a plain model value) and the shell's view/panel state, and restores both before it
  asserts: `setView(Arrange)` + `revealPanel(detail)`, and `m_muteModel` ends at `false` in every path.
  The second case does not assert anything while the shell is in the finding's frame; it records booleans
  first, restores, then `QVERIFY2`s them.
  Family run after the change: 8/8 (`13-family-ctest.log` (EVIDENCE.md)). No limit, ratchet or gate was edited; the
  gates are the shipped ones and pass unchanged (`14-feature-check.log` (EVIDENCE.md)).
- **? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract
  that shows it.** `src/gui/widgets/AutomatableButton.cpp:114-127` (a left press on a checkable button
  toggles its model synchronously and accepts the event) plus `src/gui/MixerChannelView.cpp:146-150` (the
  button's model is the channel's `m_muteModel`) show the control is wired. `src/gui/shell/ShellSelection.cpp:190`
  `return false;` shows the selection filter does not consume the press. The contract that makes Devices
  *open* is the footer switch: `src/gui/shell/ShellChrome.cpp:478` places the bottom-region switches in the
  footer and `:497` builds each from its `window.toggle` action. The measurement that ties the two
  together: the mute button's AT-SPI centre (292,976) is over that switch at `268,973 77x24`
  (`02-probe-footer-hit.log` (EVIDENCE.md)), 6 px below the 4 px sliver of the button that the 300 px bottom region
  leaves visible (`01-probe-strip-and-region.log` (EVIDENCE.md)).
