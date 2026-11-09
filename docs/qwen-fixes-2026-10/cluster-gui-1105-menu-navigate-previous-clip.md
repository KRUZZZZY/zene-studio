FIXED

# gui-1105-menu-navigate-previous-clip — Navigate refusal notification is placed off-screen

## Verdict: REAL

`TextFloat::displayMessage` with a `parent` placed the float at `parent->width() + 2` from the parent's left edge
(`src/gui/widgets/TextFloat.cpp`, the `moveGlobal(parent, QPoint(parent->width() + 2, 0))` line; was :113).
`CommandRun::announce` (`src/gui/CommandRun.cpp:95`) passes the main window as that parent, so with a window that fills
the screen the float lands past the right edge and is never seen. The refused command (no clip selected) is therefore
silent. The same applies to every `displayMessage(..., parent)` caller whose parent touches the screen's right edge, and
to the no-parent branch should the main window extend off-screen.

## Fix

`src/gui/widgets/TextFloat.cpp`: new file-local `keepOnScreen()`, called at the end of the positioning in
`displayMessage` (both branches). It clamps the float into `availableGeometry()` of the screen at the float's position
(falling back to the anchor widget's screen when the position is on no screen). A float already fully on screen is not
moved, so existing placements are unchanged. No header change, no audio-thread code, no new command, no undo involvement.
`tests/upstream-modifications.txt`: the existing `TextFloat.cpp` entry extended with the reason.

## Regression test

`tests/src/gui/GrooveMenuTest.cpp` `aMessageOverAMaximisedWindowStaysOnScreen`: sets the main window geometry to the
screen's available geometry, calls `TextFloat::displayMessage(..., parent = mainWindow)` and requires the float's
frame to lie inside that geometry. Deterministic (no timing; timeout 0 so no timer).

## Integration onto 050/m0-train-26 (round 2)

Accepted source `bugs/qwen-gui-1105-menu-navigate-previous-clip @ e8600d80b`, target `050/m0-train-26 @ af010bc3cc`.
The driver's three-way merge reported a conflict in `src/gui/widgets/TextFloat.cpp`; its fallback direct application
then applied all three hunks cleanly (`integration-apply.log`: "Applied patch to 'src/gui/widgets/TextFloat.cpp'
cleanly"). I checked the result by hand: `displayMessage` (`src/gui/widgets/TextFloat.cpp:124-162`) carries the
accepted `anchor`/`keepOnScreen` lines once, after both placement branches and before the train's theme-17
accessibility announcement (`setAccessibleName`/`a11y::announceStatus`), which is unchanged. No conflict markers, no
semantic difference from the accepted patch, no new design. Accepted regression test and registration kept verbatim.

Failure paths and platform hazards considered: parent given (CommandRun → main window, the reported path) and the
no-parent branch (main window lower-left) both pass through the clamp; a float whose position is on no screen falls
back to the anchor's screen, and with no screen at all (`screen == nullptr`) the clamp does nothing (old behaviour).
Not checked: Wayland, where a client's global `move()` of a top-level may be ignored by the compositor (the clamp
then changes nothing, like the original placement); only the offscreen platform was run. GUI thread only; nothing on the audio thread.

Round-2 evidence (logs in `qwen/runs/gui-1105-menu-navigate-previous-clip.r2/logs/`, build with
`CCACHE_TEMPDIR=$TMPDIR/cct`, `QT_QPA_PLATFORM=offscreen`):

| Step | Command | Result |
| --- | --- | --- |
| build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > build-1.log` | EXIT=0 |
| GREEN family | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > test-1-green.log` | EXIT=0, 2 of 2 tests passed |
| 4b revert | `git diff --cached -- src/gui/widgets/TextFloat.cpp > fix.patch && git apply -R fix.patch` | REVERT=0 |
| rebuild | `cmake --build build --target GrooveMenuTest -j4 > build-2-reverted.log` | EXIT=0 |
| RED | `ctest -R '^GrooveMenuTest$' --output-on-failure > test-2-red.log` | EXIT=8: `'screen.contains(shown)' returned FALSE. (message 802,0 152x80 is outside the screen 0,0 800x800)` |
| 4b reapply | `git apply fix.patch` | REAPPLY=0 |
| rebuild | `cmake --build build --target GrooveMenuTest -j4 > build-3-reapplied.log` | EXIT=0 |
| GREEN | `ctest -R '^GrooveMenuTest$' --output-on-failure > test-3-green.log` | EXIT=0, 1 of 1 passed |
| gates (original) | `python3 qwen/feature_check.py <worktree> > gates-1.log` | EXIT=1, `PermissionError` creating the sibling `<worktree>-feature-check` dir (write-jail), no gate ran |
| gates (copy) | `python3 runs/…r2/feature_check_copy.py <worktree> > gates-2.log` (diff vs original in `fc-copy.diff`: only `ROOT` pinned and log dir moved into `logs/fc`) | EXIT=0, all 8 rows PASS, RESULT: PASS (base af010bc3c, 4 changed files) |

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-gui-1105-menu-navigate-previous-clip.md`.

## Evidence (round 1, original fix on its own base)

Note: the build needed `CCACHE_TEMPDIR=$TMPDIR/cct` (the default ccache temp dir is outside the write area; the first
build attempt, build-1.log, failed with ccache "Permission denied" for that reason only).

| Step | Command | Result |
| --- | --- | --- |
| build (RED tree) | `cmake --build build --target GrooveMenuTest -j4 > build-2.log` | EXIT=0 |
| RED | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > test-1-red.log` | EXIT=8: `'screen.contains(shown)' returned FALSE. (message 802,0 152x80 is outside the screen 0,0 800x800)` |
| build (fix) | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > build-3.log` | EXIT=0 |
| GREEN family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > test-2-green.log` | EXIT=0, 2 of 2 tests passed |
| 4b revert | `git diff -- src/gui/widgets/TextFloat.cpp > fix.patch && git apply -R fix.patch` (REVERT=0); rebuild GrooveMenuTest EXIT=0; `ctest -R '^GrooveMenuTest$'` (test-3-reverted.log) | EXIT=8, same failing assertion (802,0 outside 0,0 800x800) |
| 4b reapply | `git apply fix.patch` (REAPPLY=0); rebuild EXIT=0; `ctest -R '^GrooveMenuTest$'` (test-4-reapplied.log) | EXIT=0 |
| gates | `python3 feature_check.py <worktree>` | See below |

### Gates

`feature_check.py` itself exits 1 in this sandbox before running anything: it creates `<worktree>-feature-check/` as a
sibling of the worktree, which the write-jail forbids (`PermissionError ... -feature-check`, gates-1.log, EXIT=1).
I ran an otherwise identical copy (`feature_check_copy.py`: only `ROOT` pinned to the qwen dir and the log dir moved to
`logs/fc`) — `diff` against the original shows exactly those two lines. Final run (gates-3.log), EXIT=0:

```
feature-check ... (base af010bc3c, 3 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-gui-1105-menu-navigate-previous-clip.md`.

Not checked: the real (non-offscreen) multi-monitor behaviour; the test runs on the offscreen platform (800x800 screen).
Other `displayMessage` callers and the `moveGlobal` float users (knobs, faders, piano roll) were not changed; only
`displayMessage` clamps.

## Self-check

Round 3 (re-verification of the integration on af010bc3cc at ff29ad69bb; logs in
`~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-previous-clip.r3/logs/`). Round 2 was reviewed
ACCEPT-NOTE with no blocking points, and the round-3 brief adds no new review points, so no product or test code
changed this round. The only note was Wayland, recorded below as not checked.

- **Same as the accepted patch:** the `src/` and `tests/` diff `af010bc3cc..HEAD` matches the `TextFloat.cpp`,
  `GrooveMenuTest.cpp` and `upstream-modifications.txt` hunks of the accepted patch line for line (`diff` of the +/- lines,
  DIFF=0). The clamp `keepOnScreen` is defined at `src/gui/widgets/TextFloat.cpp:50` and called once at
  `src/gui/widgets/TextFloat.cpp:146`, after both placement branches and before the theme-17 announcement.
- **Build:** `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` BUILD=0 (01-build.log).
- **Family:** `ctest -R '^(GrooveMenuTest|PluginShortcutsTest)$' --output-on-failure` from `build/tests` TEST=0, 2/2
  passed (02-test-green.log).
- **RED/GREEN, product change reverted:** `git diff af010bc3cc HEAD -- src/gui/widgets/TextFloat.cpp > logs/fix.patch &&
  git apply -R logs/fix.patch` REVERT=0; rebuild BUILD=0 (03-build-reverted.log); `ctest -R '^GrooveMenuTest$'`
  TEST_RED=8, `FAIL! GrooveMenuTest::aMessageOverAMaximisedWindowStaysOnScreen() ... (message 802,0 152x80 is outside
  the screen 0,0 800x800)` (04-test-red.log). `git apply logs/fix.patch` REAPPLY=0; rebuild BUILD=0
  (05-build-reapplied.log); TEST_GREEN=0 (06-test-green.log). `git status` was clean afterwards.
- **Deterministic:** the test is unchanged. It uses fixed geometry: the main window is set to the screen's
  `availableGeometry()`. Timeout 0 means no timer is armed, and there are no sleeps, event-loop waits or timestamps.
- **Cause/complete, no collateral damage:** the round-1 and round-2 audits still hold, because the code is unchanged. Every
  `displayMessage` caller goes through the clamp. A float that is already on screen is not moved. Nothing changes in
  saved data, commands, headers or the audio thread.
- **Gates:** `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` GATES=1, `PermissionError`
  creating `<worktree>-feature-check` outside the write-jail, so no gate ran (07-gates.log). That makes
  `submit_check.py` show `FAIL gates` (09-submit.log, SUBMIT=1); every other row is PASS. A copy with only the `ROOT`
  path and the log folder changed (fc-copy.diff, 4 changed lines) gives GATES=0: all 8 rows PASS, with complexity and
  file-length showing "no new item in your files" (08-gates-copy.log).
- **Not checked:** Wayland and real multi-monitor setups, where the compositor may ignore a global move. Only the offscreen
  platform was run.
- **Verdict FIXED** (not ALREADY-FIXED/FALSE-POSITIVE).

Round 2 (integration on af010bc3cc, r2 logs):

- **RED/GREEN on the integrated tree:** `git apply -R fix.patch` (REVERT=0) → `ctest -R '^GrooveMenuTest$'` RED_EXIT=8
  (`message 802,0 152x80 is outside the screen 0,0 800x800`, test-2-red.log); `git apply fix.patch` (REAPPLY=0) →
  GREEN_EXIT=0 (test-3-green.log). Family `GrooveMenuTest|PluginShortcutsTest`: EXIT=0, 2/2 (test-1-green.log).
- **Deterministic:** unchanged test; fixed geometry (main window set to the screen's `availableGeometry()`), timeout 0
  so no timer, no sleeps or event-loop waits.
- **Cause/complete:** integration changed nothing in the accepted design; the clamp still follows both placement
  branches in `displayMessage`, so every `displayMessage` caller is covered (round-1 audit below still holds).
- **No collateral damage:** the train's theme-17 announcement code after the placement is untouched; no data, command,
  header or audio-thread change.
- **Gates:** `submit_check.py` reports `FAIL gates (feature_check.py)` (submit-1.log) because it runs the original
  `feature_check.py`, which dies with `PermissionError` creating `<worktree>-feature-check` outside the write-jail
  (gates-1.log, EXIT=1, no gate executed). The two-line-diff copy (fc-copy.diff) runs every gate: EXIT=0, 8/8 PASS
  (gates-2.log). This row cannot go green from inside this sandbox.
- **Verdict FIXED** (not ALREADY-FIXED/FALSE-POSITIVE).

Round 1:

- **RED/GREEN with the product change reverted:** reverted `TextFloat.cpp` → `ctest -R '^GrooveMenuTest$'` EXIT=8
  (`message 802,0 152x80 is outside the screen 0,0 800x800`, test-3-reverted.log); reapplied → EXIT=0
  (test-4-reapplied.log).
- **Deterministic:** the test sets the main window to the screen's `availableGeometry()` and compares the float's
  `frameGeometry()` with it. Timeout is 0, so no `QTimer` is armed; no sleeps, no event-loop waits, no timestamps.
- **Cause and completeness:** the cause is the single placement in `TextFloat::displayMessage` (`parent->width() + 2`);
  the clamp sits after both branches (parent given / main-window fallback). Callers of `displayMessage` (rg
  `displayMessage\(` in `src/`: CommandRun, MainWindow, ClipView, ClipLockFeedback, PianoRoll, TimeLineWidget, …) all
  go through it, so all are covered. The sibling `moveGlobal` users (FloatModelEditorBase, Fader, SongEditor,
  PianoRoll) position their own floats at a knob or slider and are a different path; I did not change them.
- **No collateral damage:** `keepOnScreen` leaves a float that is already fully inside the screen exactly where it was, so
  only placements that were partly or wholly off-screen move. No saved data, project file or command is touched; no
  header change; nothing on the audio thread.
- **Gates:** see the Gates section. `feature_check.py` / `submit_check.py` cannot create their sibling log directory in
  this write-jail (`PermissionError`), so their gates row reports FAIL here for that environmental reason; the
  equivalent copy passes all 8 rows (gates-3.log).
- **Verdict is FIXED**, not ALREADY-FIXED/FALSE-POSITIVE, so no not-real evidence is needed.
