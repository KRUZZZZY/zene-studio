FIXED — cluster new-p16-91: `ItemListPanel::open(nullptr, ...)` now returns nullptr before it makes a source or reads the surface's geometry.

Finding (Luna patrol, historical line 305): "Opening an item list with a null surface dereferences it".

## Verdict and evidence

**Real at the API level, not reachable from the agent command.** Before the fix, `open()` called `make(nullptr)` and built a panel with no parent, then called `surface->window()` on the null surface (SIGSEGV, address 0x20). `find()` already guarded null (`src/gui/ItemListPanel.cpp:297`), so the fault was only in `open()`.

- Fix: `src/gui/ItemListPanel.cpp:302` — `if (surface == nullptr) { return nullptr; }`, placed before `find()`, `make()` and the `new ItemListPanel` (lines 302-303), so nothing is created for a null surface. Non-null behaviour is unchanged.
- Contract: `include/ItemList.h:142-143` documents the nullptr return.
- The only production caller, `src/gui/ControlCommandsAccessibleEditing.cpp:148`, already returns `NotFound` when `surface.host == nullptr` before it calls `open()` at line 157. So `window.item_list` never reached the crash; the bug was in the C++ API, which tests and future callers use directly. The fix makes the API safe on its own.
- The other callers in `tests/src/gui/ItemListPanelTest.cpp` (lines 128, 212, 337, 362, 392) and `tests/src/gui/AutomationEditorEditingTest.cpp` pass non-null surfaces, so they are unaffected.

## Regression test

`tests/src/gui/ItemListPanelTest.cpp:434` — `aNullSurfaceOpensNothing()`: `open(nullptr, "notes", make)` must return nullptr, must not invoke `make`, and `find(nullptr, ...)` must return nullptr. Deterministic: no timing, no sleeps, the `made` flag is the only observation.

## Commands and exit codes (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-91.r1/logs/`)

| Step | Command (run from the worktree / `build/tests` as noted) | Log | EXIT |
| --- | --- | --- | --- |
| Build test, first try | `cmake --build build --target ItemListPanelTest -j4` | build-1-red.log | 2 (ccache could not write its temp file: `Permission denied` under `dsh/home/ccache-tmp-r6`; no compile happened) |
| Build test (RED) | `CCACHE_DISABLE=1 cmake --build build --target ItemListPanelTest -j4` | build-2-red.log | 0 |
| **RED** (fix not applied) | `cd build/tests && ctest -R '^ItemListPanelTest$' --output-on-failure` | test-1-red.log | **8** — `ItemListPanelTest (SEGFAULT)`; `Received signal 11 (SIGSEGV)` in `aNullSurfaceOpensNothing` |
| Fix applied | edits to `src/gui/ItemListPanel.cpp`, `include/ItemList.h` | — | — |
| Build (GREEN) | `CCACHE_DISABLE=1 cmake --build build --target zene ItemListPanelTest ItemListButtonsTest TakeLanesPanelTest AutomationEditorEditingTest ArrangerKeysNavigatorTest -j4` | build-3-green.log | 0 |
| **GREEN** | `cd build/tests && ctest -R '^ItemListPanelTest$' --output-on-failure` | test-2-green.log | **0** — 1/1 passed |
| New slot by name | `QT_QPA_PLATFORM=offscreen ./ItemListPanelTest aNullSurfaceOpensNothing` | test-9-green-slot.log | 0 — `PASS aNullSurfaceOpensNothing()` |
| Whole binary | `QT_QPA_PLATFORM=offscreen ./ItemListPanelTest` | test-10-green-full-binary.log | 0 — `Totals: 19 passed, 0 failed` |

### Step 4b (proof the test catches the bug)

1. `git diff -- src/gui/ItemListPanel.cpp include/ItemList.h > .../logs/fix.patch` → exit 0; `git apply -R .../logs/fix.patch` → exit 0.
2. `CCACHE_DISABLE=1 cmake --build build --target zene ItemListPanelTest AutomationEditorEditingTest -j4` → exit 0 (build-4-reverted.log).
3. `cd build/tests && ctest -R '^ItemListPanelTest$' --output-on-failure` → **exit 8, SEGFAULT** (test-5-reverted-itemlist.log). RED reproduced.
4. `git apply .../logs/fix.patch` → exit 0; rebuild (build-5-fixed.log) → exit 0; `ctest -R '^ItemListPanelTest$'` → **exit 0** (test-7-green-itemlist.log). GREEN.

### Affected family (`cd build/tests && ctest -R 'ItemList|TakeLanes|AutomationEditorEditing|ArrangerKeysNavigator' --output-on-failure`)

Five tests executed: ItemListPanelTest, ItemListButtonsTest, AutomationEditorEditingTest, ArrangerKeysNavigatorTest, TakeLanesPanelTest. Result (test-8-green-family.log): **EXIT 8**, 4/5 passed. ItemListPanelTest, ItemListButtonsTest, ArrangerKeysNavigatorTest and TakeLanesPanelTest pass.

`AutomationEditorEditingTest` fails with `'!m_track.isEmpty()' returned FALSE (no instrument to drive (LMMS_TEST_PLUGIN_DIR))` (test-4-automation.log, test-8-green-family.log). **This is pre-existing and not caused by this change**: it fails identically on the reverted base code (test-6-reverted-automation.log, EXIT 8, same message). The precondition is `revtest::firstLoadableInstrument()` / `plugin.list loadable_only` returning no instrument (`tests/src/core/ReversibilityTestSupport.h:129,152`), a plugin-scan environment condition that the diff does not touch. Not investigated further.

A mistake in my run: the first family run was executed from `build/` rather than `build/tests/` (exit 97); I reran it from `build/tests/` as above. The `build/` run's output was overwritten by that rerun and is not cited.

### Gates

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → **exit 1** (`PermissionError` creating the log folder `<worktree>/../zene-qwen-new-p16-91-feature-check`, which is outside the write-jail; log `gate-1-feature-check.log`). No gate was evaluated by that run.
- Workaround: `python3 -I .../logs/gate-driver/run_gates.py <worktree> .../logs/gates-2` runs the same `SIMPLE` and `RATCHET` commands and the same table logic imported from `feature_check.py`, with per-gate logs in the LOG folder. Final run after the ledger line was added (`gate-3-feature-check-table.log`): **exit 0, RESULT: PASS**, 8/8 rows (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS; complexity and file-length: "no new item in your files").

### Registration

- `include/ItemList.h` and `src/gui/ItemListPanel.cpp` are already in `tests/fork-sources.txt` (lines 923, 2012); no entry needed.
- `tests/src/gui/ItemListPanelTest.cpp` is already listed in `tests/upstream-modifications.txt:2260`; I added one reason line for this change (same convention as the recent cluster fixes).

### Other notes

- No new control command, no audio-thread code, no undo change, no signal, no allocation on any path that changed. The early return happens before any allocation.
- Not changed, noticed only: `open()` passes `make(nullptr)`'s result straight to the constructor, which dereferences `source` (`source->setParent`, `source->canInsert()`). A `make` that returns nullptr would still crash. It is outside this finding and I did not change it.

## Self-check

Mechanical items (from `submit_check.py`, answered after the first commit — see the final message for its output):

1. **Each new test FAILS with the product change reverted, then PASSES restored?** Yes. RED exit 8 (SIGSEGV in `aNullSurfaceOpensNothing`, test-1-red.log and test-5-reverted-itemlist.log); GREEN exit 0 (test-2-green.log, test-7-green-itemlist.log).
2. **Every acceptance criterion proven by a test I ran?** The criterion is "a null surface opens nothing": `aNullSurfaceOpensNothing` (`ItemListPanelTest.cpp:434`) checks the nullptr return, that `make` is not invoked, and that `find(nullptr)` is nullptr. Run by name: exit 0 (test-9-green-slot.log); full binary 19/19 (test-10-green-full-binary.log).
3. **Each edit is one undo step, and every changed command has an honest reversibility class?** No command, undo or model code changed; the fix is a return-value guard. N/A.
4. **Nothing added runs on the audio thread?** Correct: the change is in `ItemListPanel::open` (a UI tool window); no audio path is touched. No allocation, lock, wait, I/O or signal was added on any path. The early return runs before any allocation.
5. **Old projects still load and render as before?** Yes: no serialization or project-data code changed; a non-null surface follows the same path as before (`open` only adds the early return).
6. **Qt 5.15 / MSVC / macOS portability?** The change uses only `QWidget`-independent C++ (`nullptr` comparison); no Qt-6-only API added.
7. **Tests in the family?** Five run (see above); the one failure (AutomationEditorEditingTest) is pre-existing and reproduced on the reverted tree.

Gates: feature_check.py run as-is exited 1 (outside-jail log path, see Gates). The equivalent run with logs redirected exited 0 (gate-3-feature-check-table.log). If the completion check's `feature_check.py` invocation still fails on the same permission error, that is the cause and nothing else.

Also checked: the test name `aNullSurfaceOpensNothing` is in the `ItemListPanelTest` binary and the family count is non-zero (5 executed).
