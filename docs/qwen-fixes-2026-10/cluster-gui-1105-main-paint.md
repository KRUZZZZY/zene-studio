FALSE-POSITIVE

# Cluster gui-1105-main-paint - "Paint tool click does not select Paint" (Terra sweep m0-train-25)

Round 2. Round 1's session ended without a commit (rc=1, only a test-only leftover patch); this round finishes the
procedure. Luna's round-1 review listed no further points beyond "finish and commit the report".

## Verdict: FALSE-POSITIVE - the sweep's click never reached the Paint button

The button and its wiring work. A real mouse click on the Paint button selects Paint, with or without a clip open.
The sweep's click at (180, 742) landed on the popped-out Inspector window, which covers that part of the toolbar.

### Evidence

- **Wiring (product, unchanged):** `src/gui/noteeditor/NoteEditorToolbar.cpp:169-176` creates one checkable action per
  tool in the exclusive `m_tools` group, connects `QAction::triggered` to `setTool(tool)` (line 175) and shows it
  through `toolButton(bar, action)` (line 113). Nothing guards on a clip, so a fresh project behaves the same.
- **The sweep's own record** (`~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-25/results.jsonl`):
  - line 13: "Pop out the Inspector panel" (179, 99), clicked;
  - line 21: "Pop out the Note Editor panel";
  - lines 22-26: Draw / Paint / Delete / Mute / Slice clicked at x = 150 / 180 / 210 / 240 / 270, y = 742.
  The sweep's first inventory (`inv-main.jsonl` line 88) had placed Paint at x=422 with the docked Inspector.
- **The sweep's after-shot** `shots/0022-after.png` (crop saved in this run's log folder as `0022-after-crop.png`)
  shows the floating INSPECTOR window ("Dock" / "Pin") spanning x = 0..480 down to y ~ 885. The pointer rests at
  (180, ~750) on empty Inspector space. The Note Editor's visible controls begin at x ~ 490 (instrument / NO SCALE /
  NOTES), so its tool buttons sit to the left, under the Inspector window.
- **Why the window covers the tools:** a popped-out panel is given a floor size of 480 x 320 and opens where the panel was
  docked (`src/gui/shell/ShellFloating.cpp:193-196`, `:248`, `:260`). The centre then reflows to the window's left
  edge, beneath it.
- **The same mechanism was accepted for the train-24 duplicate:** `gui-1030-main-paint` (Luna verdict ACCEPT-NOTE,
  branch `bugs/qwen-gui-1030-main-paint`, not yet in this base).

## Regression tests (`tests/src/gui/NoteEditorTest.cpp`)

- `aClickOnThePaintButtonWithNoClipOpenSelectsPaint`: this is the finding's repro, a fresh project with no clip. It
  sends a real `QTest::mouseClick` to the toolbar's Paint `ShellButton`, then checks that the editor's tool is Paint, the
  button is checked and no other tool in the group is still checked.
- `thePoppedOutInspectorCoversThePaintButtonTheSweepClicked`: this sets up the sweep's layout. A 1600x1000 `Shell` with
  every panel mounted, the note editor in `detail` and the Inspector detached. The test asserts that the Paint button's
  global centre lies inside the floating Inspector's frame. A click delivered to the button itself still selects Paint.

Both tests are deterministic: no sleeps or timers, with one `processEvents()` for the reflow.

With no product fix to revert, the negative control disables the cause the finding names: line 175's handler becomes a
no-op (`$LOGS/mutant.patch`). Both new tests then FAIL. Restoring line 175 makes them PASS again.

## Commands and results (LOGS = `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-paint.r2/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| Build (tests added, product untouched) | `cmake --build build --target zene NoteEditorTest -j4 > LOGS/build-1.log 2>&1` | 0 |
| ctest, product untouched | `cd build/tests && ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-1-green.log` | 0 (1/1 test, 100% passed) |
| Direct binary (the new functions ran) | `build/tests/NoteEditorTest > LOGS/test-2-direct.log` | 0 (43 passed, 0 failed; both new PASS) |
| Negative control: line 175 handler no-op | `sed` on line 175, `git diff > LOGS/mutant.patch`; build `LOGS/build-2-mutant.log` | build 0 |
| ctest on the mutant (RED) | `ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-3-mutant.log` | **8** (41 passed, 2 failed: both new tests, "Compared values are not the same") |
| Restore | `git apply -R LOGS/mutant.patch` (diff empty afterwards); build `LOGS/build-3-restored.log` | 0 |
| ctest restored (GREEN) | `ctest -R '^NoteEditorTest$' --output-on-failure > LOGS/test-4-restored.log` | **0** (1/1, 100% passed) |
| Gates, stock script | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log` | 1: `PermissionError` creating `<worktree>-feature-check` beside the worktree (write-jail); no gate ran |
| Gates, log-path-only copy | `python3 LOGS/feature_check_logpath.py <worktree> > LOGS/gates-2.log` | **0**: all 8 rows PASS |

The gates copy differs from the stock script in two lines only (`diff` recorded in this session). `ROOT` is pinned to
`~/.local/share/zene-bunny-fix/qwen`, so it reads the same baselines and FIX-BASE, and the log directory is
`LOGS/feature-check`. Rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology, doc-figures, complexity (no new item), file-length (no new item), RESULT: PASS.

`CCACHE_TEMPDIR` was pointed at `$TMPDIR` for the builds.

## Files committed

- `tests/src/gui/NoteEditorTest.cpp`: two tests, one helper, and the `Shell` / `ShellChrome` / `ShellLayout` includes.
- `tests/upstream-modifications.txt`: the existing `NoteEditorTest.cpp` line extended with this cluster's reason.
- `docs/qwen-fixes-2026-10/cluster-gui-1105-main-paint.md`: this report.

No product file changed. Nothing touches the audio thread, undo or control commands.

## Self-check

These answer `submit_check.py`'s questions.

- **Gates row FAIL in `submit_check.py`:** the check runs the stock `feature_check.py`. Inside this session's write-jail
  that script dies with `PermissionError` while creating `<worktree>-feature-check` beside the worktree
  (`LOGS/gates-1.log`), before any gate runs. The log-path-only copy runs the same gates against the same
  baselines and exits 0 with all 8 rows PASS (`LOGS/gates-2.log`). I did not edit the shared script. Outside the jail the
  stock invocation should behave the same, as it did for Luna on the gui-1030 duplicate.
- **The test FAILS without the change and PASSES with it:** there is no product change, because the verdict is
  FALSE-POSITIVE. The negative control removes the cause the finding names: `NoteEditorToolbar.cpp:175`'s handler
  becomes a no-op. ctest `-R '^NoteEditorTest$'` then gives EXIT=8, with both new tests FAILED and 41 passed. After
  `git apply -R LOGS/mutant.patch` and a rebuild it gives EXIT=0 (1/1; the direct binary had 43 passed).
- **Deterministic:** no sleeps, timers or wall-clock time. The tests use `QTest::mouseClick` on the button,
  `qWaitForWindowExposed`, and one `QCoreApplication::processEvents()` for the shell's synchronous reflow. The shell size
  is fixed at 1600x1000, and the floor size of the floating panel comes from `ShellFloating.cpp:193-196`.
- **At the cause, siblings checked:** no product code changed. The six other tool buttons (Draw, Delete, Mute, Slice,
  Select, Play) go through the same loop at `NoteEditorToolbar.cpp:165-177`. The keyboard path (`Key_6`) is already
  covered by `theNewViewFieldsAreDrivableOverTheSocket`, and the socket path `window.note_editor_set` ends in the same
  `setTool`. The sibling sweep findings for Delete/Mute/Slice (gui-1030) share this mechanism.
- **No collateral damage:** only tests and docs changed. No product file, saved data, limits or commands.
- **FALSE-POSITIVE evidence:** the wiring is at `NoteEditorToolbar.cpp:169-176`, with no clip guard. The sweep record
  `results.jsonl` lines 13 and 21-23 show the Inspector pop-out before the click at (180,742), while `inv-main.jsonl` line 88
  originally had Paint at x=422. `shots/0022-after.png` shows the pointer on the floating Inspector, which spans x 0..480.
  The floor size and in-place opening come from `ShellFloating.cpp:193-196, 248, 260`. The test
  `thePoppedOutInspectorCoversThePaintButtonTheSweepClicked` reproduces the overlap.
