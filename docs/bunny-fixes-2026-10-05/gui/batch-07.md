# GUI batch-07 — Space Bunny fixer lane

Six findings triaged against `bugs/bunny-gui-2026-10-05` at `43ab9a8e8`. Two were real and are
fixed; one turned out to be already-correct once tested over the real application; three were
hypotheses that the code does not support.

### Cluster 5098 — Snapshot Recall menu stays stale after storing a snapshot

Verdict: FALSE-POSITIVE

Evidence: `src/gui/LibraryCommandMenus.cpp:185-199` builds the recall list from
`mixer.snapshot_list`, but that menu is not built once and kept: `src/gui/shell/ShellChrome.cpp:229-232`
connects `QMenu::aboutToShow` to `clear()` + `ShellCommandMenus::populateProject`, so the recall
entries are rebuilt from the current snapshot list on every single open. Storing a snapshot changes
`mixer.snapshot_list`, and the next open repopulates from it.

Commit: none
Test: none
Red/green: none

### Cluster 4188 — Loading the default document type can leave the wrong view open

Verdict: SKIPPED-SCOPE

Evidence: the guard is real at `src/gui/LibraryCommandMenus.cpp:352` — the `projectLoaded` handler
skips `applyDocumentType` when the loaded type is `documentTypes().first()`. But the default type is
not written to the project file at all (`src/core/ProjectInfo.cpp:104` writes `type` only when it
differs from the first type, and `:123-125` resets to it on `clear()`), so "loaded the default type"
and "the file said nothing about the type" are the same case, and the surrounding legacy behaviour
depends on staying put. Removing the guard changes what opening a default-typed project does to the
piano-roll arrangement, which is a design/legacy behaviour change rather than a defect repair. The
guard also arrives with document types themselves (`c21169dc1`), not as a later regression.

Commit: none
Test: none
Red/green: none

### Cluster 1456 — Deleting a custom stylesheet stops hot reload permanently

Verdict: FIXED

Evidence: `src/gui/LmmsStyle.cpp:161-182` watched only the stylesheet file and re-added that watch
solely inside the branch handling a change to an *existing* file. `QFileSystemWatcher` drops a
deleted file's path, so once `style.css` was removed nothing re-armed the watch and a recreated
stylesheet was never picked up again for the rest of the session. The containing directory is now
watched as well, and the file watch is re-armed whenever it is missing; the reload body moved into
`LmmsStyle::reloadCustomStyle()` so the constructor's complexity does not grow.

Commit: `43ab9a8e8`
Test: `LmmsStyleTest::recreatingADeletedStylesheetKeepsHotReloading`
Red/green: red `cmake --build build --target LmmsStyleTest` EXIT=0,
`cd build/tests && ctest -R '^LmmsStyleTest$' --output-on-failure` EXIT=8 (recreated stylesheet's
marker never appeared in `qApp->styleSheet()`); green
`cmake --build build --target zene LmmsStyleTest` EXIT=0, same ctest EXIT=0, and
`ctest -R '^(MainWindowTest|WcagContrastTest|AccessibilityWalkTest|VcaStripLifetimeTest|MixerModesTest|ShortcutsCommandsTest|HelpAccessTest|LmmsStyleTest)$' --output-on-failure`
EXIT=0 (8/8). Gates: `tests/complexity-gate.sh --check` EXIT=0, `tests/file-length-gate.sh --check` EXIT=0.

### Cluster 5118 — Hiding a floating editor leaves its window frame open

Verdict: FALSE-POSITIVE

Evidence: the report assumes the visible top-level window is the `QMdiSubWindow` frame that
`toggleWindow` fails to hide. It is not: these editors float out of the workspace, and
`SubWindow::floatOut()` (`src/gui/SubWindow.cpp:368-392`) hides the workspace frame on purpose
(`QMdiSubWindow::setVisible(false)`, line 376) and gives the editor content `Qt::Window` so the
*content itself* becomes the top-level window. `src/gui/MainWindow.cpp:1149-1165` looks up that
frame via `window->parentWidget()`, and `window->hide()` hides the widget that is itself the window.
Written as a regression over the real application, the test passes against unmodified product code:
after toggling the controller rack off, neither the editor nor its frame is visible. An earlier
attempt to "fix" this by adding `parent->hide()` changed nothing observable and was reverted.

Commit: none (the covering test is kept in `tests/src/gui/MainWindowTest.cpp`, added with `bca41393b`)
Test: `MainWindowTest::hidingAFloatingEditorTakesItsWindowFrameWithIt` (passes unmodified)
Red/green: none — the regression could not be made to fail for the stated reason, which is what
rules the finding out.

### Cluster 5119 — Unattended first run permanently skips setup

Verdict: FIXED

Evidence: `src/gui/MainWindow.cpp:555-557` wrote `app/configured` *before* the `interactive` check at
`:559`, so an unattended launch skipped the `SetupDialog` but still persisted "configured",
suppressing the dialog on the next interactive launch even though the user had never been asked.
The setting is now written only after the dialog has actually run.

Commit: `bca41393b`
Test: `MainWindowTest::anUnattendedFirstRunDoesNotRecordTheSetupAsDone`
Red/green: red `cmake --build build --target zene MainWindowTest` EXIT=0,
`cd build/tests && ctest -R '^MainWindowTest$' --output-on-failure` EXIT=8 (`app/configured` Actual
1, Expected 0); green same commands EXIT=0, and
`ctest -R '^(ShortcutsCommandsTest|ArrangerTimelineTest|VcaStripLifetimeTest|VisibilitySetMenuTest|TrackFolderMenuTest|HelpAccessTest|MixerModesTest|MixerSendsOnFadersTest|AccessibilityWalkTest|CaptureMidiMenuTest|CollabPanelTest|MainWindowTest)$' --output-on-failure`
EXIT=0 (12/12, every test executable that boots the real GUI). Gates: `tests/complexity-gate.sh --check`
EXIT=0, `tests/file-length-gate.sh --check` EXIT=0.

### Cluster 4209 — Audio diagnostic labels the requested device as the backend

Verdict: FALSE-POSITIVE

Evidence: the diagnostic at `src/gui/MainWindow.cpp:606-610` deliberately reports the configured
request and the active device as separate fields: `backend="%s"` is fed `requested`
(`audioengine/audiodev`) and `device="%s"` is fed `AudioEngine::audioDevName()`, the device the
engine is actually on. That contract is asserted on purpose by
`tests/control-headless-no-audio-device.py:110-115`, which fails the run if the line does not name
the configured backend or does not name the device the engine is using. The field is labelled for
what it holds, and the finding asks for a behaviour the test suite deliberately pins.

Commit: none
Test: none
Red/green: none

## Leftovers

None. Two new test files were added (`tests/src/gui/MainWindowTest.cpp`, `tests/src/gui/LmmsStyleTest.cpp`),
registered in both `tests/CMakeLists.txt` and `tests/fork-sources.txt`. Cluster 4188 is left
unchanged on purpose and is the only finding where a follow-up design decision — not a code change —
would settle the report.