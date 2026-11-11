FALSE-POSITIVE

# gui-1106-main-paint: "Paint tool selection is not visibly indicated"

## Decision: FALSE-POSITIVE (sweep artifact: the click never reached the Paint button)

**What the sweep clicked.** Sweep `m0-train-26` (`terra-pilot/buttons/runs/m0-train-26/results.jsonl`, entry 22) clicked (180, 742) for "Paint". Entry 12, ten steps earlier, clicked "Pop out the Inspector panel". That floated the Inspector over the left 480 px of the shell. Its header then shows the text "Dock", which a panel shows only while floating: [ShellFloating.cpp:436](src/gui/shell/ShellFloating.cpp#L436)-[438](src/gui/shell/ShellFloating.cpp#L438) (`button->setText(floating ? tr("Dock") : QString())`).

**Where the click landed.** Screenshot `shots/0022-after.png` shows the red click marker and cursor at (180, 742) on the empty floating Inspector. The visible Note Editor toolbar starts at x=480 with the fold buttons (spacing 30 px, the same as the tool buttons). The Draw..Play tool buttons (x=150..330, spacing 30) sit under the floating Inspector. The Draw, Delete, Mute and Slice clicks in the same run (entries 21-25) have the same coordinates and the same "no change" verdict. Draw was judged "UNSURE", not FAIL.

**The code path is correct.** A checked `ShellButton` paints a `pal.control` fill ([ShellChrome.cpp:141](src/gui/shell/ShellChrome.cpp#L141)) and the accent-coloured icon (`SvgIconEngine::paint`, [ShellIcons.cpp:313](src/gui/shell/ShellIcons.cpp#L313)). `NoteEditor::setTool` checks the matching action of the exclusive `QActionGroup` ([NoteEditor.cpp:278](src/gui/noteeditor/NoteEditor.cpp#L278)-[284](src/gui/noteeditor/NoteEditor.cpp#L284)), and the button follows its default action (`toolButton()`, [NoteEditorToolbar.cpp:121](src/gui/noteeditor/NoteEditorToolbar.cpp#L121)). Luna's own root cause admits "the source path does not explain why those states diverge".

**Measured.** The new test `NoteEditorFlTest::clickingAToolButtonSelectsItAndShowsIt` sends a real left mouse click to the Paint `ShellButton` of a fresh `NoteEditor`. Result: the tool becomes Paint, Paint is checked, Draw is unchecked, and the rendered Paint face differs from its unchecked face. It passes on the unmodified product code, so there is no product change. It stays as a guard.

## Evidence table

| Evidence | Command | Result |
| --- | --- | --- |
| Test build (first attempt, ccache temp dir not writable in this sandbox) | `cmake --build build --target NoteEditorFlTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-paint.r1/logs/build-1.log 2>&1` | EXIT=2 (ccache "Permission denied"; environment, not code) |
| Test build | `CCACHE_DISABLE=1 cmake --build build --target NoteEditorFlTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-paint.r1/logs/build-2.log 2>&1` | EXIT=0 |
| "RED" probe on unmodified product code | `ctest -R '^NoteEditorFlTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-paint.r1/logs/test-1.log 2>&1` (from build/tests) | EXIT=0, 1/1 passed, so not reproducible |
| Slot executed | `ctest -R '^NoteEditorFlTest$' -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-paint.r1/logs/test-4.log 2>&1` after the final build (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-paint.r1/logs/build-4.log`, EXIT=0) | EXIT=0; "PASS : NoteEditorFlTest::clickingAToolButtonSelectsItAndShowsIt()", Totals: 20 passed, 0 failed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-paint.r1/logs/feature_check_logdir.py $PWD > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-paint.r1/logs/gates-2.log 2>&1` | EXIT=0, all 8 rows PASS |

There is no product patch, so step 4b (revert, rebuild, fail) does not apply. The test cannot fail without a fix because the bug does not exist in the product.

**Gate-script note.** The stock `feature_check.py` exited 1 (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-paint.r1/logs/gates-1.log`): it creates `<worktree>-feature-check` next to the worktree, which the write jail forbids (PermissionError). I ran an unchanged copy with only that log directory (and `ROOT`) redirected to the log folder (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-paint.r1/logs/feature_check_logdir.py`). No limit or baseline was touched.

## Possible follow-up (not a bug fix)

The sweep's click-through-overlay problem is a harness issue. A control reported by the accessibility tree should be skipped, or the click re-targeted, when another top-level window covers its centre. Floating panels covering docked ones are by design.

Files committed: `tests/src/gui/NoteEditorFlTest.cpp` (+27 lines, new slot and one include; the file was already registered) and this report.

## Self-check

- **Test fails with the product change reverted and passes with it:** not applicable. The verdict is FALSE-POSITIVE, no product file changed, and the guard test passes on the unmodified code (EXIT=0, 20/20 in NoteEditorFlTest).
- **Determinism:** the test uses no timers, sleeps or waits. It sends synchronous \`QTest::mouseClick\` to the button and renders it with \`QWidget::render\` into two \`QImage\`s. The editor is the existing fixture (\`init()\` in the same file).
- **Fix at the cause, siblings:** no fix. The sibling tool buttons (Draw, Delete, Mute, Slice, Select, Play) are built by the same loop (\`addToolButtons\`, NoteEditorToolbar.cpp:162-177), and in the sweep they all show "no change" for the same reason: the click lands on the floating Inspector.
- **Collateral damage:** none; only a test and a report are added. No saved-data, undo, realtime or command surface is touched.
- **FALSE-POSITIVE contract:** a panel floats when the user pops it out. Its header then shows "Dock" (ShellFloating.cpp:436-438), and a floating panel is a separate window over the shell. Sweep entry 12 popped out the Inspector, and the screenshot 0022-after.png shows it covering x<480 where the reported button (x=180, y=742) sits. Checked-state painting is at ShellChrome.cpp:141 and ShellIcons.cpp:313. The measured click test above shows it works.
- **Gates:** run EXIT=0 (all 8 PASS) through the unchanged-logic copy of feature_check.py. The stock script cannot create \`<worktree>-feature-check\` under the write jail, so the "gates" row of submit_check.py, which calls it, reports FAIL for that environmental reason only.
