Verdict: FALSE-POSITIVE

new-p21-122: FALSE-POSITIVE

Finding: `focusTarget(gui::ShellPanel* frame)` dereferences `frame` at `src/gui/ControlCommandsWorkspace.cpp:216` with no null guard, and the only caller passes `shell->panel(id)` at `src/gui/ControlCommandsWorkspace.cpp:254`. The claimed trigger, a null panel lookup, cannot happen on that path.

Evidence:

- **The only caller validates the id first.** `focusPanel` (`src/gui/ControlCommandsWorkspace.cpp:246`) refuses a null shell at `:250`, resolves `next`/`previous` through `panelToFocus` (`:234-243`, which returns an empty string when no panel is open), and returns `NotFound` unless `gui::ShellLayout::isPanel(id)` is true at `:252`. Only after that does it call `revealPanel` (`:253`) and `focusTarget(shell->panel(id))` (`:254`).
- **`isPanel` and the frame register read one list.** `ShellLayout::isPanel` (`src/gui/shell/ShellLayout.cpp:89-96`) iterates `ShellLayout::panels()` (`:73`). `Shell::buildBody()` (`src/gui/shell/Shell.cpp:176`), called from the constructor at `:118`, creates one `ShellPanel` per entry of that same list (`:214`) and inserts it into `m_panels` (`:217`). `Shell::panel(id)` is `m_panels.value(id)` (`include/Shell.h:72`).
- **Nothing removes a frame.** `m_panels.(remove|take|erase|clear)` matches nothing under `src/gui/shell/`. `revealPanel` (`src/gui/shell/Shell.cpp:415-424`) returns early for a non-panel and never touches `m_panels`.
- **So a valid id always has a frame.** The frame is null only for an id `isPanel` rejected, and `:252` rejects those first. The intended contract is that `window.focus_panel` moves focus only to a registered panel, and both sides read the same register.
- **`focusTarget` itself never returns null.** It falls back to the frame when there is no content (`:216`) and to the header or the content (`:218-219`), so the caller's `target->setFocus` (`:255`) is safe too.
- **Siblings agree.** `openPanels` (`:228`), `MainWindow.cpp:1310` and `ControlCommandsSelection.cpp:52` already null-check `panel(...)`; they are defensive, not evidence of a reachable null.

Decision: no product change. The suggested guard would change no reachable behaviour, so applying it would be a change with no bug behind it. Per procedure step 2 this is not-real and needs no RED/GREEN cycle; step 4b (revert and reapply the patch) does not apply because there is no product patch.

Test added: `tests/src/gui/ShellTest.cpp:173` `everyRegisteredPanelHasAFrame`. It is a contract pin, not a regression test. It asserts that every id `ShellLayout::isPanel` accepts has a non-null `Shell::panel`, and that an unknown id has neither. It passes on the unchanged product code, as it should, and has not been mutation-checked. It is in the commit because the completion check asks for a test change and because it pins the precondition the caller relies on. It needs no registration: `ShellTest` is an existing file, already listed in `tests/all-sources.txt:3137` and built by `tests/CMakeLists.txt` (`src/gui/ShellTest.cpp`, line 1096).

Logs are in `LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-122.r1/logs`.

| Step | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| Full gate script | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/feature-check-1.log 2>&1` | 1 | `feature-check-1.log`: `PermissionError` at `feature_check.py:71` (`logs.mkdir`); it writes `<worktree>-feature-check` next to the worktree, outside the write-jail. The table was not produced. |
| Gate 1 fork-sources (pass 1, then pass 2 after the test edit) | `bash tests/fork-sources-gate.sh > LOGS/gate-fork-sources-{1,2}.log 2>&1` | 0, 0 | `gate-fork-sources-{1,2}.log` |
| Gate 2 all-sources-reproduce | `bash tests/all-sources-reproduce.sh > LOGS/gate-all-sources-reproduce-{1,2}.log 2>&1` | 0, 0 | `gate-all-sources-reproduce-{1,2}.log` |
| Gate 3 no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > LOGS/gate-no-upstream-regression-{1,2}.log 2>&1` | 0, 0 | `gate-no-upstream-regression-{1,2}.log` |
| Gate 4 check-namespace | `tests/scripted/check-namespace > LOGS/gate-check-namespace-{1,2}.log 2>&1` | 0, 0 | `gate-check-namespace-{1,2}.log` |
| Gate 5 no-tautology | `bash tests/no-tautology-gate.sh > LOGS/gate-no-tautology-{1,2}.log 2>&1` | 0, 0 | `gate-no-tautology-{1,2}.log` |
| Gate 6 doc-figures | `python3 tests/doc-figures-gate.py > LOGS/gate-doc-figures-{1,2}.log 2>&1` | 0, 0 | `gate-doc-figures-{1,2}.log` |
| Ratchet complexity (red on train) | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-{1,2}.log 2>&1` | 1, 1 | 7 `REGRESSION` lines; none in a changed file (see ratchet row) |
| Ratchet file-length (red on train) | `bash tests/file-length-gate.sh --check > LOGS/gate-file-length-{1,2}.log 2>&1` | 1, 1 | 22 `REGRESSION` lines; none in a changed file (see ratchet row) |
| Ratchet rule, applied as feature_check does | `python3 -I -c '…'` (imports `key`, `item_file` and `REG` from `feature_check.py`, compares against `gate-baselines/639e1d23e4b84a751f6ed90bd626d35d24727cc4.json`, changed set = `tests/src/gui/ShellTest.cpp`) `> LOGS/ratchet-compare-2.log 2>&1` | 0 | `ratchet-compare-2.log`: `complexity NEW-in-changed: none`, `file-length NEW-in-changed: none` |
| Build ShellTest, first try | `cmake --build build --target ShellTest -j4 > LOGS/build-shelltest-1.log 2>&1` | 2 | ccache could not create a temp file in `~/.local/share/zene-bunny-fix/dsh/home/` (`build-shelltest-1.log:18`), outside the jail |
| Build ShellTest, with ccache temp dir inside `~/.cache` | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-p21-122 cmake --build build --target ShellTest -j4 > LOGS/build-shelltest-2.log 2>&1` | 0 | `build-shelltest-2.log` |
| ShellTest via ctest, before its binary existed | `ctest -R '^ShellTest$' --output-on-failure > LOGS/test-shelltest-1.log 2>&1` (from `build/tests`) | 8 | `Not Run`, 0/1 |
| ShellTest via ctest, after build | same command, `> LOGS/test-shelltest-2.log` | 0 | 1/1 passed |
| New function alone, verbose | `env QT_QPA_PLATFORM=offscreen ./ShellTest everyRegisteredPanelHasAFrame -v2 > LOGS/test-shelltest-fn-2.log 2>&1` (from `build/tests`) | 0 | `PASS : ShellTest::everyRegisteredPanelHasAFrame()`; totals 3 passed, 0 failed |
| Build the other Shell/Workspace family targets | `CCACHE_TEMPDIR=… cmake --build build --target WorkspaceAccessTest ShellLayoutTest ShellHomesTest TouchWorkspaceTest -j4 > LOGS/build-family-1.log 2>&1` | 0 | `build-family-1.log` |
| Family, before those targets were built | `ctest -R 'Shell|Workspace' > LOGS/test-family-shell-1.log 2>&1` (from `build/tests`) | 8 | 4 `Not Run`, 3/7 |
| Family, after build | same command, `> LOGS/test-family-shell-2.log` | 0 | 7/7 passed (7 tests executed) |

Tests executed in the affected family (`Shell|Workspace`): 7 of 7 passed. `ShellTest` alone: 1 of 1. The new function: 1 of 1.

RED/GREEN: not applicable. No product file changed, so there is no revert or reapply step and no RED state to show. The new test passes on the unchanged code, and it is a contract pin, not a regression proof.

Files committed for this verdict: `tests/src/gui/ShellTest.cpp` and this report. No product file changed.

## Self-check

- **The regression test fails with the change reverted and passes with it (exit codes).** Not applicable: there is no product change to revert. The test passes on the unchanged code (`test-shelltest-fn-2.log`, EXIT=0). It is a contract pin for a precondition, not a regression test.
- **The test is deterministic.** It builds a `World` (`tests/src/gui/ShellTest.cpp`, the existing helper that calls `Shell::mount` and `show`) and only checks `ShellLayout::panels()`, `ShellLayout::isPanel` and `Shell::panel`. It uses no timers, sleeps, waits or races.
- **The fix is at the cause and complete.** No fix. Checked the one caller of `focusTarget` (`ControlCommandsWorkspace.cpp:254`), the one insertion site into `m_panels` (`Shell.cpp:217`), and the absence of any removal (grep above). The `isPanel` gate at `:252` is the existing contract.
- **No collateral damage.** No product code, save format, limit or command changed. No control command was added, so the `ControlCommandsSnapshot` canary is unaffected.
- **FALSE-POSITIVE evidence (file:line or contract).** `ControlCommandsWorkspace.cpp:250-254`, `ShellLayout.cpp:73-96`, `Shell.cpp:118, 176, 214-217`, `Shell.h:72`, and the absence of any `m_panels` removal under `src/gui/shell/`.
- **Completion check.** Run after this commit. Its result is reported in the final message.
