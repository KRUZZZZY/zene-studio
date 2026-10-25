VERDICT: FIXED
5564: FIXED
new-p14-454: FIXED
new-p14-455: FIXED

## Decisions and test matrix

| Cluster | Decision and evidence | Normal path | Failure / boundary paths | Audio thread | Build coverage |
| --- | --- | --- | --- | --- | --- |
| 5564 | REAL: `paletteActivate()` triggered action entries directly, while `runNow()` alone called `confirmAction()`. The regression drives a menu action tagged `project.new` through that public path. | Accept a confirmation and trigger once. | Reject confirmation, return “cancelled,” and leave the action untouched. No retry, exhaustion, or reconfiguration state exists in this synchronous UI dispatch. | UI thread only. | Built with the configured Linux x86_64 / Qt 6.12 offscreen build. No platform-specific API was added. |
| new-p14-454 | REAL: `itemFor()` used one fixed “cannot be undone” suffix for every `entry.irreversible`; `commandUndoWarning()` already differentiates manual fallback. | Render `plugin.load` and show that undo may not fully reverse it. | Keep the row and detail warning consistent. Row rendering has no retry, exhaustion, or reconfiguration path. | UI thread only. | Same Linux Qt 6.12 offscreen target; no platform-specific API was added. |
| new-p14-455 | REAL: `CommandPalette::eventFilter()` watched only the filter field; Shift+Enter delivered to the list reached `itemActivated` and the Configure action. | Filter-field Enter remains primary; list Shift+Enter runs a form with defaults. | List Enter without Shift remains primary. A form with an unfilled required argument remains blocked by `paletteCanRunWithDefaults()` (covered by the existing `unresolvedArgumentsMakeConfigurePrimary` test). No retry or exhaustion state exists. | UI thread only. | Same Linux Qt 6.12 offscreen target; no platform-specific API was added. |

The affected test target was already registered as `CommandPaletteTest` in `tests/CMakeLists.txt`; CTest reports one affected-family test (`1/1`). Each regression is a separate QtTest slot. The confirmation case uses a zero-delay `QTimer` callback to answer the modal `QMessageBox` inside its nested event loop; it does not wait for wall-clock time. Shift+Enter is sent synchronously to the list with `QTest::keyClick`.

## RED/GREEN and reverse-patch proofs

All build and test output was written under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs`. Commands below were run from the worktree unless the working directory is stated as `build/tests`.

### 5564 — irreversible menu action confirmation

Evidence: `src/gui/CommandPalette.cpp:218-222` already confirmed registry dispatch, but the former action branch at `:243-247` triggered without confirmation. The fixed action branch now confirms with the same `commandUndoWarning()` and cancellation outcome. `tests/src/gui/CommandPaletteTest.cpp:244` checks both No and Yes answers and action trigger counts.

- RED build: `cmake --build build --target CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/001-build-red-5564.log 2>&1; echo EXIT=$?` — **EXIT=0**.
- RED test: `ctest -R '^CommandPaletteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/002-test-red-5564.log 2>&1; echo EXIT=$?` from `build/tests` — **EXIT=8**; `askedBeforeAccept` was false.
- GREEN product build: `cmake --build build --target zene CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/003-build-green-5564.log 2>&1; echo EXIT=$?` — **EXIT=0**. After the test's Yes answer was changed to click the real QMessageBox button, the regression target was rebuilt with `cmake --build build --target CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/008-build-confirm-5564.log 2>&1; echo EXIT=$?` — **EXIT=0**.
- GREEN test: `ctest -R '^CommandPaletteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/009-test-green-5564.log 2>&1; echo EXIT=$?` from `build/tests` — **EXIT=0**, `1/1` passed.
- 4b reverse command: `git diff -- src/gui/CommandPalette.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch; echo EXIT=$?` — **EXIT=0**.
- 4b reverted build: `cmake --build build --target zene CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/010-build-redproof-5564.log 2>&1; echo EXIT=$?` — **EXIT=0**. Reverted test used the same CTest command — **EXIT=8**, `askedBeforeAccept` false (`011-test-redproof-5564.log`).
- 4b restore command: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch; echo EXIT=$?` — **EXIT=0**.
- 4b restored build: `cmake --build build --target zene CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/012-build-greenproof-5564.log 2>&1; echo EXIT=$?` — **EXIT=0**. The same CTest command — **EXIT=0**, `1/1` passed (`013-test-greenproof-5564.log`).

### new-p14-454 — manual-fallback row warning

Evidence: the former `itemFor()` suffix at `src/gui/CommandPalette.cpp:285` ignored the reversibility class. The fixed row at `:289-294` derives its wording from `commandUndoWarning()`, matching the detail text. `tests/src/gui/CommandPaletteTest.cpp:358` renders the real `plugin.load` palette row and checks its label.

- RED build: `cmake --build build --target CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/015-build-red-454.log 2>&1; echo EXIT=$?` — **EXIT=0**.
- RED test: `ctest -R '^CommandPaletteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/016-test-red-454.log 2>&1; echo EXIT=$?` from `build/tests` — **EXIT=8**; `plugin.load` was displayed as “cannot be undone.”
- GREEN build: `cmake --build build --target zene CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/017-build-green-454.log 2>&1; echo EXIT=$?` — **EXIT=0**.
- GREEN test: `ctest -R '^CommandPaletteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/018-test-green-454.log 2>&1; echo EXIT=$?` from `build/tests` — **EXIT=0**, `1/1` passed.
- 4b patch isolation: `cp /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/019-fix-5564.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/019-fix-5564.patch && git diff -- src/gui/CommandPalette.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch && git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/019-fix-5564.patch; echo EXIT=$?` — **EXIT=0**; this isolated only the row-label fix while leaving 5564 fixed.
- 4b reverse command: `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch; echo EXIT=$?` — **EXIT=0**. Reverted build: `cmake --build build --target zene CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/020-build-redproof-454.log 2>&1; echo EXIT=$?` — **EXIT=0**. Same CTest command — **EXIT=8**, label assertion failed (`021-test-redproof-454.log`).
- 4b restore command: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch; echo EXIT=$?` — **EXIT=0**. Restored build `cmake --build build --target zene CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/022-build-greenproof-454.log 2>&1; echo EXIT=$?` — **EXIT=0**. Same CTest command — **EXIT=0**, `1/1` passed (`023-test-greenproof-454.log`).

### new-p14-455 — Shift+Enter from the list

Evidence: the previous `eventFilter()` condition at `src/gui/CommandPalette.cpp:417` watched only `m_filter`. The fixed constructor installs the filter on `m_list` too (`:319-322`), and the handler now preserves Shift for Return and Enter while leaving list Up/Down to Qt (`:427-447`). `tests/src/gui/CommandPaletteTest.cpp:376` sends Shift+Enter to a selected form row and checks that its handler runs with defaults.

- RED build: `cmake --build build --target CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/026-build-red-455.log 2>&1; echo EXIT=$?` — **EXIT=0**.
- RED test: `ctest -R '^CommandPaletteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/027-test-red-455.log 2>&1; echo EXIT=$?` from `build/tests` — **EXIT=8**; the handler count stayed at 2 instead of increasing to 3.
- GREEN build: `cmake --build build --target zene CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/028-build-green-455.log 2>&1; echo EXIT=$?` — **EXIT=0**.
- GREEN test: `ctest -R '^CommandPaletteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/029-test-green-455.log 2>&1; echo EXIT=$?` from `build/tests` — **EXIT=0**, `1/1` passed.
- 4b patch isolation: `cp /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/030-fix-454.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/019-fix-5564.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/030-fix-454.patch && git diff -- src/gui/CommandPalette.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch && git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/030-fix-454.patch && git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/019-fix-5564.patch; echo EXIT=$?` — **EXIT=0**; this isolated only the list-key fix while leaving the other two fixes applied.
- 4b reverse command: `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch; echo EXIT=$?` — **EXIT=0**. Reverted build: `cmake --build build --target zene CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/031-build-redproof-455.log 2>&1; echo EXIT=$?` — **EXIT=0**. Same CTest command — **EXIT=8**, handler-count assertion failed (`032-test-redproof-455.log`).
- 4b restore command: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/fix.patch; echo EXIT=$?` — **EXIT=0**. Restored build `cmake --build build --target zene CommandPaletteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/033-build-greenproof-455.log 2>&1; echo EXIT=$?` — **EXIT=0**. Same CTest command — **EXIT=0**, `1/1` passed (`034-test-greenproof-455.log`).

## Gates, registration and extra repair

The train snapshot had 1,148 vendored `plugins/SfzPlayer/sfizz/` paths in `tests/all-sources.txt`, although its published recipe explicitly excludes that third-party tree. I regenerated the entry list from the recipe without hand editing; this was necessary for Gate 9 and leaves only the recipe's tracked first-party sources. The three regressions use the existing `CommandPaletteTest` target. The changed test source and this report are named in `tests/fork-sources.txt`.

| Gate | Result | Evidence |
| --- | --- | --- |
| `feature_check.py` | PASS, **EXIT=0** | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5564`; `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/040-feature-check-final.log`. All rows pass: fork-sources, all-sources-reproduce, no-upstream-regression, namespace, no-tautology, doc-figures, complexity, file-length. |
| `submit_check.py` | PASS, **EXIT=0** (`PASS - submit`) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5564 5564`; `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5564.r1/logs/041-submit-check-postcommit.log`. |

## Self-check

- **Regression fails without each fix and passes with it:** cluster 5564: CTest EXIT=8 at `002` and reverse proof `011`, EXIT=0 at `009` and restored proof `013`. new-p14-454: EXIT=8 at `016` and `021`, EXIT=0 at `018` and `023`. new-p14-455: EXIT=8 at `027` and `032`, EXIT=0 at `029` and `034`. Each reverse patch contained only that bug's product hunk, keeping sibling fixes applied.
- **Deterministic tests:** the confirmation test answers the modal question through a zero-delay Qt event-loop callback; no sleep, wall-clock assertion, or race is used. The shortcut test sends a synchronous `QTest::keyClick` directly to the list. The row-render test reads the created `QTreeWidgetItem` synchronously.
- **Cause and callers checked:** `paletteActivate()` now confirms action entries and still dispatches registry entries through guarded `runNow()`. `paletteRunWithDefaults()` routes action entries through `paletteActivate()`. `itemFor()` uses the same reversibility warning as the detail text. `CommandPalette::eventFilter()` handles Return/Enter from both watched widgets, preserves Shift, keeps filter Up/Down navigation, and lets the list keep its own Up/Down handling; mouse activation remains connected to `activateRow()`.
- **No collateral data or realtime change:** no saved-project format, command registration, limits, undo behavior, or audio callback changed. The fixes are confined to palette UI dispatch, row text and key handling; they allocate no new audio-thread work.
- **Not-real verdict evidence:** not applicable; all three findings were REAL and are fixed above.

## Files committed

Fix commit `d5caa3d49` contents: `src/gui/CommandPalette.cpp`, `tests/src/gui/CommandPaletteTest.cpp`, `tests/fork-sources.txt`, `tests/all-sources.txt`, and `docs/qwen-fixes-2026-10/cluster-5564.md`.
