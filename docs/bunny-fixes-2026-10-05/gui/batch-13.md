# GUI batch 13 — 6 Luna findings

Worktree `zene-bunny-gui`, branch `bugs/bunny-gui-2026-10-05`. Findings were treated as
hypotheses and checked against the current tree; line numbers below are the current ones.

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 4636 | ALREADY-FIXED | — |
| 4635 | ALREADY-FIXED | — |
| 5450 | SKIPPED-SCOPE | — |
| 5481 | FIXED | `6ab6dcc23` |
| 4674 | FIXED | `c7b03c7de` |
| 1855 | ALREADY-FIXED | — |

### Cluster 5450 — "New VCA group" takes two undo steps

Verdict: SKIPPED-SCOPE

Evidence: `src/gui/VcaStripView.cpp:196-202` does call `vca.create` and then `vca.assign`
as two separate invocations, so the report's mechanism is real. But making it one undo
step requires either a new/changed control command (`vca.create`'s argsSchema has only
`name`, `src/core/ControlCommandsVca.cpp:340-342`) or a GUI-callable journal transaction.
Undo coalescing only groups consecutive runs of the *same* command on the *same* target
(`src/core/ControlUndoCoalescing.cpp`, `src/core/ControlReversibility.cpp:86-88`), and no
`beginGroup`/transaction API is exposed to the GUI (`include/ControlRegistry.h`,
`include/ProjectJournal.h`, `include/ControlRegistryGroups.h` searched). Rule 8 forbids
adding or changing a control command, so the code is left alone.

Commit: none
Test: none
Red/green: none

### Cluster 5481 — The XY pad's dot is clipped at the range endpoints

Verdict: FIXED

Evidence: `src/gui/XyPad.cpp:173` painted the dot at `m_point.x() * width()` /
`(1.0 - m_point.y()) * height()` while `moveTo()` (`src/gui/XyPad.cpp:196`) maps the pointer
over `std::max(1, width() - 1)` / `std::max(1, height() - 1)`. At an endpoint the dot's
centre therefore sat a whole pixel *outside* the widget and half the 7px-radius circle was
clipped. The draw now uses the same `width() - 1` / `height() - 1` span as the pointer.

Commit: `6ab6dcc23`
Test: `XyPadPairTest::theDotIsNotClippedAtTheRangeEndpoints`

Red/green:
- Red: `cd build/tests && ctest -R '^XyPadPairTest$' --output-on-failure` → `EXIT=8`,
  "the dot at X=0 is centred at 2.96 and the dot at X=1 at 157.03; mirrored about the
  widget they would sum to 159" (they summed to 160.0 = `width()`).
- Green: the same command → `EXIT=0`; also
  `ctest -R '^(XyPadPairTest|CanvasKeyboardTest)$'` → `EXIT=0` (2/2).

Note on the assertion: a whole-pixel bounding-box or centroid comparison cannot see this
defect — clipping is symmetric, so the dot is the same truncated size at *both* ends, and
all four endpoint boxes measure 7px across versus 14px at the centre. The test therefore
asserts the invariant that a one-pixel offset actually violates: the two endpoint dots must
be mirror images about the widget centre (`leftX + rightX == width() - 1`, and likewise
for Y), measured as an antialiasing-coverage-weighted sub-pixel centroid that excludes the
two flat background fills. Tolerance 0.6; no existing assertion was loosened.

### Cluster 4636 — The XY pad shows movement even when macro updates fail

Verdict: ALREADY-FIXED

Evidence: `src/gui/XyPad.cpp:274-288` already reads both `ControlResult`s, sets the status
line to "Not set: <reason>", announces the refusal and calls `syncPoint()` to pull the dot
back. Covered today by
`tests/src/gui/XyPadPairTest.cpp::aDragIsOneStepAndARefusalIsShown`, which removes the Y
macro underneath the open pad and asserts the move is refused whole (X does not move alone)
and the status says "Not set".

Commit: none
Test: `XyPadPairTest::aDragIsOneStepAndARefusalIsShown`
Red/green: none (pre-existing coverage passes: `ctest -R '^XyPadPairTest$'` → `EXIT=0`)

### Cluster 4635 — One XY pad move creates separate undo steps for its axes

Verdict: ALREADY-FIXED

Evidence: `src/gui/XyPad.cpp:60-71` issues a *single* `rack.macro_set_pair` for distinct
X/Y macros (or one `rack.macro_set` when both axes name the same macro), and the widget's
control command property is set to that same pair command (`src/gui/XyPad.cpp:215`). A drag
is therefore one mutation, and coalescing inside the journal window groups it into one step.
Covered by `tests/src/gui/XyPadPairTest.cpp::aDragIsOneStepAndARefusalIsShown` and
`tests/src/core/UndoHonestyTest.cpp::twoMacrosAreSetAsOneStep`. Note that undo coalescing is
known-by-design and must not be "fixed" (rule 9).

Commit: none
Test: `XyPadPairTest::aDragIsOneStepAndARefusalIsShown`,
`UndoHonestyTest::twoMacrosAreSetAsOneStep`
Red/green: none (pre-existing coverage passes)

### Cluster 4674 — The lock tooltip stays wrong when the lock holder changes

Verdict: FIXED

Evidence: `src/gui/clips/ClipLockFeedback.cpp:136-140` set the tooltip only inside
`if (badge->property("locked").toBool() != locked)`. Locking the track and then one of its
clips leaves the badge locked either way, so the property never flips and the tooltip keeps
saying the track holds the lock even though `lockHolder()`
(`src/gui/clips/ClipLockFeedback.cpp:119-123`) now reports the clip. The `setToolTip` call
now runs on every sync; the `setProperty` stays inside the guard.

Commit: `c7b03c7de`
Test: `ArrangerTimelineTest::theLockTooltipFollowsTheLockHolder`

Red/green:
- Red: `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` → `EXIT=8`,
  `badge->toolTip().contains("This clip is locked")` returned FALSE (actual: "This clip's
  track is locked"); the other 16 cases passed.
- Green: the same command → `EXIT=0`.
- `ArrangerTimelineTest` is the only test that exercises `ClipLockFeedback` (verified by
  search), so no other executable needed rerunning. Both gates `EXIT=0` before each commit.

### Cluster 1855 — Locked clips can move with an unlocked selection

Verdict: ALREADY-FIXED

Evidence: `src/gui/clips/ClipView.cpp:879-880` skips locked clips in the
`Action::MoveSelection` path (`if (EditLocks::instance().locks(*it)) { continue; }`), so a
group drag cannot carry a locked clip along; the press guard at
`src/gui/clips/ClipView.cpp:608-614` also blocks starting an edit on a locked clip. The
group *resize* path named in the finding does not exist in this file — a search of
`ClipView.cpp` finds no `ResizeSelection` handler — so there is nothing left unguarded. The
delete-selection filter (`src/gui/clips/ClipView.cpp:1220-1224`) and the paste refusal
(`:1254-1255`) are in place as well.

Commit: none
Test: none of its own (the behaviour is covered by
`ArrangerTimelineTest::aLockedClipWearsAPadlockAndStillSelects` and the locked-clip refusal
case at `ArrangerTimelineTest.cpp:231-235`)
Red/green: none

Leftovers: cluster 5450 is unfixed by design and will stay so until a control command can
carry create-and-assign, or a journal transaction API is exposed to the GUI (the parent's
call, not this lane's). Cluster 5481's regression test depends on the pad's own colours
(`DesignTokens.h` accent over the control fill) and would need revisiting if that dot ever
stops being painted in the palette accent.