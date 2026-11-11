FALSE-POSITIVE

# gui-1030-menu-record-punch-in-here: "Punch In Here fails to show the new punch range"

Verdict: FALSE-POSITIVE. Record > Punch In Here sets and arms the range at the play position and the ruler paints it.
The sweep's screenshot is obstructed by the previous control's open command dialog. No product code changed; one
characterisation test was added (it passes with and without any change, as expected for a not-real finding).

## Evidence

1. The path is intact end to end (current tree).
   - `src/gui/RecordLaunchMenus.cpp:137` the item runs `transport.punch_here {edge: in}` synchronously
     (`src/gui/CommandRun.cpp:122-131`, `runCommand` -> `ControlRegistry::invoke`, no form because `ask` is false).
   - `src/core/ControlCommandsRecordTools.cpp:111-115` "in" calls `transport.punch_set` from `here` (the current mode's
     play position, the same value `transport.get_state` reports) to the song end, armed.
   - `src/core/ControlCommandsPunch.cpp:182-205` `punchSet` writes the range and arms it on the Song timeline.
   - `src/core/Song.cpp:252` and `src/gui/editors/TimeLineWidget.cpp:79` `Timeline::punchChanged` repaints the ruler;
     `TimeLineWidget.cpp:279-292, 227-250` draws the band (full-width strip, handles at the range ends) when
     `punchEnd > punchBegin`.
2. The sweep's own "after" screenshot shows the punch WAS set:
   `/home/kruzzzzy/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-24/shots/0275-after.png`
   - the Arranger toolbar's punch button is lit and its label changed to "Clear punch range" (that action is enabled
     only when a range exists, `TimeLineWidget.cpp:651`, `SongEditor.cpp:880-912`);
   - the ruler region is covered by the "Record From the Master or a Channel..." command dialog opened by the PREVIOUS
     sweep item (`RecordLaunchMenus.cpp:136`, a non-modal `CommandForm`, `CommandRun.cpp:112-118`) and still open in
     both the before and the after shots (`0275-before.png` shows it too). The judge compared two pictures with the
     same dialog on top of the ruler.
3. Reproduction test (end to end, real menu item, real ruler): `ArrangerTakePunchTest::punchInHereFromTheRecordMenuShowsTheBand`
   (`tests/src/gui/ArrangerTakePunchTest.cpp`). It seeks to bar 3 + 48 ticks with `transport.seek`, triggers the Record
   menu's "Punch In Here" action found by its `controlCommand` property, and asserts: punch begin == play position, end
   past it, armed, `ruler->hasPunchRange()`, a non-empty `punchBandRect()`, the in handle on the play position, red band
   pixels in `ruler->grab()`, and the toolbar's arm action checked. It passes on the unmodified product code, so there is
   no bug to turn RED -> GREEN.

Notes (observations, not changed):
- A first draft of the test moved only the Song-mode play head (`setPlayPos(.., Song)`) and `punchBegin` stayed 0: while
  stopped, `punch_here` reads the current mode's head (`PlayMode::None`), which `transport.seek` and a ruler click keep in
  step with the Song head (`ControlCommandsTransport.cpp:47-56`, `TimeLineWidget.cpp:446-454`). That is the documented
  contract (`RecordToolsTest.cpp:423`, review W3-batchA A1), so it is not a defect and was not touched.
- The punch band is a full-width strip with handles at the ends; with the range starting off-screen to the right
  only the handle is out of view. Design as built by Q050-147, out of scope for this finding.

## Commands run (all in the worktree; logs in the LOG FOLDER)

| step | command | exit |
| --- | --- | --- |
| build (first try, ccache temp dir not writable in the jail) | `cmake --build build --target ArrangerTakePunchTest -j4 > logs/build-1.log` | 2 (ccache permission, not code) |
| build, `CCACHE_TEMPDIR=$TMPDIR/cc-1030` | `cmake --build build --target ArrangerTakePunchTest -j4 > logs/build-2.log` | 0 |
| draft test, wrong item lookup (`data()` instead of the `controlCommand` property) | `cd build/tests && ctest -R ArrangerTakePunchTest --output-on-failure > logs/test-1.log` | 8 (test bug: item not found) |
| draft test, Song-mode seek only | `... > logs/test-2.log` (after `build-3.log`, exit 0) | 8 (test bug: `punchBegin` 0 != 624) |
| final test | `cmake --build build --target ArrangerTakePunchTest -j4 > logs/build-4.log` | 0 |
| final test | `cd build/tests && ctest -R ArrangerTakePunchTest --output-on-failure > logs/test-3.log` | 0 (100% tests passed out of 1 ctest test; its 4 test slots all pass) |
| gates (copy of feature_check.py with only `ROOT` and the log directory redirected, because the original writes a sibling directory outside the jail) | `python3 logs/feature_check_copy.py <worktree> > logs/gates-2.log` | 0, RESULT: PASS (8/8) |

The original `feature_check.py <worktree>` exited 1 with `PermissionError` creating `<worktree>-feature-check` (write-jail),
saved in `logs/gates-1.log`; the redirected copy ran the same eight gates.

Step 4b (revert the fix and rerun) does not apply: there is no product fix.

Files committed: `tests/src/gui/ArrangerTakePunchTest.cpp`, this report.

## Self-check

- Test fails with the fix reverted and passes with it: not applicable, there is no product change (FALSE-POSITIVE). The test
  passes on the unmodified product (`test-3.log`, exit 0). To show it is not vacuous, its first drafts failed when the
  setup did not match the real path (`test-1.log`, `test-2.log`, exit 8), so its assertions bite on `punchBegin`.
- Deterministic: no sleeps or timers. The play head is set by `transport.seek` to a fixed tick (3 * 192 + 48), the menu
  action is triggered directly (`QAction::trigger`, synchronous), the ruler is resized to a fixed 1200x32 and painted with
  `grab()`; only `processEvents()` is used to flush repaints.
- Fix complete / siblings: no fix. Siblings checked for consistency: `punch_here edge out` (`RecordToolsTest.cpp:420-430`
  already covers it), the toolbar arm action (`SongEditor.cpp:885-912`, reads the Song-mode head, kept equal to the stopped
  head by `transport.seek`/ruler clicks), `transport.punch_set`/`punch_clear` (`ControlCommandsPunch.cpp:182-230`).
- No collateral damage: product code untouched; saved data and loading unchanged. The only change is one added test case.
- FALSE-POSITIVE contract and code: see Evidence 1-3 above (`RecordLaunchMenus.cpp:137`, `CommandRun.cpp:122-131`,
  `ControlCommandsRecordTools.cpp:111-115`, `ControlCommandsPunch.cpp:182-205`, `TimeLineWidget.cpp:79,227-292`, and the
  sweep screenshot `0275-after.png` showing "Clear punch range" with the previous control's dialog over the ruler).
