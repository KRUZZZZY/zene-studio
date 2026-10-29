Verdict: FIXED

new-p21-202: FIXED (latent; not reachable from the current UI)

Evidence:
- The finding is real at the contract level. `ControllerSource::setValue` checked only `row >= size` and then indexed `all[static_cast<std::size_t>(row)]` ([src/gui/noteeditor/ItemListNoteSources.cpp:238-242](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-202/src/gui/noteeditor/ItemListNoteSources.cpp)). A negative `int` row converts to a huge `size_t`. `operator[]` does not bounds-check, so the result is undefined behaviour, not the "throw" the finding describes. The RED run shows it as heap corruption (`free(): invalid size`, SIGABRT).
- The current callers cannot produce a negative row. `ItemListPanel::setData` passes `index.row()` after `index.isValid()` ([src/gui/ItemListPanel.cpp:218](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-202/src/gui/ItemListPanel.cpp)). `ItemListPanel::stepCurrent` passes `current.row()` after `current.isValid()` ([src/gui/ItemListPanel.cpp:413-416](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-202/src/gui/ItemListPanel.cpp)). `rg` over `src` and `include` finds no other `setValue(` caller for this interface. The defect is in the contract, so it is latent in the product.
- The sibling `NotesSource::setValue` has the same gap ([src/gui/noteeditor/ItemListNoteSources.cpp:131](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-202/src/gui/noteeditor/ItemListNoteSources.cpp)). A negative row there reaches `note(row)`, whose `m_order.at()` throws `std::out_of_range`. It is fixed with the same guard.
- The fix adds `row < 0 ||` to both guards. Nothing else in the file changes. `insertAfter` and `remove` already check `row >= 0`, and the `ItemListSurfaceSources` setValue implementations already check `row < 0` or are unchanged by this fix.

Found while reading, not changed here (separate defect, recommend its own cluster): `ControllerSource::value()` binds `const ControllerPoint& point = points().at(...)`. `points()` returns a `std::vector` by value, so `point` refers into a temporary that is destroyed at the end of that declaration. The read of `point.tick`/`point.value` is a use-after-free for every valid row. It is called for every cell render and by `ItemListPanel::stepCurrent`. The fix is a local copy, `const ControllerPoint point = points().at(...)`, but that is outside this finding.

Test: `tests/src/gui/ItemListPanelTest.cpp`, new functions `aNegativeControllerRowIsRefusedNotIndexed` and `aNegativeNoteRowIsRefusedNotIndexed`, next to the existing controller-list tests. They call the sources directly with row `-1` (the list panel cannot produce it) and assert the call is refused and the lane or note list is unchanged.

Commands and exit codes (LOG FOLDER: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-202.r1/logs`):

| Step | Command (run from the worktree or `build/tests` as noted) | EXIT | Result |
| --- | --- | --- | --- |
| Build attempt 1 (test only, pre-fix) | `cmake --build build --target ItemListPanelTest -j2 > build-1.log 2>&1` | 2 | ccache could not create temp files under the sandbox-denied `~/.local/share/zene-bunny-fix/dsh/...` path. No compile happened. |
| Build attempt 2 (same target) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-new-p21-202 cmake --build build --target ItemListPanelTest -j2 > build-2.log 2>&1` | 0 | Built. ccache temp dir moved into the build-cache area; no product change. |
| RED (pre-fix) | `cd build/tests && ctest -R ItemListPanelTest --output-on-failure > test-red-1.log 2>&1` | 8 | `aNegativeControllerRowIsRefusedNotIndexed` aborts: `free(): invalid size`, SIGABRT. The earlier 12 functions pass. |
| Fix | `git diff -- src/gui/noteeditor/ItemListNoteSources.cpp` (2 guards changed) | - | See the diff above. |
| GREEN 1 | `cmake --build ... ItemListPanelTest -j2 > build-3.log`; `ctest -R ItemListPanelTest --output-on-failure > test-green-1.log` | 0; 0 | 100% passed. |
| GREEN 2 (verbose) | `ctest -R ItemListPanelTest -V > test-green-2.log` | 0 | `Totals: 20 passed, 0 failed`. Both new functions show PASS. |
| 4b: save product patch | `git diff -- src/gui/noteeditor/ItemListNoteSources.cpp > fix.patch` | 0 | |
| 4b: revert product fix | `git apply -R fix.patch` | 0 | |
| 4b: rebuild with fix reverted | `cmake --build ... ItemListPanelTest -j2 > build-4b-red.log` | 0 | Built with the product code unfixed. |
| 4b: RED with fix reverted | `ctest -R ItemListPanelTest --output-on-failure > test-4b-red.log` | not captured | The stack-trace handler hung after `free(): invalid next size` / SIGABRT. ctest killed the job at its 900 s timeout (`0% tests passed, 1 tests failed`, `ItemListPanelTest (Timeout)`). The shell chain's exit code was overwritten by a later grep, so no ctest EXIT was recorded. The RED from step 3 (EXIT 8) is the clean failing run. |
| 4b: re-apply fix | `git apply fix.patch` | 0 | |
| 4b: rebuild with fix | `cmake --build ... ItemListPanelTest -j2 > build-4b-green.log` | 0 | |
| 4b: GREEN with fix | `ctest -R ItemListPanelTest -V > test-4b-green.log` | 0 | `Totals: 20 passed, 0 failed`. |
| Family build | `cmake --build build -j2 --target NoteTransformTest NoteGridTest NoteEditorTest CanvasKeyboardTest ItemListButtonsTest NoteEditorFlTest AutomationEditorEditingTest NoteTransformActionsTest TakeLanesPanelTest > build-family.log 2>&1` | 0 | |
| Affected family | `cd build/tests && ctest -R "NoteEditor|ItemListPanel|ItemListButtons|AutomationEditorEditing|TakeLanesPanel|NoteGrid|NoteTransform|CanvasKeyboard" > test-family-2.log 2>&1` | 8 | 10 of 11 pass, including `ItemListPanelTest` (20/20), `NoteEditorTest`, `NoteEditorFlTest`, `ControlNoteEditor`, `ItemListButtonsTest`, `NoteGridTest`, `NoteTransformTest`, `NoteTransformActionsTest`, `CanvasKeyboardTest` and `TakeLanesPanelTest`. |
| Family failure, investigated | `ctest -R AutomationEditorEditingTest --output-on-failure > test-automation-1.log 2>&1` | 8 | `arrowKeysMoveAPointThroughTheCommand`: `'!m_track.isEmpty()' returned FALSE. (no instrument to drive (LMMS_TEST_PLUGIN_DIR))`, at [tests/src/gui/AutomationEditorEditingTest.cpp:106](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-202/tests/src/gui/AutomationEditorEditingTest.cpp). This is an environment precondition that fails before any code this change touches runs. `LMMS_TEST_PLUGIN_DIR` is unset in this session. I did not rerun it on the base. |

Gates. I could not run `feature_check.py` itself: it creates `<worktree>-feature-check/` next to the worktree (`feature_check.py:71`), and the parent `projects/zene-studio-program/` is not writable from this session (PermissionError, log `gates-1.log`). I did not patch the shared script. I ran the eight commands that `feature_check.py` runs, from the worktree, with logs in the LOG FOLDER:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchet gates exit 1 on items in other files: `WasmSandbox::loadModuleBytes`, `XyPadPairTest.cpp`, `DawProjectR...`, `ControlCommandsMixerTools.cpp`, `ControlCommandsSessionRecordInternal.h`, and the `VideoTrackTest.cpp` and `ArrangerTimelineTest.cpp` file-length lines. `grep -E "ItemListNoteSources|ItemListPanelTest"` over both gate logs finds nothing (grep EXIT 1). By `feature_check.py`'s own rule, which counts only items in changed files, both ratchet rows would be PASS. I could not confirm that with the script itself.

Files committed: `src/gui/noteeditor/ItemListNoteSources.cpp`, `tests/src/gui/ItemListPanelTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p21-202.md`. No new files, so no registration in `tests/fork-sources.txt` or `tests/upstream-modifications.txt` is needed. `ItemListPanelTest.cpp` is already in `upstream-modifications.txt` (line 2260), and `ItemListNoteSources.cpp` is a fork file already listed in `fork-sources.txt`.

## Self-check

- **The regression test fails with the product change reverted and passes with it.** Yes. RED: `test-red-1.log`, EXIT 8, `aNegativeControllerRowIsRefusedNotIndexed` aborts with `free(): invalid size`. 4b: product diff reverted with `git apply -R fix.patch` (EXIT 0), rebuilt (EXIT 0), run aborted/timed out (`test-4b-red.log`, EXIT not captured, see table). Fix re-applied with `git apply fix.patch` (EXIT 0), rebuilt (EXIT 0), `test-4b-green.log` EXIT 0, 20/20 passed.
- **The test is deterministic.** It uses no timers, sleeps, or waits. It creates the clip and lane through the same `clip.controllers_set` command the existing tests use, and calls the sources directly with a fixed row of `-1`. The note test uses fixed notes from `init()`. The `QObject owner` scope deletes the source at the end of each test.
- **The fix is at the cause and complete.** Both `setValue` bodies that index by row now refuse negative rows. The other callers and siblings were checked: `ItemListPanel::setData` and `stepCurrent` (valid-index only), `insertAfter`/`remove` (already check `row >= 0` or ignore the row), `value()`/`rowId()` (not changed here; `value()` has the separate use-after-free noted above).
- **No collateral damage.** The guard only rejects a negative row, which no current caller can pass. Valid rows, the `row >= rowCount()` refusal, and every command path are unchanged. No saved data or project format is involved. No new control command; no realtime path touched.
- **Verdict basis.** FIXED is the verdict because the contract was wrong and both sibling paths now refuse negative rows. The finding's "throws" wording is corrected above: the controller path is undefined behaviour, not an exception.
- **Gates.** `feature_check.py` could not run in this session (log-directory permission). The eight underlying commands are listed above; six exit 0. The two ratchets exit 1 on items in other files only.
- **Open items for the reviewer.** `AutomationEditorEditingTest` needs `LMMS_TEST_PLUGIN_DIR`, which I did not set and did not verify on the base. The `ControllerSource::value()` use-after-free is a separate defect outside this cluster.
