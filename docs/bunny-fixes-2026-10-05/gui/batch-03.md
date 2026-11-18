# GUI batch-03 — Zene Studio Space Bunny fixer lane

Branch `bugs/bunny-gui-2026-10-05`, base `78f11579f`. Six findings: three fixed, one false-positive,
one real-but-untestable, none skipped for scope.

### Cluster 3797 — Palette labels lose literal ampersands

Verdict: FIXED
Evidence: `src/gui/CommandPalette.cpp` `clean()` removed every `&`, so a Qt-escaped `&&` lost one
ampersand and the label read "A  B". It now collapses `&&` to a single literal `&` while still
dropping a lone mnemonic marker; the tab-truncation behaviour is unchanged.
Commit: a0c463679
Test: `CommandPaletteTest::anEscapedAmpersandSurvivesAsOne`
Red/green:
- `cmake --build build --target zene CommandPaletteTest` -> EXIT=0
- `cd build/tests && ctest -R '^CommandPaletteTest$' --output-on-failure` -> EXIT=8 (a literal '&&' lost its ampersand)
- after the fix, the same build -> EXIT=0 and the same ctest -> EXIT=0 (100% tests passed, 0 failed out of 1)
- gates: complexity EXIT=0, file-length EXIT=0

### Cluster 5558 — A non-left double-click cycles the comp

Verdict: FIXED
Evidence: `src/gui/CompSwipeStrip.cpp` `mouseDoubleClickEvent()` invoked `comp.cycle` for any
button, so a right- or middle-double-click changed the project. It now returns early unless
`event->button() == Qt::LeftButton`.
Commit: 2ca48b403
Test: `CanvasKeyboardTest::onlyALeftDoubleClickCyclesTheTake`
Red/green:
- `cmake --build build --target zene CanvasKeyboardTest` -> EXIT=0
- `cd build/tests && ctest -R '^CanvasKeyboardTest$' --output-on-failure` -> EXIT=8 (a non-left double-click reached comp.cycle)
- after the fix, build EXIT=0, same ctest EXIT=0 (100% tests passed)
- gates: complexity EXIT=0, file-length EXIT=0

### Cluster 5564 — Irreversible palette actions skip confirmation

Verdict: REAL-UNTESTED
Evidence: `src/gui/CommandPalette.cpp` `paletteActivate` (~241-255) calls `entry.action->trigger()`
with no `entry.irreversible` check, while `runNow` (~216-223) confirms first; `src/gui/MainWindow.cpp`
`declares()` tags `project.new` (~290), `project.open` (~299), `project.save_version` (~310),
`project.save_as_template` (~314), `project.import` (~319) and `script.run` (~325) as irreversible.
The gap is real, but it cannot be tested deterministically headlessly: the confirmation seam
`confirmAction` (`src/gui/CommandMetadata.cpp` ~441-448) returns true when `ControlRegistry::isHeadless()`
or `getGUI() == nullptr`, and `CommandPaletteTest` runs a plain `QApplication`, so forcing a real modal
`QMessageBox` would block offscreen. No product code was changed; a trial patch was reverted.
Commit: none
Test: none
Red/green: none

### Cluster 2733 — An unknown arranger tool can index the tool list out of bounds

Verdict: FALSE-POSITIVE
Evidence: the out-of-bounds path is unreachable through the registry. `set_tool` is declared with
`enumProperty(toolNames())` (`src/gui/ControlCommandsArrangerView.cpp:421`), `ControlRegistry::invoke`
calls `validateArgs(cmd->argsSchema, args)` before the handler (`src/core/ControlRegistry.cpp:251`), and
`validateValue` runs `checkEnum` (`src/core/ControlSchema.cpp:102-110`, reached from `validateArgs` at
`src/core/ControlSchema.cpp:161-172`), which refuses a value outside the allowed set with InvalidArgs.
So `setTool`'s `indexOf` (~273-282) cannot return -1 from registry dispatch — an unknown tool is rejected
before the handler runs. `arrangerToolNames()` (`src/gui/ArrangerOverlays.cpp` ~251-256) is the same
static 7-name list used for both the schema and the handler. No defence-in-depth was added, since the
minimal behaviour-preserving verdict is that the defect cannot occur.
Commit: none
Test: none
Red/green: none

### Cluster 2824 — Layout names are trimmed on save but not on lookup

Verdict: FIXED
Evidence: `saveLayout` trims the name (`src/gui/ControlCommandsLayouts.cpp:99`) but `applyLayout`
(:132) and `removeLayout` (:184) used the supplied name verbatim, so a layout saved as " Studio " was
stored as "Studio" and could not be applied or removed as "Studio". Both lookups now `.trimmed()`.
Commit: e971cf968
Test: `WorkspaceAccessTest::aLayoutIsFoundUnderTheNameItWasSavedWith`
Red/green:
- `cmake --build build --target zene WorkspaceAccessTest` -> EXIT=0
- `cd build/tests && ctest -R '^WorkspaceAccessTest$' --output-on-failure` -> EXIT=8 (no saved layout ' Studio ')
- after the fix, build EXIT=0, same ctest EXIT=0 (100% tests passed, 0 failed out of 1)
- other suites covering the file: `ctest -R '^(WorkspaceAccessTest|ShellHomesTest|CustomAccessTest)$'` -> EXIT=0
- gates: complexity EXIT=0, file-length EXIT=0

### Cluster 971 — `setField()` reports success for values that do not fit the field

Verdict: FIXED
Evidence: `CommandForm::setField` (`src/gui/CommandForm.cpp:247-253`) returned `formfield::writeValue`,
whose `Kind::Text`/`List`/`Json`/`Keys`/`Pick` arms always answered true
(`src/gui/CommandFormFields.cpp:462-476`), while only `Kind::Choice` could refuse — contradicting the
contract in `include/CommandForm.h:93` ("false when the value does not fit it"). `writeValue` now takes
the field's spec and refuses a value that is not of the field's type; a choice still answers from its own
list, and bounds stay `collect()`'s business so an out-of-range number remains correctable by the user
(CommandFormTest asserts that at `tests/src/gui/CommandFormTest.cpp` ~347). Only caller is `applyDefaults`
(`src/gui/CommandForm.cpp:222-232`), which only warns, so nothing else changes.
Commit: e115ab9cb
Test: `CommandFormTest::aValueOfTheWrongTypeIsRefusedByTheFieldItDoesNotFit`
Red/green:
- `cmake --build build --target zene CommandFormTest` -> EXIT=0
- `cd build/tests && ctest -R '^CommandFormTest$' --output-on-failure` -> EXIT=8 (an integer field took a word)
- after the fix, build EXIT=0, same ctest EXIT=0 (100% tests passed, 0 failed out of 1)
- gates: complexity EXIT=0, file-length EXIT=0

Leftovers: Cluster 5564 is real but stays unfixed — it needs a test that can drive a modal confirmation
offscreen (or a non-modal confirm seam) before any product change is justified; and Cluster 971's bounds
half of the report's suggestion is deliberately left to `collect()`, since `CommandFormTest` documents
that an out-of-range value is accepted into the field and refused at collect time.
