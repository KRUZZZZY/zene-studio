# Bunny fixes — GUI batch 06

Lane: `gui` (src/gui). Branch `bugs/bunny-gui-2026-10-05`.
Findings: Luna clusters 2252, 4153, 4154, 4156, 5044, 5097.

Totals: 3 FIXED, 2 REAL-UNTESTED, 1 SKIPPED-SCOPE. No FALSE-POSITIVE, no ALREADY-FIXED, no DUPLICATE.

---

### Cluster 2252 — Render quality controls can change an active render

Verdict: REAL-UNTESTED

Evidence: `src/gui/modals/ExportProjectDialog.cpp:195` adds the quality group to the dialog layout, and
`src/gui/modals/ExportProjectDialog.cpp:282` disables only `m_startButton` when a render starts — the
`QGroupBox` from `makeExportQualityControls` (`src/gui/ExportQualityControls.cpp:71`) is never disabled, and
`choiceBox`'s `currentIndexChanged` handler (`src/gui/ExportQualityControls.cpp:63-65`) invokes
`export.set_dither` / `export.set_src_quality` immediately. The enabling half of the report is therefore
accurate.

Not fixed: a deterministic headless RED is not available. Rendering is asynchronous — the dialog connects
`RenderManager::finished` and rebuilds enablement only when the render ends
(`src/gui/modals/ExportProjectDialog.cpp:290`, `:346`) — so "the group is disabled while a render is in
flight" can only be asserted by racing the render thread, and an empty/small project can finish before the
assertion runs. Per the batch procedure (no product change without a deterministic headless RED), the code
was left alone.

Commit: none
Test: none
Red/green: none

---

### Cluster 4153 — Canceling feedback-disable leaves the menu checkbox out of sync

Verdict: FIXED

Evidence: `src/gui/FeedbackModeAction.cpp:69-76` — a checkable `QAction` unticks itself before `triggered`
runs, and the decline path returned without restoring the state, so the menu item showed unchecked while
feedback was still enabled. The fix captures the action in the lambda, restores `setChecked(true)` on the
decline path, and otherwise syncs the tick from the `ControlResult` (`enabled`) instead of assuming success.

Commit: 20fb862cb
Test: FeedbackModeActionTest::theDeclinedDisableLeavesTheMenuItemTicked
Red/green:
- RED: `cmake --build build --target zene FeedbackModeActionTest` EXIT=0; `ctest -R '^FeedbackModeActionTest$'` EXIT=8 — `'allow->isChecked()' returned FALSE`.
- GREEN: same build EXIT=0; same ctest EXIT=0.

---

### Cluster 4154 — Groove-menu command failures are discarded

Verdict: REAL-UNTESTED

Evidence: `src/gui/GrooveMenu.cpp:67-71` — `run` calls `ControlRegistry::instance()->invoke(...)` and drops
the returned `ControlResult`, so no error text ever reaches the user. The fix route is the codebase-standard
`runCommand`/`announce` pair (`src/gui/CommandRun.cpp:87-120`), which floats a `TextFloat` on failure.

Not fixed: `announce` returns early when `ControlRegistry::isHeadless()` or `getGUI() == nullptr`
(`src/gui/CommandRun.cpp:90`), and `GrooveMenuTest` boots only a headless `Engine::init(true)`
(`tests/src/gui/GrooveMenuTest.cpp`), so the observable the fix creates does not exist in that harness.
Observing it would mean booting a real `GuiApplication` via `tests/src/gui/GuiTestApplication.h` and
asserting on a floated `TextFloat`, which has no precedent anywhere in the suite
(`rg -n "TextFloat" tests/src` finds only comments). Per the batch procedure, product code was left alone.

Commit: none
Test: none
Red/green: none

---

### Cluster 4156 — Follow-action timing controls imply later rows can set timing

Verdict: SKIPPED-SCOPE

Evidence: `src/gui/FollowActionEditor.cpp:141` already states in the dialog's own hint that "The first row's
timing (After the clip or Bars) applies to the whole chain", while `addRow` wires `Bars` for every row
(`src/gui/FollowActionEditor.cpp:174-188`) and `wireRow` enables it for every row
(`src/gui/FollowActionEditor.cpp:82-91`). The confusing part is real, but the report offers two remedies —
restructure timing into a single chain-level control, or disable and relabel later rows — both of which are
product/UX design decisions about the editor's shape rather than a defect with one correct minimal fix.
Either choice needs its own RED defining the intended behaviour first.

Commit: none
Test: none
Red/green: none

---

### Cluster 5044 — A follow-action chance edit can be mistaken for an untouched value

Verdict: FIXED

Evidence: `src/gui/FollowActionEditor.cpp:96-110` — `valueOf` compared the box's current value against the
rounded `shown` property, so explicitly typing the already-displayed value was discarded and the loaded
value returned instead. The fix has `numberBox` record an `edited` dynamic property on
`QAbstractSpinBox::editingFinished`, and `valueOf` returns `box->value()` when `edited` is set, keeping the
old loaded/shown logic for a box the user never committed.

Commit: cb1ac493e (test registered in the scope manifest by 420bb27e0)
Test: FollowActionEditorTest::anExplicitChanceEditIsWritten
Red/green:
- RED: `cmake --build build --target zene FollowActionEditorTest` EXIT=0; `ctest -R '^FollowActionEditorTest$'` EXIT=8 — `Actual 0.1234, Expected 0.12`.
- GREEN: same build EXIT=0; same ctest EXIT=0 (Test #252 Passed). `SessionTransportBarTest` and `SessionSceneFollowTest` both EXIT=0.

---

### Cluster 5097 — Document Type actions can show incorrect check marks

Verdict: FIXED

Evidence: `src/gui/LibraryCommandMenus.cpp:96-110` built the Document Type actions as independent checkable
`QAction`s with no exclusive group, so selecting one left the previous one ticked and re-triggering the
active type unticked it. The fix wraps them in an exclusive `QActionGroup`, which also makes `QAction`
untick the previous selection.

Commit: 7bdfb3d26
Test: LibraryMenusTest::theDocumentTypeMenuTicksOneTypeAtATime
Red/green:
- RED: `cmake --build build --target zene LibraryMenusTest` EXIT=0; `ctest -R '^LibraryMenusTest$'` EXIT=8 — `Compared lists have different sizes. Actual (ticked()) size: 2, Expected (QStringList{other}) size: 1`.
- GREEN: same build EXIT=0; same ctest EXIT=0 (Test #237 Passed).

---

## Leftovers

- `tests/fork-sources-gate.sh` exits 1 on `tests/src/gui/ShortcutsCommandsTest.cpp` not being listed in the
  scope manifest. This is pre-existing (present before any change in this batch) and was deliberately not
  touched, since editing another scope's manifest is not this lane's call.
- Clusters 2252 and 4154 are confirmed real and remain unfixed; both need a harness that boots a real
  `GuiApplication` (and, for 2252, a way to observe mid-render state deterministically).
- Cluster 4156 needs a product decision on the editor's timing shape before it can be given a RED.
