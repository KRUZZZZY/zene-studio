FALSE-POSITIVE

# cluster gui-1105-main-mute - "Mute tool click leaves the toolbar selection unchanged"

## Verdict and evidence

The product code selects the Mute tool correctly on a real mouse click. The sweep clicked the wrong pixel.

1. **The code path is correct.** `src/gui/noteeditor/NoteEditorToolbar.cpp:165-178` makes one checkable `QAction` per tool in an
   exclusive `QActionGroup` and connects `QAction::triggered` to `setTool(tool)`. `src/gui/noteeditor/NoteEditor.cpp:278-287`
   sets `m_state.tool` and checks the matching action. `NoteTool::Mute` is wired at `NoteEditorToolbar.cpp:73`.
   Luna's root cause itself says "the source setter updates the tool and its checked action".
2. **A real click works on the unmodified code.** The new test `NoteEditorFlTest::clickingAToolButtonSelectsThatTool`
   (`tests/src/gui/NoteEditorFlTest.cpp`) finds each tool's toolbar button (`QToolButton` whose `defaultAction()` is the
   tool action), sends `QTest::mouseClick(button, Qt::LeftButton)`, and asserts `state().tool`, the action's and the button's checked state.
   It covers Mute, Delete, Slice, Select, Play, Paint and Draw. It passes with no product change.
3. **The sweep clicked a stale position.** In the sweep run (`terra-pilot/buttons/runs/m0-train-25`) `controls.jsonl` places the
   Note Editor tool buttons at x = Draw 392, Paint 422, Delete 452, **Mute 482**, Slice 512, Select 542, Play 572 (y 742, 28x24).
   `results.jsonl` records the clicks at Draw 150, Paint 180, Delete 210, **Mute 240**, Slice 270 and then at Select 542 and Play 572.
   The first five clicks are 242 px left of their buttons, and only Select and Play were clicked in the right place. The click
   missed the Mute button, which is why the "after" screenshot is unchanged. The sibling findings for Delete and Slice
   (same cluster family, `triage-7.md`) have the same cause.

No product file was changed. The only change is the added regression test, which keeps real-click tool selection covered. Before this
the suite only triggered the actions or pressed the letters.

## Commands run (the shell had `CCACHE_DISABLE=1`: ccache's temp dir under `~/.local/share/zene-bunny-fix/dsh` is not writable in this jail)

| Step | Command | Exit |
| --- | --- | --- |
| First build attempt (ccache temp dir denied) | `cmake --build build --target NoteEditorFlTest -j4 > LOGS/build-1.log 2>&1` | 2 |
| Build | `cmake --build build --target NoteEditorFlTest -j4 > LOGS/build-2.log 2>&1` | 0 |
| Family run (from `build/tests`) | `ctest -R NoteEditorFlTest --output-on-failure > LOGS/test-1.log 2>&1` | 0 (1 of 1 passed) |
| New test alone | `./NoteEditorFlTest clickingAToolButtonSelectsThatTool > LOGS/test-2.log 2>&1` | 0 (3 passed, 0 failed) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 - `PermissionError`, see below |

`LOGS` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-mute.r1/logs`.

There is no RED run and no `fix.patch` revert step, because there is no product fix: the test passes on the original code, which is the evidence.

**Not run:** `feature_check.py` could not run. It creates `<worktree>-feature-check` next to the worktree, outside the allowed
write area (`PermissionError: [Errno 13]` at `feature_check.py:71`; log in `LOGS/gates-1.log`). No product file changed, so the
complexity and file-length gates cannot report anything new. This was not verified by the script.

## Files committed

- `tests/src/gui/NoteEditorFlTest.cpp` (one test and the `QToolButton` include)
- `docs/qwen-fixes-2026-10/cluster-gui-1105-main-mute.md`

## Self-check

- **Regression test fails with the change reverted and passes with it:** not applicable. The verdict is FALSE-POSITIVE and no product
  file changed. The test is characterisation coverage: it passes on the unmodified code (`ctest -R NoteEditorFlTest` EXIT=0,
  `./NoteEditorFlTest clickingAToolButtonSelectsThatTool` EXIT=0, 3 passed), which is the evidence that the bug does not exist.
- **Deterministic:** yes. It uses `QTest::mouseClick` on the real toolbar buttons of an offscreen `NoteEditor` and compares
  state. It has no timers, sleeps or waits beyond the fixture's existing `qWaitForWindowExposed`.
- **Fix at the cause, siblings checked:** no fix is needed. All seven tools are built in one loop (`NoteEditorToolbar.cpp:165-177`),
  and the test clicks all seven. The Delete and Slice sibling findings have the same sweep-coordinate cause, and every tool is covered
  by the same test.
- **No collateral damage:** nothing in product code, saved data or commands changed.
- **Code and contract showing FALSE-POSITIVE:** `NoteEditorToolbar.cpp:165-178` (action per tool, `triggered` -> `setTool`),
  `NoteEditor.cpp:278-287` (`setTool` sets the state and checks the action). Sweep evidence: `controls.jsonl` Mute button at x=482,
  `results.jsonl` click at x=240 (see the evidence section above).
- **Gates:** `feature_check.py` exit 1 because it cannot create `<worktree>-feature-check` in this write-jailed run (PermissionError).
  The check was not run. The change is one test function and a report, with no product code.
