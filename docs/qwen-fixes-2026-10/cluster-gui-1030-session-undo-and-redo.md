FIXED

# Cluster gui-1030-session-undo-and-redo: Redo shortcut stays dead after undo

Finding: `src/gui/MainWindow.cpp:1524` (historical; the Edit menu refresh is now `MainWindow.cpp:1535`). After an
undo, Ctrl+Shift+Z / Ctrl+Y did nothing until the Edit menu was reopened.

## Root cause (verified in the code)

- Edit > Undo and Edit > Redo are `QAction`s whose enabled state is set only by `updateUndoRedoButtons()`
  (`MainWindow.cpp:485`, connected to the Edit menu's `aboutToShow`).
- A disabled `QAction` does not fire its shortcut. So when Redo was greyed out (no redo step) and an undo then
  created one, Redo stayed disabled and its shortcut was dead.
- The same cache also hid the mirror case: an edit made after the menu last opened with an empty undo stack left Undo
  disabled, so Ctrl+Z did nothing.
- The stack also moves outside the menu: `control.undo` / `control.redo` unwind the journal directly
  (`src/core/ControlCommandsControl.cpp`, `ControlCommandsUndo.cpp`), and every edit pushes a step. None of those
  refreshed the actions.
- `ProjectJournal` is not a `QObject` and had no change signal (`include/ProjectJournal.h`).

## Fix (at the cause)

`ProjectJournal` now has one observer, told whenever either stack moves; the GUI uses it to keep the Undo/Redo
actions current.

- `include/ProjectJournal.h:376` declares `setStackObserver( std::function<void()> )`. The private
  `StackChangeNotice` RAII type (`include/ProjectJournal.h:498`) and `notifyStackChanged()` (`:509`) fire the
  observer on every return path.
- Sixteen stack-changing mutators open with `const StackChangeNotice stackNotice( this );`:
  - `src/core/ProjectJournal.cpp`: undo, redo, both `addJournalCheckPoint` overloads, `addJournalAction`,
    `mergeCheckpointsFrom`, `clearJournal` (7);
  - `src/core/ProjectJournalBounds.cpp`: `setMaxUndoStates`, `setMaxUndoBytes`, `coalesceTopStepIntoPrevious` (3);
  - `src/core/ProjectJournalStructural.cpp`: `addJournalStructure`, `discardTopSteps`, `discardLoadedSteps` (3);
  - `src/core/ProjectJournalSnapshots.cpp`: `pushLoadedStep`, `detachHistory`, `reattachHistory` (3).
  `mergeCheckpointsSince` delegates to `mergeCheckpointsFrom`, so it is covered.
- `src/core/ProjectJournal.cpp:227` defines `setStackObserver`.
- `src/gui/MainWindow.cpp:487-498` (`finalize()`) installs the observer next to the `aboutToShow` connection. It calls
  `updateUndoRedoButtons()` directly on the GUI thread; from another thread it queues the call, so the actions are
  only touched on the GUI thread.
- `src/gui/MainWindow.cpp:287` (`~MainWindow`) clears the observer before `Engine::destroy()`, so the journal never
  calls into a dead window.
- `src/gui/MainWindow.cpp:1535-1539`: `updateUndoRedoButtons()` is unchanged apart from its comment.

Behaviour otherwise unchanged: the same stack operations run, and the observer does only two `setEnabled` calls.
Nothing is saved or loaded differently, and no command is added (no `ControlCommandsSnapshot` change).

## Regression test

`tests/src/gui/MainWindowTest.cpp`, three new functions, using the real `MainWindow` and the real journal:

- `redoShortcutWorksAfterUndoWhileTheEditMenuIsClosed`: menu opened (Redo greyed), undo by the bound key, Redo
  enabled, Redo by its bound key runs the redo step.
- `undoShortcutWorksForAnEditMadeWhileTheEditMenuIsClosed`: menu opened with nothing to undo, an edit arrives,
  Undo enabled, bound key runs the undo step.
- `aStepUnwoundThroughTheJournalRefreshesRedo`: `journal->undo()` / `redo()` directly (the control-surface path)
  moves Redo's enabled state.

Determinism: no timers, sleeps or races. The window and the journal are driven synchronously: `QTest::keyClick`
dispatches through the event loop and the observer runs inline on the GUI thread. The menu is opened by invoking its
`aboutToShow`. The shortcut is taken from the action (`pressShortcut()`), so it does not depend on the platform's key
binding (the offscreen theme binds Redo to Ctrl+Y, not Ctrl+Shift+Z).

A first draft hard-coded Ctrl+Shift+Z. A diagnostic showed the offscreen theme binds Redo to Ctrl+Y, so the press
matched nothing (`redone 0`, `redoEnabled true`). The test now presses the bound key, and the product code was not
touched for it.

## Evidence (exact commands, exit codes)

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-session-undo-and-redo.r1/logs`

| Step | Command (run from the worktree / `build/tests` as noted) | EXIT |
| --- | --- | --- |
| RED, first draft, original code | `cmake --build build --target MainWindowTest` (`build-1-red.log`, ccache error) | 2 |
| build (ccache temp dir made writable) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target MainWindowTest -j4` (`build-2-red.log`) | 0 |
| RED, first draft, original code | `ctest -R '^MainWindowTest$' --output-on-failure` in `build/tests` (`test-1-red.log`) | 8 (3 failed, 4 passed) |
| GREEN, fix applied | `cmake --build build --target zene -j4` (`build-3-zene-green.log`) | 0 |
| GREEN, fix applied | `cmake --build build --target MainWindowTest -j4` (`build-4-test-green.log`) | 0 |
| GREEN | `ctest -R '^MainWindowTest$' --output-on-failure` (`test-2-green.log`) | 8 (diagnostic draft, see above) |
| diagnostic | rebuild + `ctest` (`test-3-diag.log`) | 8 (`redone 0`, `redoEnabled true`) |
| final test, fix applied | `cmake --build build --target MainWindowTest -j4` (`build-6-green.log`) | 0 |
| final test, fix applied | `ctest -R '^MainWindowTest$' --output-on-failure` (`test-4-green.log`) | 0 |

Step 4b, proof with the final test file (`fix.patch` = `git diff` of the six product files; 257 lines):

| Command | EXIT |
| --- | --- |
| `git diff -- include/ProjectJournal.h src/core/ProjectJournal.cpp src/core/ProjectJournalBounds.cpp src/core/ProjectJournalSnapshots.cpp src/core/ProjectJournalStructural.cpp src/gui/MainWindow.cpp > fix.patch && git apply --check -R fix.patch` | 0 (PATCH_OK) |
| `git apply -R fix.patch` (`proof-1-revert.exit`) | 0 |
| `cmake --build build --target zene -j4` (`build-7-red-proof.log`, fix reverted) | 0 |
| `cmake --build build --target MainWindowTest -j4` (same log) then `ctest -R '^MainWindowTest$' --output-on-failure` (`test-5-red-proof.log`) | **8 (RED)** |
| RED assertions: `MainWindowTest.cpp:129` `redoAction->isEnabled()` false; `:151` `undoAction->isEnabled()` false; `:169` `redoAction->isEnabled()` false | |
| `git apply fix.patch` (`proof-4-reapply.exit`) | 0 |
| `cmake --build build --target zene -j4` (`build-8-green-proof.log`) and `MainWindowTest` | 0 |
| `ctest -R '^MainWindowTest$' --output-on-failure` (`test-6-green-proof.log`) | **0 (GREEN)** |

The tree after re-apply is byte-identical to `fix.patch` (`cmp` returned no difference).

Test counts (test functions executed, from running `build/tests/MainWindowTest` directly with
`QT_QPA_PLATFORM=offscreen`, `test-9-binary.log`): `Totals: 7 passed, 0 failed`, exit 0. These are the three new
functions, the two original functions, and `initTestCase` / `cleanupTestCase`.

Family and related suites, final tree (`test-10-journal-family.log`, `build-11-journal-family.log`):

- `ctest -R '^(UndoBoundsTest|UndoHonestyStateTest|UndoHonestyTest|JournallingObjectRestoreTest|NoteCommandTransactionsTest|ControlNoteNoOpUndoTest|MainWindowTest|MainWindowGeometryTest|Lv2ViewBaseLifetimeTest)$' --output-on-failure`: **100% passed out of 9**, EXIT 0.
- `ctest -R 'MainWindow|Lv2ViewBaseLifetimeTest' -V` (`test-8-family.log`): 3 of 3, EXIT 0. The first run
  (`test-7-family.log`, EXIT 8) showed two binaries not yet built; they were built (`build-9-family.log`, exit 0) and
  pass.

Gates (see the self-check for the script issue):

| Gate | Command | EXIT |
| --- | --- | --- |
| feature_check.py (as specified) | `python3 .../feature_check.py <worktree>` (`gates-1.log`) | 1, crashed before any gate ran: `PermissionError` creating `<worktree>-feature-check` next to the worktree (outside the write-jail) |
| fork-sources | `bash tests/fork-sources-gate.sh` (`gate-fork-sources.log`) | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` (`gate-all-sources-reproduce.log`) | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` (`gate-no-upstream-regression.log`) | 0 |
| check-namespace | `tests/scripted/check-namespace` (`gate-check-namespace.log`) | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` (`gate-no-tautology.log`) | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` (`gate-doc-figures.log`) | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` (`gate-complexity.log`) | 1 (7 existing items, none in a file this change touches) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` (`gate-file-length.log`) | 1 (26 existing items, none in a file this change touches) |

The two ratchets are red on the train on purpose. I grepped their regression lines for every file this change
touches (`ProjectJournal*`, `MainWindow.cpp`, `MainWindowTest.cpp`): none. Under the script's own rule (new items in
changed files only) both would pass.

## Files committed

Product: `include/ProjectJournal.h`, `src/core/ProjectJournal.cpp`, `src/core/ProjectJournalBounds.cpp`,
`src/core/ProjectJournalSnapshots.cpp`, `src/core/ProjectJournalStructural.cpp`, `src/gui/MainWindow.cpp`.
Test: `tests/src/gui/MainWindowTest.cpp`. Register: `tests/upstream-modifications.txt` (three inherited files: the
header, `ProjectJournal.cpp` and `MainWindow.cpp`; the three `ProjectJournal*` sources are fork-new, already in
`tests/fork-sources.txt`; `MainWindowTest.cpp` is already a fork file). Report: this file.

## Self-check

- **Regression test fails with the change reverted and passes with it.** Yes. Reverted: `test-5-red-proof.log`, exit
  8, three failing assertions at `MainWindowTest.cpp:129`, `:151`, `:169`. Re-applied: `test-6-green-proof.log`, exit 0,
  7 of 7 functions pass.
- **Deterministic.** Yes. No timers, sleeps or races. `QTest::keyClick` is synchronous through the event loop, and the
  observer runs inline on the GUI thread. The menu is opened by invoking `aboutToShow`. The queued cross-thread branch
  (`MainWindow.cpp:493-496`) is not exercised by the test; the journal is GUI-thread only, so no test drives it.
- **Fix at the cause, every caller and sibling checked.**
  - Every public mutator that moves a stack is covered (16 notices; `mergeCheckpointsSince` goes through
    `mergeCheckpointsFrom`). `declineRedo`, `beginEdit`/`endEdit` and the snapshot pruning move no stack and are not
    instrumented.
  - Both paths into the stacks are covered: the Edit menu / shortcut path (`undo()`/`redo()` in `MainWindow`, and
    the test drives the bound keys), and the control path (`control.undo`/`control.redo` call the journal directly;
    `aStepUnwoundThroughTheJournalRefreshesRedo` covers it).
  - Both directions are covered: the Redo-after-undo case from the finding, and the Undo-after-edit mirror.
  - Not checked live in the GUI: a real key press in a running app on a real desktop theme. The test uses the offscreen
    theme's bindings.
- **No collateral damage.** The observer does two `setEnabled` calls and two emptiness reads per stack change; the
  journal's contents, serials, bounds, save format and load path are not touched. Journal-facing suites (9 tests, above)
  pass. The observer is cleared before `Engine::destroy()`, and it is only installed when `Engine::projectJournal()`
  exists, so a null journal is harmless.
- **Verdict FIXED (not ALREADY-FIXED or FALSE-POSITIVE)**: the defect reproduces on the original code (RED above).

## Gate caveat (for the completion check)

`feature_check.py` cannot run from this jail: it creates `<worktree>-feature-check` as a sibling of the worktree, which
is outside the allowed write area (`PermissionError`, `gates-1.log`). I did not modify the shared script. The same
eight gate commands were run by hand from the worktree, with the exit codes above. Those are the script's own
commands and its own ratchet filter, so the result should match, but the table itself was not produced by the script.
The completion check `submit_check.py` will still report the feature_check line as FAIL for this reason.
