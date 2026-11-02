FALSE-POSITIVE

# cluster gui-1030-main - "Proportional snap toggle slots never run"

Finding (Luna, 050/m0-train-24): `SongEditor.cpp:1038-1039` connect `QAction::triggered()` (old-style
`SIGNAL(triggered())`) to `SongEditor::toggleProportionalSnap()` / `SongEditorWindow::updateSnapLabel()`;
Luna's root cause is "QAction exposes triggered(bool), so the no-argument connection fails".

## Decision: FALSE-POSITIVE (the connections work; behaviour is as intended)

Evidence:

- `QAction::triggered` is declared `void triggered(bool checked = false);` (`/usr/include/qt6/QtGui/qaction.h:225`).
  moc generates a *cloned* signal for every default argument, so both `triggered(bool)` and `triggered()` exist
  as signals, and `SIGNAL(triggered())` is a valid old-style connection. A slot with fewer arguments than the
  signal is also always legal. The connect calls at `src/gui/editors/SongEditor.cpp:1038-1039` therefore succeed.
  The sibling toolbar actions in the same function (`:958-960`, `:985-987`, `:1001-1002`) use the identical
  idiom and are not reported broken.
- Runtime proof, not just reading: the new regression
  `TrackFolderMenuTest::proportionalSnapActionTogglesSnapAndReadout` (`tests/src/gui/TrackFolderMenuTest.cpp`)
  finds the real "Toggle proportional snap on/off" action in the live song editor toolbar and triggers it.
  On the UNCHANGED product code it passes: `proportionalSnapChanged` fires once (so
  `SongEditor::toggleProportionalSnap`, `SongEditor.cpp:387-393`, ran), the action reports `isChecked()`, and
  the readout label reads `"Snap: " + getSnapSizeString()` (`updateSnapLabel`, `SongEditor.cpp:1058-1069`);
  a second trigger unchecks the action, fires the signal again and clears the readout.
- The test is not vacuous: with `SIGNAL(triggered())` on lines 1038-1039 deliberately mutated to a signal that
  does not exist, the same test FAILS (`Compared values are not the same`, `TrackFolderMenuTest.cpp:244`,
  the `proportionalSnapChanged` count). The mutation was reverted with `git checkout -- src/gui/editors/SongEditor.cpp`;
  no product file differs from the branch base.
- Luna's observed symptom ("a horizontal slider appears, but no checked control or Snap readout") is a
  mis-identification by the GUI sweep: the horizontal slider is the zoom slider (`SongEditor.cpp:1010-1021`), and
  the proportional-snap control is an icon-only checkable toolbar button (`:1034-1044`) whose tooltip is empty in
  the sweep's accessibility dump ("check box '' ... tooltip ''"); the "Snap:" readout only exists after the
  button is ticked. No product behaviour was changed ("a triaged finding is not a license to change intended behavior").

Only a regression test was added (it passes before and after, so it is a guard, not a RED/GREEN fix test; there
is no fix to prove with step 4b - the mutation run above plays that role).

## Commands run (all in the worktree; logs in `.../runs/gui-1030-main.r1/logs/`)

The inherited ccache temp dir is not writable from this jail, so builds ran with
`CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-gui1030` (first attempt `build-1.log` EXIT=2: ccache "Permission denied").

| step | command | EXIT |
| --- | --- | --- |
| build test | `cmake --build build --target TrackFolderMenuTest -j4 > build-3.log 2>&1` | 0 |
| test on original product code (first run, test's own label lookup was wrong: `test-1.log`) | `cd build/tests && ctest -R '^TrackFolderMenuTest$' --output-on-failure > test-1.log` | 8 (test bug, fixed) |
| test on original product code | `ctest -R '^TrackFolderMenuTest$' --output-on-failure > test-2.log` | 0 (1/1 test, new slot PASS) |
| direct run of the new slot | `./TrackFolderMenuTest proportionalSnapActionTogglesSnapAndReadout` | PASS (3 passed, 0 failed) |
| MUTANT (lines 1038-1039 `triggeredNever()`), build | `cmake --build build --target TrackFolderMenuTest -j4 > build-4-mutant.log 2>&1` | 0 |
| MUTANT, test | `ctest -R '^TrackFolderMenuTest$' --output-on-failure > test-4-mutant.log` | 8 (new slot FAIL at `TrackFolderMenuTest.cpp:244`; 5 passed, 1 failed) |
| restore | `git checkout -- src/gui/editors/SongEditor.cpp` | 0 |
| rebuild restored | `cmake --build build --target zene TrackFolderMenuTest -j4 > build-5.log 2>&1` | 0 |
| test restored | `ctest -R '^TrackFolderMenuTest$' --output-on-failure > test-5.log` | 0 (1/1) |
| family | `cmake --build build --target VisibilitySetMenuTest TrackViewDeferredLifetimeTest -j4 > build-6.log`; `ctest -R 'VisibilitySetMenuTest\|TrackViewDeferredLifetimeTest\|TrackFolderMenuTest' > test-7.log` | build 0, ctest 0 (3/3 passed) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log` | 1 - NOT RUN: `PermissionError` creating `zene-qwen-gui-1030-main-feature-check` (sibling dir outside the write jail); no gate result exists |

Files committed: `docs/qwen-fixes-2026-10/cluster-gui-1030-main.md`, `tests/src/gui/TrackFolderMenuTest.cpp`
(+`#include <QLabel>`, +1 test slot), `tests/upstream-modifications.txt` (existing TrackFolderMenuTest line extended).
No new files (nothing to add to `tests/fork-sources.txt`), no header changed, no new control command, nothing on
an audio path touched.

## Self-check

(see below, filled after `submit_check.py`)
