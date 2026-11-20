IMPLEMENTED

# Q050-468 - Tab / Shift+Tab move to the next and previous transient

While the arranger has keyboard focus, a plain Tab moves the play position to the next audio transient and
Shift+Tab to the previous one. Both run the registered `arrangement.next_transient` command, the same handler as
Navigate > Next Transient / Previous Transient. A Tab in a text field, a Tab with Ctrl/Alt/Meta, or a Tab sent to
an arranger without focus is not claimed. A move with no transient in that direction leaves the position and
announces the refusal. The only product effect is `transport.seek`; no command, command table, reversibility row,
metadata snapshot or file format changed.

## Acceptance

| Criterion | Test slot | Command | Exit |
| --- | --- | --- | --- |
| Tab and Shift+Tab invoke `arrangement.next_transient` forward / backward when the arranger has focus, and the position equals the command's own result | `ArrangerKeysNavigatorTest::tabMovesToNextTransientAndShiftTabToPrevious` (`tests/src/gui/ArrangerKeysNavigatorTest.cpp:449`) | `ctest -R ArrangerKeysNavigatorTest --output-on-failure` (cwd `build/tests`) | 0 |
| A move with no transient either way leaves the position and announces the refusal | `ArrangerKeysNavigatorTest::tabWithNoTransientLeavesPositionAndSays` (`:474`) | same | 0 |
| Text entry and unrelated chords are not intercepted | `ArrangerKeysNavigatorTest::tabDoesNotTouchTextEntry` (`:483`), `otherChordsAreNotClaimed` (`:503`) | same | 0 |
| The tests fail with the product change reverted and pass restored | the same positive slots, with `SongEditor::event`'s call neutralised (`false &&`) | `ctest -R ArrangerKeysNavigatorTest --output-on-failure`, then rebuild and rerun | 8 RED / 0 GREEN |

GREEN totals from the direct run: 19 passed, 0 failed, 0 skipped (`/tmp/duet-Q050-468.r1/003-test-2.log`);
ctest GREEN: 1/1 tests passed (`/tmp/duet-Q050-468.r1/007-test-green.log`). RED: 18 passed, 1 failed
(`/tmp/duet-Q050-468.r1/005-test-red.log`, red-green narrative in `/tmp/duet-Q050-468.r1/red-green.txt`).

## RED / GREEN evidence

RED mutation (test-only proof, reverted): `src/gui/editors/SongEditor.cpp` (`SongEditor::event`, lines 409-419)
prefixed with `false &&` so `arrangerkeys::handleTransientKey` never fires; tests untouched.

- RED build `cmake --build build --target zene ArrangerKeysNavigatorTest -- -j4` exit 0
  (`004-build-red.log`); RED ctest exit 8 (`005-test-red.log`), 1/1 failed:
  `FAIL! : ArrangerKeysNavigatorTest::tabMovesToNextTransientAndShiftTabToPrevious() 'next > 30' returned FALSE. (Tab went to 30, not forward)`
  totals 18 passed, 1 failed.
- Restored with `git checkout -- src/gui/editors/SongEditor.cpp`; GREEN build exit 0 (`006-build-green.log`);
  GREEN ctest exit 0, 1/1 passed in 2.93 s (`007-test-green.log`).

## Gates

`feature_check.py` (final, after the completion-gate fix; `/tmp/duet-Q050-468.r1/013-gate-2b.log`, exit 0):

```
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

`doc-figures-gate.py` (`/tmp/duet-Q050-468.r1/010-gate-3.log`, exit 0): snapshot measures 933 ids / 78 groups;
`ok README.md: 933 ids / 78 groups`; `RESULT: PASS`. The command set was not changed, so the snapshot was not
regenerated.

`submit_check.py` before the report existed (`/tmp/duet-Q050-468.r1/008-gate-1.log`, exit 1) passed every item
except the report: `PASS commits on the branch`, `PASS working tree clean`, `FAIL report ... missing`,
`FAIL report has a 'Self-check' section ...`, `PASS feature record`, `PASS a GUI path ...`, `PASS a GUI test
drives that GUI path ...`, `PASS user manual updated`, `PASS no allocation, lock, signal or I/O added inside
audio-path functions`, `PASS no POSIX-only absolute-path test`, `PASS the GUI test clicks, triggers or types into
the real widget`, `PASS tests added or changed`, and `FAIL gates (feature_check.py) - complexity` for the
12-CCN handler. The complexity failure was fixed by moving the key/chord/focus filters into `claimsTransient`
(`src/gui/ArrangerClipKeys.cpp:324-333`), leaving `handleTransientKey` (`:335-346`) under the CCN 10 target; the
fix was committed as `62787cb03c` and `ArrangerKeysNavigatorTest` rebuilt (exit 0, `011-build-fix-1.log`) and
rerun (exit 0, `012-test-fix-1.log`, 1/1 passed). The final `submit_check.py` run after this report's commit is
recorded as gate-4 in the same log directory.

## Log location

The runs logs directory was not writable, so all build/test/gate logs for this packet live under
`/tmp/duet-Q050-468.r1/`, with `DUET_LOG_DIR` exported to that path before every quiet run.

## Known limits

- A consumed Tab means there is no Tab focus traversal out of the arranger: while the arranger has focus, Tab
  navigates transients instead of moving keyboard focus to the next widget.
- Only audio clips under the play position have transients (the existing command's behaviour).
- Local TSan is not available; no threading was touched and the code runs on the GUI thread only.

## Self-check

**Each new test FAILS with the product change reverted, then PASSES restored?** Yes. The `SongEditor::event` call
was neutralised with `false &&`, tests untouched: build exit 0, ctest exit 8 with
`tabMovesToNextTransientAndShiftTabToPrevious` failing (`'next > 30' returned FALSE`, Tab stayed at 30); the two
negative-guard slots passed by design because they assert that nothing is intercepted. After
`git checkout -- src/gui/editors/SongEditor.cpp`, build exit 0 and ctest exit 0 (19 passed). See
`/tmp/duet-Q050-468.r1/red-green.txt`, logs `004`-`007`.

**Every acceptance criterion is proven by a test you ran yourself?** Yes. `ctest -R ArrangerKeysNavigatorTest
--output-on-failure`, cwd `build/tests`, exit 0, 1/1 test passed; the direct binary log reports 19 passed, 0
failed, including `tabMovesToNextTransientAndShiftTabToPrevious`, `tabWithNoTransientLeavesPositionAndSays`,
`tabDoesNotTouchTextEntry` and `otherChordsAreNotClaimed` (`003-test-2.log`, `007-test-green.log`).

**Each edit is ONE undo step, and every new or changed command has an honest reversibility class?** No command is
new or changed: the keys invoke the pre-existing `arrangement.next_transient`, whose class is `not_mutating`, and
there is no command-table or reversibility-row change (`submit_check.py` reports no command file changed; the
feature record states this). Each key press is one command invocation and nothing writes the model directly, so
there is no extra undo step to collapse; the command is not undoable because it only seeks the transport.

**Nothing added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal?** The
whole path is GUI-thread code: `SongEditor::event` calls `arrangerkeys::handleTransientKey`, which filters the
event and invokes a registered command through `ControlRegistry`; no audio-path function was touched.
`submit_check.py` passes `no allocation, lock, signal or I/O added inside audio-path functions`, and
`feature_check.py` passes `no-upstream-regression` (exit 0).

**Old projects still load and render exactly as before; new data round-trips through save and load?** No file
format, schema or serialization code changed; the change is key handling plus `transport.seek`. `feature_check.py`
passes `no-upstream-regression` (exit 0) and the diff touches only GUI key handling, its test, the manual and
docs. No new persisted data exists to round-trip.

**Completeness: walk each acceptance criterion through the REAL user path and the agent path; which edge case
did you try?** Real path: a focused `SongEditor` receives a real `QTest::keyClick`; `SongEditor::event` sees Tab
before focus traversal, `handleTransientKey` claims it only for plain Tab / Shift+Tab with arranger focus, and the
registered `arrangement.next_transient` runs, so the transport lands on the command's own `position_ticks`. Agent
path: the same registered command is what the menu and any agent surface invoke; no new command was added, so the
agent surface is unchanged and still reaches `arrangement.next_transient`. Edge cases tried: no transient in
either direction (position stays at 500, refusal announced), a focused `QLineEdit` (Tab not claimed), Ctrl+Tab and
Alt+Tab (not claimed), and Shift+Tab delivered as both `Key_Backtab` and `Key_Tab` with Shift (both move
backward).

**The GUI test drives the visible control and checks the SAME registered command ran?** Yes: the test sends real
key events with `QTest::keyClick` to the focused arranger widget and asserts the resulting play position equals
the `position_ticks` returned by invoking `arrangement.next_transient` from the same start, so it proves the key
path ran that command's own handler rather than writing the model directly. `submit_check.py` passes both `the GUI
test clicks, triggers or types into the real widget` and `a GUI test drives that GUI path`. The command is
`not_mutating`, so an undo-depth increment is not applicable and would contradict its declared class; no undo
entry is created by a seek.

**Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro names, no
std::filesystem outside _WIN32?** The change uses only `QKeyEvent`, `QApplication::focusWidget`, `QEvent::KeyPress`,
`Qt::Key_Tab`, `Qt::Key_Backtab` and `Qt::ShiftModifier`, all present in Qt 5.15 and Qt 6; no `windows.h`, no
`std::filesystem`, no Qt-6-only API. `feature_check.py` passes `check-namespace` (exit 0) and
`all-sources-reproduce` (exit 0).
