FIXED

# cluster gui-1106-menu-navigate-zoom-in — Zoom In can silently no-op at maximum zoom

## Verdict: REAL (fixed)

- Navigate > Zoom In runs `arrangement.zoom {"step": 4}` (`src/gui/MenuBarExtras.cpp:73`); Zoom Out runs `step: -4` (`:74`).
- `requestedZoom` clamps `value + step` to the zoom model's bounds (`src/gui/ControlCommandsArrangerView.cpp:146-153`); the
  model runs 0..`ZOOM_STEPS` (200), its maximum being `MAX_PIXELS_PER_BAR` = 400 px per bar (`src/gui/editors/SongEditor.cpp:84-86,98`),
  and its start value is read back from the UI settings (`SongEditor.cpp:185`), so a fresh project can start at the bound.
- Before the fix, `zoom()` then returned `ControlResult::success` with the zoom unchanged. A menu action announces only failures
  (`src/gui/CommandRun.cpp:90`: `result.ok && !call.showResult` returns early), so the menu closed with nothing changed and no message.
  The RED run below shows the command answering success at the maximum and at the minimum.
- The sibling commands already refuse when they have nothing to do: `zoom_back`, `zoom_forward` and `zoom_toggle` return a failure
  (`ControlCommandsArrangerView.cpp:220-260`, documented as "Refused when there is nothing earlier/later").

## Fix

`src/gui/ControlCommandsArrangerView.cpp:155-173` adds `stepBoundProblem()`. A `step` zoom (no `pixels_per_bar`, span or `fit`)
whose step points past a bound the model is already at is refused (`ControlErrorKind::Refused`). The message is "already at the
maximum zoom (400 px per bar); nothing to zoom in to", or the minimum version for zooming out. The call is at `:203-206`.
Because the menu path announces failures, the menu now floats that reason. These cases still succeed as before:
- a step that moves, including one clamped onto the bound (`ArrangerTimelineTest::anOversizedZoomStepClampsToMaximum` still passes);
- `step: 0`;
- every `pixels_per_bar` zoom (presets and view-memory restore at `ControlCommandsViewMemory.cpp:298`), span zoom and fit zoom.

Callers checked: the Navigate menu (`MenuBarExtras.cpp:73-74`), the panel shortcuts (`PanelShortcuts.cpp:96-99`, same args, now
announced the same way), the navigator `+`/`-` keys (`ArrangerNavigator.cpp:357-362`, they ignore the result, so nothing changes for
them), and the ruler/overlay/navigator span zooms (`ArrangerRulerLanes.cpp:503`, `ArrangerOverlays.cpp:225`,
`ArrangerNavigator.cpp:387`, which this fix does not touch). `tests/control-customisation.py:388` steps +3 from about 160 px per bar,
which is not at the bound. That live-instance script was not run here.
I left the command's description text unchanged on purpose. It is mirrored in `tools/mcp-zene-control/zene_control/commands_snapshot.json`,
so editing it would move the snapshot, and no command, schema or id changed.
GUI thread only: nothing on the audio thread. The zoom is interface state with no undo entry (`ControlReversibilityTableCustomisation.cpp:144`), and that is unchanged.
The refusal returns before `remember(before)`, so the zoom history is not touched either.

## Regression test

New `tests/src/gui/ArrangerZoomBoundTest.cpp`, a GUI test running offscreen. It is a new file because `ArrangerTimelineTest.cpp`
(1416 lines) is under the file-length ratchet. It is registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt` and
`tests/all-sources.txt`.
- `zoomInAtTheMaximumIsRefusedAndSaysSo`: 400 px per bar, then `step:4` must be refused with "maximum" and leave the zoom as it was.
  Triggering the real Navigate > Zoom In action must float a message containing "maximum".
- `zoomOutAtTheMinimumIsRefused`: the mirror case at 4 px per bar. After it, `step:+4` still works.
- `movingClampedZeroAndAbsoluteZoomsStaySuccesses`: from max-1, `step:4` clamps and succeeds. `step:0` succeeds, and so does
  `pixels_per_bar:400` at the max.

## Evidence (LOG FOLDER = ~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-zoom-in.r1/logs)

The sandbox denied ccache's configured temp dir, so builds ran with `CCACHE_TEMPDIR=<run folder>/ccache-tmp`. Plain
`cmake -S . -B build` is blocked by the lane guard (`01-cmake.log`, EXIT=97). Building the `zene` target re-ran CMake itself
(`03-...log`). That first build failed on the ccache permission (EXIT=2) before the temp-dir override.

| step | command | EXIT |
|---|---|---|
| RED build | `cmake --build build --target ArrangerZoomBoundTest -j4 > 06-build-red.log` | 0 |
| RED test | `cd build/tests && ctest -R ArrangerZoomBoundTest --output-on-failure > 07-test-red.log` | 8 (2 failed: `'!refused.ok' returned FALSE` at max and at min) |
| GREEN build | `cmake --build build --target zene ArrangerZoomBoundTest -j4 > 10-build-green.log` | 0 |
| GREEN test | `ctest -R 'ArrangerZoomBoundTest\|ArrangerTimelineTest' --output-on-failure > 11-test-green.log` | 0 (100% of 2) |
| 4b revert | `git diff -- src/gui/ControlCommandsArrangerView.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b build | `cmake --build build --target zene ArrangerZoomBoundTest -j4 > 12-build-revert.log` | 0 |
| 4b test (no fix) | `ctest -R ArrangerZoomBoundTest --output-on-failure > 13-test-revert.log` | 8 (Totals: 3 passed, 2 failed) |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b build | `cmake --build build --target zene ArrangerZoomBoundTest -j4 > 14-build-reapply.log` | 0 |
| 4b test (fix) | `ctest -R ArrangerZoomBoundTest --output-on-failure > 15-test-reapply.log` | 0 (100% of 1) |
| family build | build every `ctest -N -R 'Arranger\|MenuBar\|Navigat\|PanelShortcut\|ViewMemory'` target `> 16-build-family.log` | 0 |
| family test | `ctest -R 'Arranger\|MenuBar\|Navigat\|PanelShortcut\|ViewMemory' -j2 > 17-test-family.log` | 0 (100% of 7) |
| gates | `python3 feature_check.py <worktree>` | see note; jailed copy EXIT=0, all 8 PASS (`21-feature-check.log`) |

Gate note: the stock `feature_check.py` failed with EXIT=1 (`18-feature-check.log`, PermissionError). It writes its logs to
`<worktree>-feature-check`, which is outside this lane's write-jail. I ran an identical copy (`<run folder>/feature_check_jail.py`)
that changes only that logs path and pins `ROOT` to `~/.local/share/zene-bunny-fix/qwen`. Its result: fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS. The first jailed run (`20-...`)
failed fork-sources and all-sources-reproduce only because the new test was not yet tracked. They passed once it was `git add`ed.

Files committed: `src/gui/ControlCommandsArrangerView.cpp`, `tests/src/gui/ArrangerZoomBoundTest.cpp`, `tests/CMakeLists.txt`,
`tests/fork-sources.txt`, `tests/all-sources.txt`, `docs/qwen-fixes-2026-10/cluster-gui-1106-menu-navigate-zoom-in.md`.

## Self-check

- **The regression test fails with the fix reverted and passes with it.** Yes. Reverted (`git apply -R logs/fix.patch`, EXIT=0, rebuild EXIT=0):
  `ctest -R ArrangerZoomBoundTest` EXIT=8, 2 failed (`13-test-revert.log`). Reapplied (`git apply`, EXIT=0, rebuild EXIT=0): EXIT=0, 100% of 1
  (`15-test-reapply.log`). The first RED run (`07-test-red.log`) also gave EXIT=8 with the same two assertions.
- **The test is deterministic.** Yes. It uses no timers, sleeps or waits. The zoom bound is set directly, through `arrangement.zoom pixels_per_bar`
  (400 / 4) or `zoomingModel()->setValue(max-1)`. The menu path is `QAction::trigger()`, which runs synchronously. The floated
  message is read from the `TextFloat` top-level widgets right after one `processEvents()`. `TextFloat::displayMessage` creates and shows that widget
  synchronously; its timeout only hides it later. `init()` closes any leftover floats, and the test calls `setHeadless(false)` the same way
  `GrooveMenuTest`/`PluginShortcutsTest` do.
- **The fix is at the cause and complete.** Yes. The silent success came from `zoom()` itself, so that is where the fix is. Every `step` caller is covered:
  Navigate menu `MenuBarExtras.cpp:73-74`, panel shortcuts `PanelShortcuts.cpp:96-99`, and navigator keys `ArrangerNavigator.cpp:357-362`, which
  ignore the result. Callers using pixels_per_bar, span or fit are not affected (`stepBoundProblem` returns empty for them). This matches the
  siblings `zoom_back`/`zoom_forward`/`zoom_toggle`, which already refuse a no-op. In the family run (`17-test-family.log`), 7 of 7 tests pass, including `ArrangerTimelineTest`'s
  `anOversizedZoomStepClampsToMaximum` and its step-zoom history cases.
- **No collateral damage.** Yes. Nothing about project or save data changes: zoom is interface state and is not saved in the project. The UI-settings zoom value
  is still read as before (`SongEditor.cpp:185`). The new refusal fires only when the model is already exactly at the bound the step points toward
  (`value >= max` with step>0, or `value <= min` with step<0). Steps that move, including ones clamped to the bound, and absolute zooms are unchanged. No schema,
  command id or description changed, so the commands snapshot does not move.
- **Verdict ALREADY-FIXED or FALSE-POSITIVE.** Does not apply: the verdict is FIXED (REAL), shown by the RED run.
- **Sibling commands in `ControlCommandsArrangerView.cpp`.** Only `arrangement.zoom`'s step branch changed.
  - `zoom_back` / `zoom_forward` / `zoom_toggle` already refuse a no-op (NotFound when nothing to walk). The new refusal follows the same pattern, so they are consistent and unchanged.
  - `arrangement.zoom` with pixels_per_bar, span (`start`/`end`) or `fit` is unchanged. An absolute target is something the caller asked for by value; it is not a relative step that cannot move.
  - `fit_tracks`, `get_state`, `modes`, `modifier_tools`, `navigator`, `next_transient`, `nudge`, `ruler_lanes`, `scroll`, `select_clip` and `set_tool` do not go through `zoom()`, `requestedZoom()` or `stepBoundProblem()`, so they are not affected.
  - `get_state`'s zoom fields come from `zoomState()`, which is untouched.

## Gate status at HEAD (after commit)

- The stock `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits 1 (`24-feature-check-stock.log`) with
  `PermissionError` on `mkdir <worktree>-feature-check`. That directory sits outside this lane's write-jail, so `submit_check.py` reports
  "gates FAIL" with no gate named. This is BLOCKED by the environment, not by a gate result.
- The same script with only its logs path redirected into the run folder (`feature_check_jail.py`) exits 0 at HEAD
  (`25-feature-check-jail.log`): all 8 gates PASS.
- A run with write access to the worktree's parent directory (for example the parent loop) will reproduce the stock check.
