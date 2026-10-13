# Cluster new-p12-154 — BUG 8, the note area's keys with no clip open

Branch `bugs/qwen-new-p12-154`. One finding, checked against the current tree before any
product code was touched. The claimed deref is real *inside the function* and unreachable
*through the only path that reaches it*, so nothing in `src/` changed; the guard that
prevents it is now locked by a test.

### BUG 8 — keyboard navigation crashes when no clip is open (`NoteCanvasAccess.cpp:162`, `moveCursor`)

Verdict: FALSE-POSITIVE
Evidence: `NoteCanvas::moveCursor` does read `m_state.clip->notes()` with no check of its own
(`src/gui/noteeditor/NoteCanvasAccess.cpp:160-172`), but it has exactly two call sites, both
inside `moveKey` (`NoteCanvasAccess.cpp:135` and `:139`), and `moveKey` is reached only from
`editKey` (`:118`), and `editKey` only from `NoteCanvas::keyPressEvent`
(`src/gui/noteeditor/NoteCanvasKeys.cpp:149-160`), whose line 152 short-circuits on
`m_state.clip != nullptr &&` before it evaluates `editKey(event)` — so with no clip open the
whole chain is skipped and the event is ignored (`:154-157`). `rg 'moveCursor|editKey|moveKey\('`
over `src/ include/ tests/` shows no other caller (the `moveCursor` hits in `HelpCenter.cpp`
and `CompSwipeStrip.cpp` are different classes); `NoteCanvas::event()` accepts only
`ShortcutOverride` through the static `isEditKey`, which reads no clip
(`NoteCanvasKeys.cpp:64-78`). The guard is not branch-local: `m_state.clip != nullptr &&`
comes from `b9a463ad42` and `|| editKey(event)` was added *inside* it by `bcac7c05ce`, both
ancestors of HEAD (`git merge-base --is-ancestor` confirmed). `m_state.clip` is a
`QPointer<MidiClip>` (`include/NoteEditor.h:85`) on a single thread, and for the claimed keys
(Alt+arrows, Home, End) `clipboardKey`/`selectionKey` return false without running any command,
so nothing can null it between the check and the read. The finding describes a crash the
shipped guard makes impossible; the deref at line 162 is defensive depth, not a defect.
Commit: none (no product file changed; `NoteCanvasKeys.cpp` was restored byte-identical and
`git diff` on it is empty)
Test: `CanvasKeyboardTest::theCursorKeysAreRefusedWhenNoClipIsOpen` — a lock-in for the guard
(the case drives the exact claimed scenario: `setClip(nullptr)`, then every cursor/resize/insert/
delete key on the canvas, asserting the area still reads "No clip open" and refuses the keys)
Red/green:
  Reverse experiment (guard removed, tree restored afterwards — proves the test covers the
  claimed path and that line 162 crashes only when reachable):
  Red:  `cmake --build build --target CanvasKeyboardTest > LOGS/build-2.log 2>&1` EXIT=0
        `cd build/tests && ctest -R '^CanvasKeyboardTest$' --output-on-failure > LOGS/test-2.log 2>&1` EXIT=8
        `Received signal 11 (SIGSEGV), code 1, for address 0x00000000000010a8`
        `#5 lmms::gui::NoteCanvas::moveCursor(int, bool)` ← `#7 NoteCanvas::editKey` ←
        `#8 NoteCanvas::keyPressEvent(QKeyEvent*)` ← `#16 CanvasKeyboardTest::theCursorKeysAreRefusedWhenNoClipIsOpen`
        `231 - CanvasKeyboardTest (SEGFAULT)`
  Green:`cmake --build build --target CanvasKeyboardTest > LOGS/build-3.log 2>&1` EXIT=0
        `cd build/tests && ctest -R '^CanvasKeyboardTest$' --output-on-failure > LOGS/test-3.log 2>&1` EXIT=0
        `1/1 Test #231: CanvasKeyboardTest ... Passed`, 100% passed, 0 failed out of 1
        (the first green run on the untouched guard is LOGS/test-1.log, EXIT=0)
  Family:`cd build/tests && ctest -R 'NoteEditor|CanvasKeyboard|LaneReadout' --output-on-failure > LOGS/test-5.log 2>&1` EXIT=0
        5 tests affected/related (NoteEditorTest, CanvasKeyboardTest, LaneReadoutTest,
        NoteEditorFlTest, ControlNoteEditor): 100% passed, 0 failed out of 5, 7.12 s.
        (LOGS/test-4.log is the first attempt, EXIT=8: three of the five binaries were not
        built yet — "Not Run" — built by LOGS/build-4.log, then re-run clean as test-5.)
  Gates: `bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0
Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-154.r1/logs`
Files committed: `tests/src/gui/CanvasKeyboardTest.cpp` (lock-in case + `handledBy` helper),
`docs/qwen-fixes-2026-10/cluster-new-p12-154.md` (this report)

## Why no defensive check was added to `moveCursor`

The suggested fix ("return early from `moveCursor` when there is no clip") would sit behind a
guard the only caller already performs, so it can never execute — dead code that would imply
a second, nonexistent entry path. The behaviour the finding reports (keys acting on a missing
clip) does not occur: with no clip the canvas paints its no-clip line (`NoteCanvas.cpp:457-461`),
`accessibleValue()` answers `"No clip open"` (`NoteCanvasAccess.cpp:245`), and the navigation
keys are refused at `NoteCanvasKeys.cpp:152` without touching the clip. Changing only that
behaviour means leaving it as it is; the new test records the contract so the guard cannot be
removed silently.
