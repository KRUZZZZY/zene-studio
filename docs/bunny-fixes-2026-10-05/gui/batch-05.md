# GUI batch-05 — Luna findings for `src/gui`

Worktree `zene-bunny-gui`, branch `bugs/bunny-gui-2026-10-05`. Six findings triaged:
2 FIXED, 3 FALSE-POSITIVE, 1 REAL-UNTESTED. No control command was added, removed or
renamed, and no params/result schema changed — the 4917 fix adds the private
`__transaction` payload, which `ControlRegistry::invoke` records and strips
(`src/core/ControlRegistry.cpp:389-421`), so no wire result gained a key.

### Cluster 4917 — View-memory mutations omit their transaction payload

Verdict: FIXED

Evidence: `src/gui/ControlCommandsViewMemory.cpp` — `saveEntry` recorded an undo step
through `storeEntries` and `removeEntry` did the same, but neither returned a
`__transaction`, so `ControlRegistry::invoke` took the fallback at
`src/core/ControlRegistry.cpp:390-408` and stamped every record with "this command
recorded no inverse or snapshot" — a command the Window menu documents as one undo step.
Both now return `entryPayload(kind, previous, changed)` (`:161`, inserted at `:216` and
`:240`): the list as it was before, `control.undo` as the inverse, and `reversible:false`
for the no-op save that recorded no step.

Commit: 9ddca8995

Test: CustomAccessTest::aViewMemoryChangeReportsTheUndoItRecorded

Red/green:
- RED: `cmake --build build --target zene CustomAccessTest > build/bunny-b.log 2>&1; echo EXIT=$?` → EXIT=0;
  `cd build/tests && ctest -R '^CustomAccessTest$' --output-on-failure > .../build/bunny-t.log 2>&1; echo EXIT=$?` → EXIT=8
  (`FAIL! ... 'saved.value("before").toObject().contains("locations")' returned FALSE. (location.save reported no previous list of locations to take back)`).
- GREEN: same build → EXIT=0; same ctest → EXIT=0, `100% tests passed, 0 tests failed out of 1`.
- Neighbours: `ctest -R '^(ReversibilityContractTest|ReversibilityUndoTest)$' --output-on-failure; echo EXIT=$?` → EXIT=0.
- Gates: `bash tests/complexity-gate.sh --check; echo EXIT=$?` → 0; `bash tests/file-length-gate.sh --check; echo EXIT=$?` → 0.

### Cluster 4916 — Recalling a location can create multiple undo steps

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsRange.cpp:137-150` — `setRange` writes the arranger
range and records NO undo step (the file's only `addUndoStep` calls are at `:121` and
`:242`, other commands), and `range.set` is classed `NotMutating` in the contract table at
`src/core/ControlReversibilityTableArranger.cpp:53` ("it selects a time range: interface
state, not saved"). Of the two commands a recall drives, only `transport.seek` records a
step (`src/core/ControlCommandsTransport.cpp:218`), so one Ctrl+Z reverses the whole
recall's engine effect. The existing case `aSavedSelectionComesBack`
(`tests/src/core/CustomAccessTest.cpp:262`) already pins this: "range.set and clip.select
are interface state: the newest undo step is still the save."

Commit: none

Test: none

Red/green: none

### Cluster 4915 — An empty clip id bypasses saved-selection validation

Verdict: FIXED

Evidence: `src/gui/ControlCommandsViewMemory.cpp:306` — `firstUnknownClip` used to answer
with a `QString`, so the unknown empty-string clip id was indistinguishable from "no
unknown clip" at the call site. It now returns `bool firstUnknownClip(const QJsonArray& ids,
QString* unknown)` and `saveSelection` (`:330`) refuses on the flag, so `clips:[""]` is a
`NotFound` refusal instead of a stored entry that `selection.apply` would later drop.

Commit: 119d05fdd

Test: CustomAccessTest::aClipIdThatNamesNoClipIsRefusedByTheValidator

Red/green:
- RED: `cmake --build build --target zene CustomAccessTest > build/bunny-b.log 2>&1; echo EXIT=$?` → EXIT=0;
  `cd build/tests && ctest -R '^CustomAccessTest$' --output-on-failure > .../build/bunny-t.log 2>&1; echo EXIT=$?` → EXIT=8
  (`FAIL! ... '!saved.ok' returned FALSE. (an empty clip id was stored as a clip)`).
- GREEN: same build → EXIT=0; same ctest → EXIT=0, `100% tests passed, 0 tests failed out of 1`.
- Gates: complexity → 0, file-length → 0.

### Cluster 3000 — State-changing selection commands are registered as non-mutating

Verdict: FALSE-POSITIVE

Evidence: `location.recall` (`src/gui/ControlCommandsViewMemory.cpp:403`) and
`selection.apply` (`:416`) compose `transport.seek`, `range.set` and `clip.select`, all
of which the A16 contract table classes as interface state, not saved state:
`src/core/ControlReversibilityTableW3Custom.cpp:76` and `:83` both say so in words, and
`ControlReversibilityTableArranger.cpp:53` does the same for `range.set`. `location.recall`
never calls `addUndoStep` itself. Flipping `mutating` would move the command out of the
table's agreement and fail `ReversibilityContractTest` (`theOnlyUnclassedMutatingCommandsAreTheRefusals`,
`tests/src/core/ReversibilityContractTest.cpp:381`), and rule 8 forbids changing a command's
published contract.

Commit: none

Test: none

Red/green: none

### Cluster 4052 — The template menu hides command failures

Verdict: REAL-UNTESTED

Evidence: `src/gui/ControllerTemplateMenu.cpp:57-60` — `run()` invokes the
`controller.template_*` command and discards the `ControlResult`, so a refused save, a
delete of something already gone or a partially resolved apply closes the menu silently.
The claim about the code is correct, but the shared surfacing path it should use,
`lmms::gui::announce` (`src/gui/CommandRun.cpp:87-97`), returns before showing anything when
`ControlRegistry::instance()->isHeadless()` — and that flag is `isUnattendedRun()`
(`src/core/ControlRegistry.cpp:132`), which the offscreen test run always satisfies — or when
`getGUI() == nullptr`. Every headless menu test therefore observes the identical tree before
and after the fix, so no deterministic RED exists in this lane. Per procedure step 2,
product code left unchanged.

Commit: none

Test: none

Red/green: none

### Cluster 1401 — Copying to multiple channels creates separate undo operations

Verdict: ALREADY-FIXED

Evidence: `src/gui/EffectDeviceMenu.cpp:91-98` — `copyTo()` picks the channels once and
sends them all in a single `invoke` as `'to'` holding a `QJsonArray` ("W1: every chosen
channel in ONE call ('to' as a list), so one undo step takes all the copies back"),
introduced by commit d92e54343. `src/core/ControlCommandsDeviceCopy.cpp:93-99` accepts that
list and records one transaction for the whole run, so a single Ctrl+Z removes every copy
the menu action made. The reported line (`e5a603132:113-121`) predates that commit.

Commit: none (already fixed by d92e54343)

Test: none

Red/green: none

Leftovers: cluster 4052 is still real but unfixed — the fix is one `announce(menu, call, result)`
at the three `run()` call sites; it needs a test that boots `GuiApplication` on a platform
where `isUnattendedRun()` is false, which this lane's offscreen ctest cannot provide.