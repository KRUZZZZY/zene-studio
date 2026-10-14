# Cluster 5198 — Right-clicking a keyboard key auditions it

**Verdict: ALREADY-FIXED** (on this branch, before this session).

## Finding

`src/gui/noteeditor/NoteEditorParts.cpp:105-116` — every press other than the
middle button started a note audition, including right-button presses.
Confidence: medium, reported twice (Luna patrol).

## Evidence

The fix is already present in the code and committed on this branch:

- `src/gui/noteeditor/NoteEditorParts.cpp:118` —
  `m_pressed = event->button() == Qt::LeftButton ? m_state.grid.keyAtY(event->pos().y()) : -1;`
  with the comment at lines 115-116 ("Only the left button auditions...").
  A non-left press sets `m_pressed = -1`, so `mousePressEvent` auditions nothing and
  `mouseReleaseEvent` (line 136, `if (m_pressed >= 0)`) releases nothing either.
- Fix commit: `6ec2958e4 fix(gui): right-clicking a keyboard key auditioned it`
  (ancestor of HEAD on `bugs/qwen-5198`; diff changed exactly the one line above).
- Regression test already exists and passes:
  `tests/src/gui/NoteEditorTest.cpp:292` —
  `aRightClickOnAKeyDoesNotAuditionIt()` presses right then left on key 64 and
  asserts only the left press keys the piano.
- `NoteEditorParts.cpp` is registered in `tests/fork-sources.txt` (2 entries), so no
  new registration is needed.

## Verification commands (this session)

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5198.r1/logs`:

```
cmake --build build --target zene NoteEditorTest > $LOGS/build-10.log 2>&1   EXIT=0
cd build/tests && ctest -R '^NoteEditorTest$' --output-on-failure > $LOGS/test-10.log 2>&1   EXIT=8
```

`test-10.log` shows `PASS   : NoteEditorTest::aRightClickOnAKeyDoesNotAuditionIt()` and
`Totals: 25 passed, 3 failed`. The ctest exit 8 is from three pre-existing still-red
tests unrelated to this cluster (`aFastVelocityLaneStrokeTakesTheNotesItSkipsOver`,
`aTrackpadWheelScrollsAndZoomsWhereAMouseWheelDoes`,
`thePaintToolLaysANoteOnEveryCellItCrossesAsOneEdit`) — identical to the counts recorded
in fix commit `6ec2958e4` ("25 passed, 3 failed"). 28 tests executed (not zero).

Gate checks (no code changed this session; run for the record):

```
bash tests/complexity-gate.sh --check   EXIT=0
bash tests/file-length-gate.sh --check  EXIT=0
```

## Action

No code change made — the behaviour is already correct and tested. This report is the
only file committed for cluster 5198.
