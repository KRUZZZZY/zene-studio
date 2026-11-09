Verdict: FIXED

gui-1030-tab-mixer-effects-chain: REAL. The Mix panel's minimum width forced the main window wider than a 1024 px display, which put the right-hand rack (EFFECTS CHAIN) off-screen. The finding blamed the channel area's 6-strip minimum; measurement shows the mode bar is the larger cause and the strip minimum the second. Both are fixed.

## Decision and evidence (round 1 measurements, offscreen, Mix panel revealed)

| Widget | minimumSizeHint width before | Source |
| --- | --- | --- |
| `MixerView` (whole panel) | 1576 | `layout()->setSizeConstraint(QLayout::SetMinimumSize)`, MixerView.cpp (constructor end) |
| `MixerModeBar` | **1576** | one `QHBoxLayout` of 13 controls, [MixerModeBar.cpp:104-150](src/gui/MixerModeBar.cpp) |
| `QSplitter mixerZones` | 763 | 64 master + 448 channels dock (6 strips x 64 + 64 add column) + 245 rack + handles |
| main window | 2000 | 200 (Inspector min, [Shell.cpp:204](src/gui/shell/Shell.cpp)) + 1576 + 220 (Browser min, [Shell.cpp:205](src/gui/shell/Shell.cpp)) + handles |

A QSplitter propagates children's minimums upward, so nothing is clipped inside the panel; the rack is off-screen only because the window cannot shrink to the display. Contract used for the test: the Mix panel must fit a 1024 px main window, i.e. a panel minimum of at most 1024 - 200 - 220 - handles (about 600 px). `MainWindow` defaults to 1600x1000 ([MainWindow.cpp:190](src/gui/MainWindow.cpp)); the finding's repro names 1024x768.

## The fix ([MixerView.cpp](src/gui/MixerView.cpp))

1. New local `ModeBarArea` (a `QScrollArea`) holds the mode bar. Its minimum width is 0, its height is the bar's height plus the horizontal scroll bar's height only while the bar is wider than the viewport. A narrow panel scrolls the bar sideways; all 13 controls stay reachable; a wide panel looks as before (same height, no scroll bar). The bar object, its parent chain to `MixerView`, `modeBar()` and every child's object name are unchanged.
2. The channel area keeps `MinVisibleStrips = 2` strips in view instead of 6 (it already scrolls). The splitter minimum becomes 582 px (VCA region visible), which fits the 600 px budget; 3 strips gave 646 px and failed.

Result: the main window minimum is now at most 1024 (the test resizes to 1024 and it takes it; before the fix it stayed at 2000).

## Evidence table (round 2; logs in `.../gui-1030-tab-mixer-effects-chain.r2/logs/`)

| Step | Command (from `build/` or `build/tests`; `CCACHE_DISABLE=1`, ccache temp dir is not writable in the sandbox) | Result |
| --- | --- | --- |
| RED build | `cmake --build . --target MixerZonesTest -j4 > build-red.log 2>&1` | EXIT=0 |
| RED run (no product change) | `ctest -R '^MixerZonesTest$' --output-on-failure > test-red.log 2>&1` | EXIT=8: `FAIL! theMixerFitsAMainWindowOf1024Pixels() 'window->width() <= 1024' returned FALSE (the window could not shrink below 2000 px)`; 10 passed, 1 failed |
| Intermediate (3 strips) | `test-green2.log` | EXIT=8, window floor 1070 px: VCA region adds ~75 px, so 2 strips chosen |
| GREEN build | `cmake --build . --target zene MixerZonesTest MixerModesTest MixerMeteringTest MixerSelectionIdentityTest MixerInsertLatencyTest MixerBusFreezeMenuTest MixerSendsOnFadersTest MixerStripFeaturesTest -j4 > build-green3.log 2>&1` | EXIT=0 |
| GREEN run | `ctest -R '^MixerZonesTest$' --output-on-failure > test-green3.log 2>&1` | EXIT=0 (11 slots pass) |
| 4b revert | `git diff -- src/gui/MixerView.cpp > logs/fix.patch && git apply -R logs/fix.patch` (APPLY-R exit 0), rebuild `build-4b-red.log` EXIT=0, `ctest -R '^MixerZonesTest$' > test-4b-red.log` | EXIT=8, same assertion, "could not shrink below 2000 px" |
| 4b re-apply | `git apply logs/fix.patch` (exit 0), rebuild `build-4b-green.log` | EXIT=0 |
| Family, 8 tests run | `ctest -R '^(MixerZonesTest\|MixerModesTest\|MixerMeteringTest\|MixerSelectionIdentityTest\|MixerInsertLatencyTest\|MixerBusFreezeMenuTest\|MixerSendsOnFadersTest\|MixerStripFeaturesTest)$' --output-on-failure > test-4b-green.log 2>&1` | EXIT=0; 8/8 passed |
| Gates (first) | `python3 logs/feature_check_local.py <worktree> > feature-check-2.log 2>&1` (copy of `feature_check.py` whose log directory is redirected to my logs folder, because the original writes to a sibling directory outside the write jail) | EXIT=0, all 8 rows PASS |
| Gates (after the `upstream-modifications.txt` line) | same, `feature-check-3.log` | EXIT=0, all 8 rows PASS |

Other `^Mixer` tests (MixerBusFreezeTest, MixerToolsTest, ...) are core/non-GUI tests that were not built in this tree and report "Not Run" (`test-family.log`); they do not include MixerView.

Files committed: `src/gui/MixerView.cpp`, `tests/src/gui/MixerZonesTest.cpp`, `tests/upstream-modifications.txt`, and this report. `MixerZonesTest` was already registered in `tests/CMakeLists.txt`; no new file, so `fork-sources.txt` is untouched.

## Integration onto 050/m0-train-26 (round 3)

Source `bugs/qwen-gui-1030-tab-mixer-effects-chain` @ `882422a898`, target `050/m0-train-26` @ `af010bc3cc`. The driver's three-way merge reported a conflict in `src/gui/MixerView.cpp`. It then fell back to direct application, and every hunk applied cleanly (`integration-apply.log`: "Applied patch to 'src/gui/MixerView.cpp' cleanly", same for the test file and `upstream-modifications.txt`). `diff <(git diff --cached) accepted.patch` printed nothing: the integrated change is byte-identical to the accepted patch, so it has **no semantic difference** and needed no hand resolution. The accepted design (the `ModeBarArea` scroll area plus `MinVisibleStrips = 2`) and its regression `MixerZonesTest::theMixerFitsAMainWindowOf1024Pixels` are kept unchanged.

Failure paths and platform hazards I checked together:
- **Narrow window:** the mode bar scrolls sideways. All 13 controls stay reachable (the test asserts the bar's scroll maximum is above 0 at 1024 px).
- **Wide window:** no scroll bar, and the bar keeps its old height (`fitHeight` adds the bar's height only when it scrolls).
- **Sends-on-faders banner** (min 382 px): narrower than the new floor, unchanged.
- **VCA region shown:** 582 px of a roughly 600 px budget.
- **Saved data:** zone layout JSON and `window.mixer_zones` are untouched.
- **Fonts:** desktop font metrics differ from offscreen ones, which is the 18 px margin noted below.
- **Platforms:** Qt-only widget code; no platform `#ifdef`s; `QScrollBar` is the only new include.
- **Realtime:** nothing on the audio thread.

| Step (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1030-tab-mixer-effects-chain.r3/logs/`) | Command | Result |
| --- | --- | --- |
| Build | `CCACHE_DISABLE=1 cmake --build . --target zene MixerZonesTest MixerModesTest MixerMeteringTest MixerSelectionIdentityTest MixerInsertLatencyTest MixerBusFreezeMenuTest MixerSendsOnFadersTest MixerStripFeaturesTest -j4 > 01-build-green.log 2>&1` | EXIT=0 |
| Family run, 8 tests | `ctest -R '^(MixerZonesTest\|MixerModesTest\|MixerMeteringTest\|MixerSelectionIdentityTest\|MixerInsertLatencyTest\|MixerBusFreezeMenuTest\|MixerSendsOnFadersTest\|MixerStripFeaturesTest)$' --output-on-failure > 02-test-green.log 2>&1` | EXIT=0, 100% passed out of 8 |
| 4b revert | `git diff --cached -- src/gui/MixerView.cpp > logs/fix.patch && git apply -R logs/fix.patch` | exit 0 |
| 4b RED | rebuild `zene MixerZonesTest` (`03-build-4b-red.log`, EXIT=0); `ctest -R '^MixerZonesTest$' --output-on-failure > 04-test-4b-red.log 2>&1` | EXIT=8: `FAIL! theMixerFitsAMainWindowOf1024Pixels() 'window->width() <= 1024' returned FALSE. (the window could not shrink below 2000 px)` |
| 4b re-apply | `git apply logs/fix.patch` | exit 0; `git diff` against the index is empty afterwards |
| 4b GREEN | rebuild (`05-build-4b-green.log`, EXIT=0); `ctest -R '^MixerZonesTest$' --output-on-failure > 06-test-4b-green.log 2>&1` | EXIT=0, 100% passed out of 1 |
| Gates, original script | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 07-feature-check.log 2>&1` | EXIT=1: `PermissionError` creating `<worktree>-feature-check` (sibling directory outside the write jail); no gate ran |
| Gates, redirected copy | `python3 logs/feature_check_local.py <worktree> > 08-feature-check.log 2>&1`. The only change from the original is the log directory, plus a pinned `ROOT` so the baselines resolve to the original location. | EXIT=0, 8/8 PASS (complexity and file-length: "no new item in your files") |

## Self-check

- **RED with the product change reverted, GREEN with it:** reverted `fix.patch` -> `ctest -R '^MixerZonesTest$'` EXIT=8 (window floor 2000 px); re-applied -> EXIT=0.
- **Deterministic:** the test uses fixed sizes only (`resize(1024, 768)`, then `processEvents`), asserts on layout minimums and geometry, never a timer, sleep or wall-clock value; it restores `resize(1400, 900)` at the end for later slots. The seam is the real main window and the real `MixerView`.
- **Cause and completeness:** checked the two things that set the panel minimum: the mode bar (1576 px, now in a scroll area, min width 0) and the strip minimum (6 -> 2). Remaining panel minimum (582 px with the VCA region shown) = master 64 + channel area 2x64 + add column 64 + rack 245 + handles + VCA ~75, all fixed-width regions the user can also resize with the zone separators. Siblings: `MixerModeBar` itself is unchanged, so `modeBar()` users (MixerModesTest, MixerZonesTest, MixerMeteringTest) still pass; the sends banner (min 382, hidden by default) is narrower than the new floor in the mode where it shows and was not changed.
- **No collateral damage:** GUI layout only; nothing is saved or loaded differently (mixer zone layout JSON, widths and `window.mixer_zones` untouched), no command added (canary unchanged, `doc-figures` PASS). Wide windows look the same: the scroll bar appears only when the bar is wider than the panel. A user who relied on 6 strips being guaranteed visible in a narrow panel now sees 2 and scrolls; the area was already a scroll area and still scrolls.
- **Realtime / undo:** no audio-thread code touched, no allocation outside GUI construction and resize, no undo step.
- **Not-real verdict:** not applicable, the verdict is FIXED.
- **Limitation:** font metrics differ between the offscreen platform and a desktop; the test's 1024 px budget is the shell's own 200 + 220 px side-panel minimums, and the margin is 18 px (582 vs 600).
