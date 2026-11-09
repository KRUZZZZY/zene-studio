FIXED

# Cluster gui-1106-session-move-copy-and-delete-a-clip — Arranger ignores Ctrl+V after clip Copy

## Verdict: REAL (Ctrl+V part), fixed and integrated into 050/m0-train-26. The gain-curve drag part is by design (not changed).

### Integration of the accepted patch (this run, r2)
The accepted patch — `bugs/qwen-gui-1106-session-move-copy-and-delete-a-clip @ 2b33505d643ac679e3be839bcb9c25006573697e`
— was applied to target `050/m0-train-26 @ af010bc3cc055d477045a54e2d866f59a95017d7` with a three-way merge in
this integration worktree. The reported conflict was in `tests/CMakeLists.txt`.

- The merge base of the accepted branch and the target is the target itself:
  `git merge-base 2b33505d64 af010bc3cc` → `af010bc3cc`. The accepted branch's first commit (`b2fc95f2bd`)
  has parent `af010bc3cc`.
- The integrated diff is byte-identical to the accepted diff: `git diff --cached` vs
  `git diff af010bc3cc 2b33505d64` compared with `diff -q` → no output, EXIT 0. The conflict resolution therefore
  keeps the accepted design, the accepted regression test and the accepted registrations, with no semantic
  difference.
- Both `tests/CMakeLists.txt` hunks are present: the test source at line 1423 and the offscreen
  `set_tests_properties` entry at line 3822.

### Affected failure paths (checked together)
- `SongEditor::keyPressEvent` (`src/gui/editors/SongEditor.cpp:401-470`) is the only entry: it calls
  `arrangerkeys::handle` first (line 404). `handle` now runs `handleClipboard` before `claims`
  (`src/gui/ArrangerClipKeys.cpp:301-305`). With no handler, Ctrl+C/X/V fell through to `QWidget::keyPressEvent`
  (line 468) and were ignored — the bug.
- `handleClipboard` (`src/gui/ArrangerClipKeys.cpp:278-288`) matches only plain Ctrl+C/X/V
  (`modsOf(event) != Qt::ControlModifier` rejects Shift+Insert/Shift+Delete, so the insert-bar / remove-bar
  chords at `SongEditor.cpp:406-415` are unchanged). With nothing selected, Ctrl+C/X announce a refusal instead
  of acting.
- `pasteAtPlayPosition` (`src/gui/ArrangerClipKeys.cpp:265-274`) reads `Clipboard::getMimeData()`, which returns a
  static empty `QMimeData` when the clipboard is null, so a headless/offscreen session cannot crash;
  `Clipboard::decodeKey` on the empty data does not start with `clip_` and is left alone (test 4).
- `pasteTarget` (`src/gui/ArrangerClipKeys.cpp:248-261`) parses `copyMetadata/initialTrackIndex` and bounds-checks
  it against `Engine::getSong()->tracks()`; an out-of-range or missing index returns `nullptr` and nothing is
  pasted.
- The paste itself is the lane's own `TrackContentWidget::canPasteSelection` + `pasteSelection`
  (`src/gui/tracks/TrackContentWidget.cpp:345`, `:724`), so track type, track lock, multi-track offsets, the
  PatternEditor single-clip rule and the same-spot snap shift still apply. Copy/Cut are the clip context menu's
  `ClipView::copy/cut` (`src/gui/clips/ClipView.cpp:1286`, `:1299`), so lock handling and one undo step are kept.
- No audio-thread code is touched; no control command is added; no saved-data format changes.

### Supported-platform hazards
- Qt6 only (the tree builds with `Experimental Qt6 support: Enabled`); the code uses `QKeyEvent`, `QVector`,
  `QMimeData` and `QCoreApplication::translate`, all available in the supported Qt6 build.
- Clipboard access goes through `Clipboard::getMimeData()`, which substitutes an empty `QMimeData` on platforms
  where Qt returns null (offscreen/headless); no platform-specific branch is added.
- The plain Ctrl chords are handled on every platform; macOS users who press Cmd keep the existing context-menu
  behaviour, unchanged.

### Regression test: `tests/src/gui/ArrangerClipboardKeysTest.cpp` (new; registered in tests/CMakeLists.txt:1423,3822 and tests/fork-sources.txt)
Key presses are sent to the arranger widget (`QApplication::sendEvent`, through `SongEditor::keyPressEvent`):
1. `ctrlVPastesACopiedClipAtThePlayPosition`: context-menu Copy, play position at bar 5, Ctrl+V → clips at 0 and
   768. Then Delete on the copy → the original stays (the finding's full move/copy/delete path).
2. `ctrlCCopiesAndCtrlVFollowsTheSelectedTrack`: Ctrl+C copies without changing anything; Ctrl+V lands on the selected
   clip's track.
3. `ctrlXCutsAndCtrlVPutsItBack`: Ctrl+X removes; Ctrl+V with nothing selected pastes on the source track.
4. `ctrlVWithoutCopiedClipsChangesNothing`: a text clipboard is not pasted.

## Commands and results (r2 integration run; logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-session-move-copy-and-delete-a-clip.r2/logs)
ccache's default temp dir is outside the write jail, so every build ran with
`CCACHE_TEMPDIR=<r2 logs>/ccache-tmp`.

| Step | Command | EXIT / result |
|---|---|---|
| build zene | `cmake --build build --target zene ArrangerClipboardKeysTest -j4` (01) | 2; zene built, then make reported `No rule to make target 'ArrangerClipboardKeysTest'` while the CMake regeneration was still settling |
| build test | `cmake --build build --target ArrangerClipboardKeysTest -j4` (02) | 0 |
| GREEN focused | `ctest -R '^ArrangerClipboardKeysTest$' --output-on-failure` (03) | 0; 100% of 1 |
| build family | `cmake --build build --target VisibilitySetMenuTest HybridTrackGuiTest TrackViewDeferredLifetimeTest ArrangerFileDropTest ArrangerKeysNavigatorTest -j4` (04) | 0 |
| family | `ctest -R '^(VisibilitySetMenuTest\|HybridTrackGuiTest\|TrackViewDeferredLifetimeTest\|ArrangerFileDropTest\|ArrangerClipboardKeysTest\|ArrangerKeysNavigatorTest)$'` (05) | 0; 100% of 6 |
| 4b revert | `git diff HEAD -- src/gui/ArrangerClipKeys.cpp include/ArrangerClipKeys.h > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b rebuild | `cmake --build build --target zene ArrangerClipboardKeysTest -j4` (06) | 0 |
| RED | `ctest -R '^ArrangerClipboardKeysTest$' --output-on-failure` (07) | 8; 3 failed: `Actual std::vector(0) / Expected std::vector(0, 768)` |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b rebuild | `cmake --build build --target zene ArrangerClipboardKeysTest -j4` (08) | 0 |
| GREEN | `ctest -R '^ArrangerClipboardKeysTest$' --output-on-failure` (09) | 0; 100% of 1 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (10) | 0; all 8 rows PASS, RESULT: PASS |

### r3 re-verification (logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-session-move-copy-and-delete-a-clip.r3/logs)
The r2 QA reviews (`r2/qa-session.log`, `r2/qa-session-2.log`) both returned `"verdict": "ACCEPT"` with
`"blocking": []`, so round 3 had no review point to address. The integrated commit `53e6ef3a4d` was re-checked
unchanged; no product or test file was edited in r3.

| Step | Command | EXIT / result |
|---|---|---|
| semantic identity | `git diff af010bc3cc HEAD -- . ':!docs'` vs `git diff af010bc3cc 2b33505d64 -- . ':!docs'`, `diff -q` | 0 (identical apart from this report) |
| build | `cmake --build build --target zene ArrangerClipboardKeysTest VisibilitySetMenuTest HybridTrackGuiTest -j4` (01) | 0 |
| GREEN family | `ctest -R '^(ArrangerClipboardKeysTest\|VisibilitySetMenuTest\|HybridTrackGuiTest)$' --output-on-failure` (02) | 0; 100% of 3 |
| 4b revert | `git diff af010bc3cc HEAD -- include/ArrangerClipKeys.h src/gui/ArrangerClipKeys.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b rebuild | `cmake --build build --target ArrangerClipboardKeysTest -j4` (03) | 0 |
| RED | `ctest -R '^ArrangerClipboardKeysTest$' --output-on-failure` (04) | 8; `FAIL! ctrlVPastesACopiedClipAtThePlayPosition … Actual (startsOn(track)) : std::vector(0)`, `FAIL! ctrlCCopiesAndCtrlVFollowsTheSelectedTrack` |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b rebuild | `cmake --build build --target zene ArrangerClipboardKeysTest -j4` (05) | 0 |
| GREEN | `ctest -R '^ArrangerClipboardKeysTest$' --output-on-failure` (06) | 0; 100% of 1 |
| gates (as shipped) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (07) | 1; `PermissionError` writing `<worktree>-feature-check/fork-sources.log` (that folder, hard-coded at `feature_check.py:70`, is outside this run's write jail), before any gate result |
| gates (log folder moved) | the same script run in memory with only line 70 changed to `logs = Path(sys.argv[2])` (gate logs → `r3/logs/gate-logs`) (08) | 0; all 8 rows PASS, RESULT: PASS |

### r1 evidence (accepted patch provenance; logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-session-move-copy-and-delete-a-clip.r1/logs)
The accepted branch was developed on the same base `af010bc3c`; its own RED/GREEN/family/gate runs were:
RED `ctest -R '^ArrangerClipboardKeysTest$'` EXIT 8 (3 failed); GREEN EXIT 0 (1/1); family EXIT 0 (6/6);
`feature_check.py` RESULT: PASS (run with a copy that only moved the log folder, because the original could not
create `<worktree>-feature-check` in that lane's jail). In this r2 worktree the original `feature_check.py` ran
directly and passed (10-feature-check.log), so that environment limitation no longer applies.

## Self-check
- **The test fails with the fix reverted and passes with it:** yes, in this integration worktree. After
  `git apply -R logs/fix.patch` and a rebuild (06-build-reverted.log, EXIT 0), `ctest -R '^ArrangerClipboardKeysTest$'`
  gave EXIT 8, 3 failed (07-test-reverted.log). After `git apply logs/fix.patch` and a rebuild (08, EXIT 0), it gave
  EXIT 0, 100% of 1 (09-test-reapplied.log). The plain GREEN run (03, EXIT 0) agrees.
- **Deterministic:** no timers, sleeps or threads. The play position is set directly
  (`Song::setPlayPos(…, PlayMode::Song)`). Clips are created at fixed ticks (0, 2, 6, 8 bars). The clipboard is the
  in-process Qt clipboard of the offscreen platform, cleared in `init()`. Key events go synchronously through
  `QApplication::sendEvent`, followed by `processEvents()`.
- **The fix is at the cause and complete:** the cause is the missing key handling in the arranger's key path. Its
  one entry is `SongEditor::keyPressEvent` → `arrangerkeys::handle` (`SongEditor.cpp:404`; `handle`'s only other
  callers are its own tests). Siblings stay consistent: Copy and Cut call the same `ClipView::copy/cut` as the
  clip context menu (`ClipView.cpp:1286-1299`). Paste uses the same `TrackContentWidget::canPasteSelection` +
  `pasteSelection` as the lane's context-menu Paste (`TrackContentWidget.cpp:345`, `:724`), so track type, track
  lock, multi-track offsets, the PatternEditor single-clip rule and the same-spot snap shift all still apply.
  Ctrl+C, Ctrl+X and Ctrl+V together, because a paste without a key copy would be half a fix. Shift+Insert and
  Shift+Delete (`QKeySequence::Paste/Cut` alternates) are deliberately not matched, so `SongEditor.cpp:406-415`
  insert-bar and remove-bar are unchanged; `ArrangerKeysNavigatorTest` (which asserts which chords `claims()`
  takes) still passes. `claims()` is unchanged.
- **No collateral damage:** no saved data, file format, control command or snapshot changed. No limit or check
  was added. Only plain Ctrl+C, Ctrl+X and Ctrl+V on the focused arranger change behaviour; before, the arranger
  ignored them. A clipboard without clips is ignored (test 4). Six related GUI tests pass (05-test-family.log).
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable (the verdict is FIXED). The gain-curve part is by design;
  see `SampleClipView.cpp:449-454` in the r1 report.
- **`submit_check.py` gates row:** in r2 the original `feature_check.py` ran directly (10-feature-check.log, EXIT 0,
  RESULT: PASS). In r3 it could not create its hard-coded log folder outside the jail (07-gates.log, EXIT 1,
  PermissionError before any gate ran). The same gate code, with only its log folder moved, passed
  (08-gates.log, EXIT 0, 8/8 PASS). No gate, limit or baseline was changed.
- **r3 review points:** none. Both r2 QA verdicts were ACCEPT with no blocking items, and the r3 re-run reproduced
  RED (EXIT 8) and GREEN (EXIT 0) on the unchanged integrated commit.
