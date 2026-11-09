Verdict: FALSE-POSITIVE (all four; no product change)

gui-1030-main-paint: FALSE-POSITIVE
gui-1030-main-delete: FALSE-POSITIVE
gui-1030-main-mute: FALSE-POSITIVE
gui-1030-main-slice: FALSE-POSITIVE

## Why these four are one cause, and not a bug in `NoteEditorToolbar.cpp`

Owner routing asked not to assume a shared cause merely from a shared file, so each tool was reproduced on its own. The four
reports do share one cause, but it is in the sweep's recorded layout, not in the toolbar.

1. **The sweep popped the Inspector out first.** `terra-pilot/buttons/runs/m0-train-24/results.jsonl` line 12 clicks "Keep this panel
   open in every view" on the Inspector and line 13 clicks "Pop out the Inspector panel" (x=179, y=99), 9 steps before Paint. Its
   own "after" shots (`shots/0022-pair.png` .. `0025-pair.png`) show the Inspector with a **Dock** button (the floating state)
   over x=0..480, and the arranger toolbar cut off at x=480.
2. **A popped-out panel is at least 480 px wide.** `Shell::floatSize` is `frame->size().expandedTo(QSize(480, 320))`
   (`src/gui/shell/ShellFloating.cpp:193-196`), and the window opens at the panel's old position (`ShellFloating.cpp:248,260`).
   The 240 px Inspector therefore becomes a 480 px top-level window over x=0..480, and the centre reflows to x=0.
3. **That reflow puts the tools exactly where the sweep clicked.** In this build, `window.detach {panel: inspector}` followed
   by `window.screenshot` (scratch `float.py` + `b.png` in the run log folder) shows Draw/Paint/Delete/Mute/Slice at x=150/180/210/240/270, y=742. The pilot's
   clicks are Paint (180,742), Delete (210,742), Mute (240,742) and Slice (270,742): the same points, now under the floating
   Inspector window. The mouse in the pilot's "after" shots sits on empty Inspector space. The clicks never reached the
   buttons, so no `QAction::triggered`/`setTool` ever ran. That also explains the "interface layout is disrupted" note on Slice.
4. **A fresh layout works.** Without the pop-out the same build lays the toolbar out normally and the tools are reachable
   (scratch `a.png`: Draw 392, Paint 422, Delete 452, Mute 482, Slice 512).
5. **The wiring itself is sound.** `NoteEditorToolbar.cpp:165-177` makes one checkable, exclusive `QAction` per tool in
   `m_tools`, connects `triggered` to `setTool(tool)`, and `NoteEditor::setTool` (`src/gui/noteeditor/NoteEditor.cpp:278-286`)
   stores the tool and checks the matching action; `ShellButton` shows the checked `defaultAction`.
   Luna's own root cause already said the source "connects the action to setTool, which checks it".

Not verified: the sweep's Select/Play coordinates (x=542/572) come from a different layout snapshot than Draw..Slice, so I make no
claim about why those two were not reported. They are not in this batch.

## Tests (all in the existing `tests/src/gui/NoteEditorTest.cpp`; one scenario per bug)

| Bug | Test slot (real left click on the toolbar button, not a `setTool` call) |
| --- | --- |
| gui-1030-main-paint | `aClickOnThePaintButtonSelectsThePaintTool` |
| gui-1030-main-delete | `aClickOnTheDeleteButtonSelectsTheDeleteTool` (Delete = `NoteTool::Erase`) |
| gui-1030-main-mute | `aClickOnTheMuteButtonSelectsTheMuteTool` |
| gui-1030-main-slice | `aClickOnTheSliceButtonSelectsTheSliceTool` |
| all four (cause) | `thePoppedOutInspectorCoversTheToolbarsFirstToolsNotTheEditorsClicks`: Shell at 1600x1000 with a real `NoteEditor` in `detail`, pops the Inspector out, asserts the floating window is >= 480 px wide and its frame contains the centre of the Paint, Delete, Mute and Slice buttons, then clicks Paint at the button and checks the tool |

Each click test starts from Draw, asserts the button is visible, enabled and unchecked, clicks it, then asserts the editor's tool, that
the button shows checked and that no other button of the group does. No sleeps or timing: events are delivered synchronously.

Because the verdict is FALSE-POSITIVE there is no product fix, so there is no RED-without-fix to record. As the substitute I ran a
mutation check instead: temporarily changing line 175 to `(void)tool;` (the tool never selected) turns every one of these five tests red.

## Evidence

| Evidence | Command (cwd) | Result |
| --- | --- | --- |
| Build the test (first try) | `cmake --build build --target NoteEditorTest -j4 > logs/build-1.log 2>&1` | EXIT=2: `ccache: error ... Permission denied` on the sandbox's ccache tmp dir, not a compile error |
| Build with a writable ccache tmp | `CCACHE_TEMPDIR=$TMPDIR/cc-r1 cmake --build build --target NoteEditorTest -j4 > logs/build-4.log 2>&1` | EXIT=0 |
| ctest, final test | `ctest -R '^NoteEditorTest$' --output-on-failure > logs/test-3.log 2>&1` (from `build/tests`) | EXIT=0, 1/1 passed (the ctest entry is the whole binary) |
| The binary directly, to count slots | `QT_QPA_PLATFORM=offscreen ./NoteEditorTest > logs/test-4.log 2>&1` (from `build/tests`) | EXIT=0, 46 passed, 0 failed; all five new slots PASS |
| Mutation (line 175 made a no-op), build | `cmake --build build --target NoteEditorTest -j4 > logs/build-5-mutant.log 2>&1` | EXIT=0 |
| Mutation, run | `QT_QPA_PLATFORM=offscreen ./NoteEditorTest > logs/test-5-mutant.log 2>&1` | **EXIT=5, 5 failed** (the four click tests and the overlap test), 41 passed |
| Restore `git checkout -- src/gui/noteeditor/NoteEditorToolbar.cpp`, rebuild, run | `... > logs/build-6-restored.log`, `./NoteEditorTest > logs/test-6-restored.log` | EXIT=0, EXIT=0, 46 passed, 0 failed; `git status` shows only the test file |
| Earlier overlap-test iterations | `logs/test-1.log`, `logs/test-2.log` | EXIT=8: my first version also asserted Select/Play were clear of the floating window; they are not in this test shell (Select at x=301), so I removed that claim. The four tools in the batch were covered in every run |
| Gates | `python3 logs/scratch/feature_check_local.py <worktree> > logs/gates-2.log 2>&1` | EXIT=0, all 8 rows PASS (see the note below) |

Gate note: the stock `feature_check.py` exits 1 here (`logs/gates-1.log`, a `PermissionError` creating
`zene-qwen-gui-1030-main-paint-feature-check/` beside the worktree, which the write-jail forbids). I ran a copy that differs in exactly two lines: `ROOT`
pinned to the real script directory and the log directory moved into this run's log folder (`diff` was printed before the run). The gate logic, FIX-BASE and
cached baselines are unchanged.

Registration: `tests/src/gui/NoteEditorTest.cpp` was already in `tests/upstream-modifications.txt`; I appended this cluster to its reason line.
No new files, no new commands, no audio-thread code touched.

Files committed: `tests/src/gui/NoteEditorTest.cpp`, `tests/upstream-modifications.txt`, this report.

## Self-check

`submit_check.py gui-1030-main-paint` (`logs/submit-1.log`, EXIT=1): commits, clean tree, report and tests rows PASS. The one FAIL is
`gates (feature_check.py)`, and it is environmental, not a gate result: the stock script raises `PermissionError` creating
`../zene-qwen-gui-1030-main-paint-feature-check/` (`logs/gates-1.log`), a directory outside this run's write-jail. The identical gate
logic run through the two-line copy passes all 8 rows, EXIT=0 (`logs/gates-2.log`; complexity and file-length: "no new item in your files").
That row will stay FAIL in any run confined to this jail; I did not edit the shared script.

- **Test fails with the product change reverted, passes with it.** There is no product change (FALSE-POSITIVE), so there is nothing to revert.
  The equivalent proof is the mutation: with line 175 made a no-op the five new tests fail (`logs/test-5-mutant.log`, EXIT=5, 5 failed); restored, they
  pass (`logs/test-6-restored.log`, EXIT=0, 46 passed). I did not get a RED against unmodified production code, because the behaviour is correct there.
- **Deterministic.** No sleeps or timing. Clicks are `QTest::mouseClick` delivered synchronously to the button; the overlap test reads widget and window
  geometry after `detachPanel` (one `qWait(0)` only flushes the reflow event, it waits on no condition). The Shell is a fixed 1600x1000 in the offscreen platform.
- **Fix at the cause and complete.** The cause is outside the toolbar: the sweep's click points sat under the Inspector's own floating window
  (`ShellFloating.cpp:193-196, 248, 260`). Siblings checked: the other tools go through the same loop at `NoteEditorToolbar.cpp:165-177` and the key path
  (`keyPressEvent`, `toolForKey`) calls the same `setTool`; nothing differs per tool, so there is no per-tool defect to fix.
- **No collateral damage.** No product, saved-data or limit change; test-only plus one registration reason line.
- **FALSE-POSITIVE contract and code.** `NoteEditorToolbar.cpp:165-177` (checkable exclusive actions, `triggered` -> `setTool`), `NoteEditor.cpp:278-286`
  (`setTool` checks the action), `ShellFloating.cpp:193-196` (480x320 minimum float size), `ShellFloating.cpp:248,260` (opens at the panel's old position),
  pilot `results.jsonl` lines 12-13 (the pop-out) and `shots/0022..0025-pair.png`. Unverified: why the sweep's Select/Play x differ (see above); not in this batch.
