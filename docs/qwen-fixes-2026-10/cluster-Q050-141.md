IMPLEMENTED

# Q050-141 - Touch-first layout and large-target mode

## Integration onto 050/m0-train-16 (round 5)

The accepted patch (`feat/050-Q050-141-oc` @ `19c15a9d5`) was applied by a three-way merge onto
`050/m0-train-16` @ `cf3618e61`. Seven files conflicted. Every conflict was two independent additions,
and both sides are kept. No new design was needed:

| File | Train side (ours) | Accepted side (theirs) | Resolution |
|---|---|---|---|
| `include/Appearance.h` | doc for Q050-235's `workspace` option | accessible-mode doc rewritten for the 44/8 minimum | the accepted accessible bullet, then the train's workspace bullet |
| `include/DesignTokens.h`, `src/gui/shell/DesignTokens.cpp` | `touchWorkspace()`, `tapTargetSize()` (Q050-235) | `targetSpacing()` | all three functions kept |
| `src/gui/tracks/TrackOperationsWidget.cpp` | Q050-022's automation-lanes fold button `m_lanesBtn` | accessible-mode 44 px sizing of the track operation buttons | both blocks; the fold button is added to the sized list (see below) |
| `data/manual/index.html` | Access paragraph rewritten for Help > Appearance and the touch workspace | Access paragraph for accessible mode's touch minimum | the train paragraph, with accessible mode described as the 44 x 44 / 8 px touch-first layout, the Customise > Theme path, and finger tap select/Play/Stop |
| `tests/all-sources.txt` | `TouchWorkspaceTest.cpp` | `TouchLayoutTest.cpp` | both, sorted; `all-sources-reproduce.sh` exit 0 |
| `commands_snapshot.json` | live capture of the train | only capture metadata (time, socket, version) | the train's file as is: the accepted patch changes no command, so no metadata differs |

**The one semantic difference from the accepted patch:** the train added a new track-operation
button (`m_lanesBtn`, Q050-022, hidden unless the track owns automation lanes). The accepted design
says every operation in that row is a 44 px target, so the new button joins the accessible-mode
sizing loop. Without that, a visible lanes button would be the only sub-44 px target in a touch row.
In standard mode nothing changes. `AutomationLaneChildGuiTest`, which drives that button, passes.

**Failure paths and platform hazards checked:**
- Accessible mode's `accessibleTargetFactor` changed in the accepted patch (1.35 -> 44/24). The train's
  `tapTargetSize()` is `max(targetSize(), 44)`, so it stays correct either way. `TouchWorkspaceTest`
  and `ControlTouchWorkspace` pass, and so do the other train tests that read `targetSize()`
  (`PerformViewTest`, `TaskIndicatorTest`, `KnobKeyboardTest`, `KnobAccessibleTargetSizingTest`).
- The legacy `density:"touch"` still means accessible mode, and the train documents the same.
  `workspace:"touch"` is a separate preference, so the two units don't collide.
- No new Qt API was used in the resolution (`std::max` and the existing widget calls only), and no
  new identifiers. The accepted patch's Qt 5.15 guards in `TouchInput.cpp` are unchanged.
- Realtime: every resolved hunk is GUI-thread widget construction or token arithmetic.
- Projects: no serialization touched. Appearance stays user config.
- Snapshot: the accepted patch adds or changes no command (its own snapshot diff was metadata only),
  so the train's live snapshot is kept, and `ControlCommandsSnapshot` passes against it.

**Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-141.r5/logs/`):**
- `build-1.log` exit 2: ccache could not write its configured temp dir (outside the write jail). Rebuilt
  with `CCACHE_TEMPDIR=/tmp/ccache-tmp-q141`: `build-2.log` was killed by the tool's 10-minute limit
  mid-build; `build-3.log` **exit 0** (`zene` and the tests below).
- `ctest -R "^(TouchLayoutTest|TouchWorkspaceTest|ControlTouchWorkspace|DesignTokensTest|NoteGridTest|WorkspaceAccessTest|KnobKeyboardTest|KnobAccessibleTargetSizingTest|ArrangerTimelineTest|PerformViewTest|TaskIndicatorTest)$" --output-on-failure`
  from `build/tests`: **exit 0**, 11/11 (`test-1.log`).
- `build-4.log` exit 0; `ctest -R "^(AutomationLaneChildGuiTest|AccessibilityWalkTest|NoteEditorTest|SessionTransportBarTest|ReversibilityContractTest|TrackFolderMenuTest|ControlCommandsSnapshot)$"`
  **exit 0**, 7/7 (`test-2.log`).
- Reversal of the resolved hunks (`targetSpacing()` returning the standard gap, and the
  TrackOperationsWidget accessible block disabled), `build-rev.log` exit 0: `ctest -R "^TouchLayoutTest$"`
  **exit 8** (`test-rev.log`). Five cases fail: header and toolbar spacing 4 and 2 < 8, arranger lane 44 < 52,
  note target 48x43, note zoom floor. Restored: `build-5.log` exit 0, `ctest -R "^(TouchLayoutTest|AutomationLaneChildGuiTest)$"`
  **exit 0** (`test-3.log`).
- Gates: `doc-figures-gate.py` 0 (`doc-figures-1.log`); `fork-sources-gate.sh` 0,
  `all-sources-reproduce.sh` 0, `no-upstream-regression-gate.sh` 0, `no-tautology-gate.sh` 0,
  `check-namespace` 0 (`gate-*.log`). `complexity-gate.sh --check` and `file-length-gate.sh --check` exit 1,
  with 7 and 19 REGRESSION lines. None of them names a file this patch changes (filtered against
  `git diff --cached --name-only cf3618e61`, empty result), so they come from the train.
- `feature_check.py` itself could not run here, for two reasons. Its log directory sits beside the worktree,
  outside the write jail (`feature-1.log`, PermissionError). A copy pointed at the run folder was then refused
  by the bunny guard, because it checks out the shared `gate-baselines/tree` (`feature-2.log`). So its eight
  gates were run one by one, as above.

The round-1 to round-4 history follows unchanged.

## Earlier rounds (round 3 rework)

Branch `feat/050-Q050-141-oc` on `050/m0-train-8` (`2da2ed356`). Round 1 keeps the earlier
accessible-mode/touch work and adds the two review fixes:

1. **Qt 5.15 compile:** all `QTouchEvent` point-list access now goes through the compatibility helper
   `touchPoints()` (`QTouchEvent::points()` on Qt 6, `QTouchEvent::touchPoints()` on Qt 5.15) and the
   `TouchPoint` alias (`QEventPoint` / `QTouchEvent::TouchPoint`). Syntax-only compiles of
   `src/gui/TouchInput.cpp` pass against **both** Qt 5.15.19 (`qt5-syntax-2.log`, exit 0) and Qt 6.12
   (`qt6-syntax-1.log`, exit 0).
2. **Arranger targets:** in accessible mode the arranger lane is 52 px (a 44 px clip plus the 8 px
   row gap), clips (including short ones) are at least 44 x 44, the per-track Actions/Mute/Solo/Arm
   buttons are 44 x 44 with 8 px gaps, the track label and settings controls grow with the lane, and
   note rows/notes are 44 px-high/wide targets. `TouchLayoutTest` now measures the rendered arranger
   geometry and gaps alongside the note-editor and transport checks.

Accessible Mode remains the one touch-oriented setting (`window.appearance`, one-step `true_inverse`
undo, legacy `touch`/`comfortable` aliases, next-start timing). No command, project format or render
path changed.

Round 3 (review of round 2: adjacent short notes' targets overlapped and pitch rows were 1 px apart):

3. **Note targets split from musical geometry:** `NoteCanvas::rectFor` is back to the musical width (3 px
   minimum); the new `NoteCanvas::touchRect` widens it to 44 px for hit-testing only. `noteAt` tests the
   drawn rectangles first, then the touch targets, taking the nearest note centre, so a tap on either of
   two adjacent 12-tick notes selects that note.
4. **Rows and zoom floor:** in accessible mode a note is 44 px high, centred in a 52 px row (8 px to the
   next row's note), and `NoteGrid::setMinRowHeight(52)` stops vertical zoom (Alt+wheel, key-strip drag)
   going below it. `TouchLayoutTest` gained `touchTapsOnAdjacentShortNotesSelectTheIntendedNote` and
   `noteRowsKeepTheTouchMinimumAndTheZoomFloor`.

Round 3 evidence: build `build-4.log` exit 0; `ctest -R "TouchLayoutTest|NoteEditorTest|NoteEditorFlTest|LaneReadoutTest|DesignTokensTest|AccessibilityWalkTest|NoteGridTest"`
exit 0, 7/7 (`test-2.log`). Reversal (note geometry back to the widened rectangle, unclamped zoom): `build-rev.log`
exit 0, `ctest -R TouchLayoutTest` exit **8** (`test-rev.log`) - both new cases fail; restored `build-4.log` exit 0,
`test-3.log` exit 0.

## Acceptance

| Criterion | Test and command | What it proves | Exit |
|---|---|---|---|
| Enable touch layout; target sizes and panel spacing meet a documented touch minimum in representative editors | `TouchLayoutTest` (all slots), `WorkspaceAccessTest`; `cmake --build build --target zene TouchLayoutTest WorkspaceAccessTest -- -j4`, then `ctest -R "TouchLayoutTest\|WorkspaceAccessTest" --output-on-failure` from `build/tests` | `theAppearanceMenuRunsTheSameCommandAsAgents` triggers Project tools > Customise > Theme > Accessible Mode (each action declares `window.appearance`) and watches `appearance::options().accessible` change; the executable starts accessible and `targetSize()` is 44. Rendered geometry: every header/transport and note-editor button is >= 44x44 with >= 8 px spacing/gaps; the arranger lane is >= 52 px, a clip is >= 44x44, the track operation buttons are >= 44x44 with >= 8 px gaps, two clip rows are >= 8 px apart, and a note target is >= 44x44. `WorkspaceAccessTest` keeps persistence, next-start timing, one-step undo and the legacy density aliases. | build 0, test 0 (2/2, `test-r1-8.log` / `test-r1-restore.log`) |
| Use touch events to operate common selection and transport controls without mouse-only targets | `TouchLayoutTest::aTouchTapSelectsAnArrangerClip`, `aTouchTapSelectsANote`, `aTouchTapPlaysAndStopsTheTransport`; same ctest command | QtTest touch begin/end on the main window: the arranger's content viewport (which accepts touch) dispatches the tap to the clip under the finger and sets the shell selection to the clip and its track; a tap selects a note through `note.select`; Play starts and Stop ends playback through `transport.play` / `transport.stop`. The test disables Qt's unhandled-touch-to-mouse fallback in its own `main()`. | test 0; reverted 8 (`test-r1-reversal.log`) |

Reversal proof (all product hunks reverted to `2da2ed356`, tests kept): `build-r1-reversal.log`
exit 0, `ctest -R "TouchLayoutTest|WorkspaceAccessTest"` exit **8** (`test-r1-reversal.log`):
the 44/24 target and header/toolbar spacing assertions fail, the arranger lane is 32 px, the note
target is 48x13, and all three touch cases fail. Restored: `build-r1-restore.log` exit 0, tests
exit 0, 2/2 passed.

Neighbours (same tree): `ctest -R "TouchLayoutTest|WorkspaceAccessTest|ArrangerTimelineTest|NoteEditorTest|ShellTest|AccessibilityWalkTest|DesignTokensTest|KnobKeyboardTest|KnobAccessibleTargetSizingTest|SessionTransportBarTest|TrackFolderMenuTest|MainWindowTest"`
exit 0, 12/12 passed (`test-r1-7.log`). `ReversibilityContractTest` and `ControlCommandsSnapshot`
exit 0 (`test-8.log`). `doc-figures-gate.py` exit 0 (`doc-figures-1.log`); snapshot regenerated
live (`snapshot-1.log`, 833 ids / 76 groups). `feature_check.py` exit 0 (`feature-check-r1-2.log`).

## Self-check

**Round 5 (integration onto train-16) - these supersede earlier numbers where they differ.**

- DoD 1: criterion 1 (`TouchLayoutTest` geometry, `WorkspaceAccessTest` menu, persistence and undo) and
  criterion 2 (`TouchLayoutTest::aTouchTap...`) pass on the integrated tree, `test-1.log` exit 0. Reversal of
  the resolved hunks gives exit 8 (`test-rev.log`); restored gives exit 0 (`test-3.log`). The full-product
  reversal is from round 3 (exit 8). It was not repeated, because the test now calls API the product adds
  (`touchRect`, `minRowHeight`), so a full revert would not compile.
- DoD 2/3/4: no command was added or changed. The GUI action (Theme > Accessible Mode) and the agent path
  (`window.appearance`) are the same as in the accepted patch. `ReversibilityContractTest` and `ControlCommandsSnapshot`
  exit 0 (`test-2.log`), and `doc-figures-gate.py` exit 0. The snapshot was not regenerated: no metadata changed,
  and the train's live capture is kept.
- DoD 5: the manual's Access paragraph is merged (touch-first accessible mode and the touch workspace); the
  feature doc records the integration and the relation to Q050-235.
- DoD 6/7/8: the resolution adds GUI-thread code only, no format change, and no new API or identifier. See the
  integration section for the details.

**Round 3 additions (these supersede the round-1 numbers where they differ).**

- Reversal: note geometry put back to the widened rectangle and the zoom unclamped, rebuilt
  (`build-rev.log` exit 0), `ctest -R TouchLayoutTest` exit **8** (`test-rev.log`): both
  `touchTapsOnAdjacentShortNotesSelectTheIntendedNote` and `noteRowsKeepTheTouchMinimumAndTheZoomFloor`
  fail. Restored: `build-4.log` exit 0, `test-3.log` exit 0.
- Neighbours after the change: `ctest -R "TouchLayoutTest|WorkspaceAccessTest|ArrangerTimelineTest|NoteEditorTest|NoteEditorFlTest|LaneReadoutTest|NoteGridTest|AccessibilityWalkTest|DesignTokensTest|SessionTransportBarTest"`
  exit 0, 10/10 (`test-4.log`).
- Gates: `feature_check.py` could not write its log directory beside the worktree (outside the write
  jail, `PermissionError`), so a copy with only the log path pointed at the run folder was run:
  exit 0, all eight rows PASS (`feature-2.log`). `submit_check.py`'s "gates" row runs the unpatched
  script and so fails for that reason alone.
- Realtime: the new code (`NoteCanvas::touchRect`/`noteAt`/`rectFor`, `NoteGrid::setMinRowHeight`) is
  GUI-thread widget geometry; nothing runs on an audio thread. No command or format changed.
- Portability: no Qt-version-specific API added (`std::abs`/`std::clamp` only).

- **DoD 1 - new tests fail when the product change is reverted, then pass restored.** Reverted build
  `build-r1-reversal.log` exit 0; `ctest -R "TouchLayoutTest|WorkspaceAccessTest"` exit 8
  (`test-r1-reversal.log`) - target/spacing, arranger lane/clip, note target and all three touch
  cases fail. Restored build `build-r1-restore.log` exit 0 and the same ctest exit 0
  (`test-r1-restore.log`).
- **DoD 1 - every acceptance criterion is proven by a test run here.** Criterion 1: `TouchLayoutTest`
  (transport/header, note editor, arranger lane/clip/track-operation geometry/gaps, menu) and
  `WorkspaceAccessTest` (command/persistence/undo/aliases), exit 0. Criterion 2: the three
  `aTouchTap...` cases, exit 0, and exit 8 with the product reverted.
- **DoD 2 - GUI and agent, the same command.** The GUI action is Project tools > Customise > Theme >
  Accessible Mode (`src/gui/WorkspaceMenus.cpp:187`, action property `window.appearance`); the agent
  command is `window.appearance` (`src/gui/ControlCommandsHelp.cpp`). `theAppearanceMenuRunsTheSameCommandAsAgents`
  actually triggers the GUI action and observes the persisted `accessible` field. No command id,
  argument or result changed.
- **DoD 3 - one undo step, honest reversibility.** No edit command was added or changed;
  `window.appearance` remains `true_inverse` (`src/core/ControlReversibilityTableDevices.cpp`) and
  records before/after as one undo step. `WorkspaceAccessTest::accessibleModeIsAnAppearanceField`
  sets then undoes once, exit 0.
- **DoD 4 - command metadata.** No command metadata changed; the live snapshot was regenerated anyway
  (`snapshot-1.log`: 833 ids / 76 groups), `doc-figures-gate.py` exit 0, and
  `ReversibilityContractTest` exit 0, so the A16 histogram did not change.
- **DoD 5 - docs.** `data/manual/index.html` states the 44 x 44 / 8 px minimum and finger selection,
  Play and Stop; `docs/050-features/Q050-141.md` has one row per acceptance criterion, the Qt 5.15
  compatibility helper, the arranger target geometry, no format change and the known limits;
  `docs/UI-SHELL.md` lists `TouchLayoutTest`.
- **DoD 6 - realtime.** Nothing added touches the audio thread: `TouchInput.cpp` is a QWidget event
  filter on the GUI thread; `Appearance`, `DesignTokens`, `ShellChrome`, `TrackView`/track views,
  `ClipView`, `NoteCanvas`, `NoteGrid`, `NoteEditorToolbar` and `ArrangerAids` are GUI code. The only
  audio-path name in the diff is `NotePlayHandleManager::init()` in the test's `initTestCase` - the
  existing test-fixture setup, no engine path changed. No allocation, lock, wait, I/O or signal was
  added to an audio callback.
- **DoD 7 - projects.** No serialization or render path changed: appearance is user config
  (`ui/density`), not project state; the new lane/clip target sizes are view-only and derived at
  startup. Old projects load and render through unchanged code.
- **DoD 8 - portability.** `TouchInput.cpp` now guards **all** point-list access (`touchPoints()`,
  `TouchPoint`) with `QT_VERSION >= 6.0.0` and uses the Qt 5.15/6 common `QMouseEvent` constructor;
  both Qt 5.15.19 and Qt 6.12 syntax compiles exit 0 (`qt5-syntax-2.log`, `qt6-syntax-1.log`). No
  `std::filesystem`, no Windows macro identifiers, no POSIX-only call.
- **Completion check.** `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-141`
  is run after the report commit; this section answers every ASK item (reversal exit 8, per-criterion
  exits above, undo/reversibility, realtime, projects, portability).

IMPLEMENTED
