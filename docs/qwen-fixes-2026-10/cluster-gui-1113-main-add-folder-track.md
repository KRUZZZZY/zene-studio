FIXED

# gui-1113-main-add-folder-track - a folder row shows its track name

## Verdict and evidence

REAL. `TrackFolder::createView` built a bare `gui::TrackView` and added only the
summary lane, so the folder's row had no `TrackLabelButton`; every other track
type creates one in its own view constructor (for example
`src/gui/tracks/NeutralTrackView.cpp:41`, `src/gui/tracks/AutomationTrackView.cpp:44`,
`src/gui/tracks/SampleTrackView.cpp:69`). A folder's row therefore had no widget
that draws `Track::name()`.

The fix is at the cause, in `src/tracks/TrackFolder.cpp:174-183`: the view now
creates the ordinary header label (`TrackLabelButton`, `src/tracks/TrackFolder.cpp:178-180`)
before the summary lane. The folder glyph itself is deliberately not added:
`src/gui/TrackIcons.cpp:36-42` has no type glyph for a folder, and
`src/gui/TrackIcons.cpp:74-77` explicitly restores "no icon" for a type without
one. The user-visible path is the song editor's "Add folder track" action
(`src/gui/editors/SongEditor.cpp:988-994`) through `track.add`, then
`TrackContainerView::createView` (`src/gui/editors/TrackContainerView.cpp:448`).

The regression is in `tests/src/gui/TrackFolderMenuTest.cpp:132-138`: it uses
the real toolbar action, finds the new folder's row, and requires a direct
`TrackLabelButton` child of its settings widget whose text is the folder's
display name. Without the fix it fails at the missing label.

## Test matrix

- Normal path: "Add folder track" creates a folder named "New folder"; its row
  has a header label whose text equals `folder->displayName()`.
- Failure path: the same test asserts the label EXISTS before checking its text,
  so a row with no label fails with "the folder's row has no track label" (the
  observed bug).
- Reconfiguration: the label is the ordinary `TrackLabelButton`, so the existing
  `nameChanged` connection updates the text on rename; folder indentation uses
  the existing `TrackView::updateFolderIndent` path for a settings widget without
  a layout (`src/gui/tracks/TrackView.cpp:260-274`).
- Audio thread: none. This is view construction only; no process/render, handle
  or mixer-channel code changed. No undo journal or control command changed.
- Platforms: Qt widgets only, offscreen; the test is registered as a normal GUI
  test in `tests/CMakeLists.txt` (`TrackFolderMenuTest`, offscreen).
- Save data: no serialization changed; an old project loads the same and simply
  gains the row header it should have had.

## Commands and exit codes

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1113-main-add-folder-track.r1/logs`.

RED (test added, product code unmodified):

```
cmake --build build --target TrackFolderMenuTest -j"$(nproc)" > LOGS/build-2.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^TrackFolderMenuTest$' --output-on-failure > LOGS/test-1-red.log 2>&1
# EXIT=8
```

The log records: `FAIL! : ... 'folderLabel != nullptr' returned FALSE. (the folder's row has no track label)`.

GREEN (fix applied):

```
cmake --build build --target zene TrackFolderMenuTest -j"$(nproc)" > LOGS/build-3-green.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^TrackFolderMenuTest$' --output-on-failure > LOGS/test-2-green.log 2>&1
# EXIT=0  (100% tests passed out of 1)
```

4b proof that the test catches the bug:

```
git diff -- src/tracks/TrackFolder.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch
# EXIT=0
cmake --build build --target TrackFolderMenuTest -j"$(nproc)" > LOGS/build-4-reverted.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^TrackFolderMenuTest$' --output-on-failure > LOGS/test-3-reverted-red.log 2>&1
# EXIT=8
git apply LOGS/fix.patch
# EXIT=0
cmake --build build --target zene TrackFolderMenuTest -j"$(nproc)" > LOGS/build-5-restored.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^TrackFolderMenuTest$' --output-on-failure > LOGS/test-4-restored-green.log 2>&1
# EXIT=0
```

Affected family (all built first, `LOGS/build-6-family.log` EXIT=0):

```
cd build/tests && ctest -R '^(TrackFolderMenuTest|TrackFolderTest|FolderSummaryLaneTest|MixerSendsOnFadersTest|TrackIconsLevelBarTest)$' --output-on-failure > LOGS/test-5-family.log 2>&1
# EXIT=0  (5/5 tests passed)
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log 2>&1
# EXIT=0
```

Result: PASS on all eight gates; complexity and file-length report "no new item
in your files".

## Files committed

- `src/tracks/TrackFolder.cpp` - the fix (fork-new file, already registered in
  `tests/fork-sources.txt`).
- `tests/src/gui/TrackFolderMenuTest.cpp` - the regression (inherited file,
  reason appended in `tests/upstream-modifications.txt`).
- `tests/upstream-modifications.txt` - reason for the changed test.
- `docs/qwen-fixes-2026-10/cluster-gui-1113-main-add-folder-track.md` - this report.

## Self-check

- **Regression fails with the product change reverted and passes with it
  (exit codes).** Yes: RED `test-1-red.log` EXIT=8 and reverted `test-3-reverted-red.log`
  EXIT=8; GREEN `test-2-green.log` EXIT=0 and restored `test-4-restored-green.log`
  EXIT=0. Both RED runs stop at `'folderLabel != nullptr' returned FALSE`.
- **Deterministic: no wall-clock timing, sleeps or races (seam or injected
  values).** No sleeps or timestamps. The test drives the real toolbar action
  and inspects the real widget tree through the existing `viewOf()` seam; the
  only wait is `QTRY_COMPARE` for Qt's label resize event to set the text, which
  is an event-loop condition, not a time-dependent behaviour. The checked value
  (`folder->displayName()`) is static.
- **Fix at the cause and complete: every caller and sibling checked and
  consistent.** `Track::createView` is called at
  `src/gui/editors/TrackContainerView.cpp:448`; the folder is the only type that
  built a bare `TrackView`. Its siblings (`NeutralTrackView.cpp:41`,
  `AutomationTrackView.cpp:44`, `PatternTrackView.cpp:46`,
  `SampleTrackView.cpp:69`, `InstrumentTrackView.cpp:71`) all use the same
  `TrackLabelButton` at `move(3,1)`, which is what the folder now does.
  `TrackIcons.cpp:36-42,74-77` deliberately has no folder glyph, so no icon
  mapping was invented.
- **No collateral damage: old projects and saved data load as before; no limit
  or check added.** The change is view construction only; no serialization,
  model, command or journal path changed. `TrackFolderTest` (save/open round
  trips) passes, as do `FolderSummaryLaneTest`, `MixerSendsOnFadersTest` and
  `TrackIconsLevelBarTest`.
- **Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE, so the evidence is the
  RED run above and the original `createView` at `src/tracks/TrackFolder.cpp:174`
  before the change, which created only `gui::TrackView` plus
  `FolderSummaryLane`.** The missing label was reproduced on this branch before
  the edit.
