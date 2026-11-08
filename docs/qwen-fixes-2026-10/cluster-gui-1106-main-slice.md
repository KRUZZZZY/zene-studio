FALSE-POSITIVE

# cluster gui-1106-main-slice - "Slice tool click leaves no visible selection feedback"

Branch `bugs/qwen-gui-1106-main-slice`, base `af010bc3cc`. Product code is unchanged; one guard test is added.

## Verdict and evidence

The Slice button works and shows its selection. The sweep's failing frame is a sweep artefact, not a product defect.

1. **The click path is correct.**
   - `src/gui/noteeditor/NoteEditorToolbar.cpp:161-177`: each tool is a checkable `QAction` in the exclusive `QActionGroup m_tools`, wrapped by `ShellButton` through `setDefaultAction` (`:113-119`). `QToolButton` mirrors the action's checked state, so the ShellButton's own `isChecked()` and the QActionGroup agree. Luna's "custom painter relies on its own checked state while NoteEditor updates the QActionGroup" does not hold.
   - `src/gui/noteeditor/NoteEditor.cpp:278-287` (`setTool`) checks the matching action.
   - `src/gui/shell/ShellChrome.cpp:141,164`: the painter reads `isChecked()` (fill `pal.control`, icon `QIcon::On`).
2. **A real left-click on the real button selects it and repaints it.** The new test `NoteEditorFlTest::aClickOnTheSliceButtonSelectsItAndShowsIt` (`tests/src/gui/NoteEditorFlTest.cpp`) mouse-clicks the toolbar's Slice `QToolButton`. It asserts `state().tool == Slice`, `isChecked()`, a different button grab than before the click, and that the toolbar stays visible. It passes on the unmodified product code (no fix exists to remove, so there is no RED/GREEN pair).
3. **A fresh Zene instance (this tree's `build/zene`, offscreen, `window.screenshot`) draws the toolbar fully.**
   - In the Arrange view with the bottom Note Editor tab active, the Slice (scissors) button is at about (512, 742). That is exactly the sweep's inventory coordinate (`inv-main.jsonl`: x=512, y=742, w=28, h=24).
   - In the Edit view the whole toolbar is also present.
4. **The sweep's own data shows a transient layout.**
   - The inventory had Slice at x=512, but the click was driven at x=270 (the sweep re-locates the control at click time; `sweep.py:362`). The control had moved.
   - The BEFORE and AFTER frames (`shots/0025-*.png`) show a half-built layout: a plain "Editor" header, no Arranger header, and a toolbar with no tool buttons, grid or quantise controls, only the fold, scale and view buttons.
   - The product's own layout at the same size is the one in point 3. AT-SPI said the button was "showing" at (270, 742), which in that frame is empty space left of the Editor panel.
   - The pinned install was older code than the branch tree, so the frame is not from the code under test.
   - Luna herself noted the absent toolbar is not explained by the tool-selection path.

If the sweep still reports this on a build from this branch, the next step is a click-time screenshot taken after the dock animation settles, not a change to ShellButton.

## Commands run (all logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-slice.r1/logs/`)

Environment note: ccache's temp dir is outside the write-jail (`build-1.log`, "Permission denied"), so builds ran with `CCACHE_DISABLE=1`.

| Step | Command | EXIT |
| --- | --- | --- |
| build | `CCACHE_DISABLE=1 cmake --build build --target NoteEditorFlTest -j4 > build-6.log` | 0 |
| ctest, family (the only test in it) | `cd build/tests && ctest -R '^NoteEditorFlTest$' --output-on-failure > test-4.log` (1 test run, passed) | 0 |
| direct run, 20 tests | `QT_QPA_PLATFORM=offscreen ./NoteEditorFlTest > test-5.log` (`Totals: 20 passed, 0 failed`; the new test passed) | 0 |
| live app | `python3 $TMPDIR/shot/run.py` (starts `build/zene`, `window.set_view`, `window.screenshot`) | 0 |

No RED/GREEN or `fix.patch` revert was run, because the verdict is FALSE-POSITIVE and no product file changed.

### Gates

`feature_check.py` could not run: it creates `<worktree>-feature-check/` beside the worktree, outside the write-jail (`PermissionError: [Errno 13]`, `gates-1.log`, exit 1). Its gates were run by hand with the same commands:

| Gate | EXIT |
| --- | --- |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `bash tests/complexity-gate.sh --check` | 1 (red on the train on purpose; 0 lines mention `NoteEditorFlTest`) |
| `bash tests/file-length-gate.sh --check` | 1 (red on the train on purpose; 0 lines mention `NoteEditorFlTest`) |

Complexity and file-length were not compared against the cached base listing. The change is one 22-line test function with no branches.

## Files committed

- `tests/src/gui/NoteEditorFlTest.cpp` (+22: one test, one `#include <QToolButton>`)
- `docs/qwen-fixes-2026-10/cluster-gui-1106-main-slice.md`
