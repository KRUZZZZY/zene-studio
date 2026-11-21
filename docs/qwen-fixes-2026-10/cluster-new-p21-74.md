FALSE-POSITIVE

Cluster new-p21-74: "MidiLearnGui::instance() is assumed non-null whenever a GUI exists" (finding at `src/core/ControlCommandsSettings.cpp:459`, `midi.learn_toggle`).

## Evidence

The finding's failure needs `MidiLearnGui::instance()` to return `nullptr`. It cannot.

- `src/gui/MidiLearnGui.cpp:50-54`: `instance()` is a function-local static. It declares `static MidiLearnGui s_instance;` and returns `&s_instance`. The address of a function-local static is never null.
- `include/MidiLearnGui.h:55`: the declaration is `static MidiLearnGui* instance();`. Nothing in the header or the definition makes a null return possible.
- `src/gui/MidiLearnGui.cpp:58-65`: the constructor is what the first call runs. "Not instantiated" is not a reachable state for a caller: calling `instance()` constructs the object, so the handler cannot see an unbuilt instance.
- `src/core/ControlCommandsSettings.cpp:449-459`: the handler first refuses when `gui::getGUI()` or `mainWindow()` is null (lines 450-458), so a null `MainWindow` never reaches line 459. Line 459 is `gui::MidiLearnGui* learn = gui::MidiLearnGui::instance();`, and it has no path to a null `learn`.
- `src/gui/MainWindow.cpp:444`: the `MainWindow` constructor already calls `MidiLearnGui::instance()` during window construction.
- `src/gui/GuiApplication.cpp:217`: `m_mainWindow` is assigned only after `new MainWindow` returns, so a non-null `mainWindow()` means the constructor finished.
- `tests/src/gui/MidiLearnGuiTest.cpp:62-63` already calls `MidiLearnGui::instance()->setArmed(false)` and `->setAction(nullptr)` without a null check, which matches the non-null contract.

The "fix" the finding asks for (a null check plus a typed refusal) would add a branch that can never be taken. No code change is made. Under procedure step 2 this is FALSE-POSITIVE, so there is no RED/GREEN regression test and no product patch to reverse for step 4b.

## Commands and exit codes

| Check | Command | Result |
| --- | --- | --- |
| Locate the call and its definition | `rg -n 'MidiLearnGui::instance|MidiLearnGui\* MidiLearnGui|static MidiLearnGui\*' src` | 4 hits: `src/gui/MidiLearnGui.cpp:50`, `src/gui/MainWindow.cpp:444`, `src/gui/MainWindow.cpp:1553`, `src/core/ControlCommandsSettings.cpp:459` |
| Feature gate runner (as specified) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1` | EXIT=1, but no gate ran: it could not create its log directory `<worktree>-feature-check` next to the worktree (`PermissionError`, `logs/gates-1.log`). The directory is outside this jail, so the runner was not modified. |
| Gate 2, fork sources | `bash tests/fork-sources-gate.sh > .../logs/gate-2-tests-fork-sources-gate.sh.log 2>&1` | EXIT=0 |
| Gate 3, all-sources reproduce | `bash tests/all-sources-reproduce.sh > .../logs/gate-3-tests-all-sources-reproduce.sh.log 2>&1` | EXIT=0 |
| Gate 4, no upstream regression | `bash tests/no-upstream-regression-gate.sh > .../logs/gate-4-tests-no-upstream-regression-gate.sh.log 2>&1` | EXIT=0 |
| Gate 5, check namespace | `tests/scripted/check-namespace > .../logs/gate-5-tests-scripted-check-namespace.log 2>&1` | EXIT=0 |
| Gate 6, no tautology | `bash tests/no-tautology-gate.sh > .../logs/gate-6-tests-no-tautology-gate.sh.log 2>&1` | EXIT=0 |
| Gate 7, doc figures | `python3 tests/doc-figures-gate.py > .../logs/gate-7-tests-doc-figures-gate.py.log 2>&1` | EXIT=0 |
| Ratchet, complexity (gate 4) | `bash tests/complexity-gate.sh --check > .../logs/gate-8-tests-complexity-gate.sh.log 2>&1` | EXIT=1 (7 `REGRESSION:` lines, all pre-existing items; none in a file this branch changes) |
| Ratchet, file length (gate 7) | `bash tests/file-length-gate.sh --check > .../logs/gate-9-tests-file-length-gate.sh.log 2>&1` | EXIT=1 (22 `REGRESSION:` lines, all pre-existing items; none in a file this branch changes) |

The gate commands in the table are the commands `feature_check.py` runs: the six simple gates and the two ratchets, taken from its `SIMPLE` and `RATCHET` lists. Each was run directly in the worktree with the same arguments. `feature_check.py` was not run to completion, so the `PASS/FAIL` table it prints was not produced.

Ratchet reading: the `feature_check.py` rule counts a ratchet item only when it is new and sits in a file the branch changed. This branch changes one file, `docs/qwen-fixes-2026-10/cluster-new-p21-74.md`, and no product file. None of the `REGRESSION:` lines in gates 8 and 9 names that file (checked with `rg`), so under that rule both ratchets would pass. This is derived from the rule, not from a `feature_check.py` run.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p21-74.md` (this report) only. No product file, no test, no registration change.

## Self-check

The non-feature checklist in `submit_check.py` applies to a claim-only closure. Each item is answered from the evidence above.

1. The regression test FAILS with the product change reverted and PASSES with it (exit codes): not applicable. No product change was made, so there is nothing to revert, and the finding's failure is not reachable. Writing a test for a branch that never runs would only pass with or without a fix.
2. The test is deterministic (no timing, sleeps or races): not applicable, no test added.
3. The fix is at the cause and complete, every caller and sibling checked: not applicable, no fix. The callers of `instance()` are `src/core/ControlCommandsSettings.cpp:459`, `src/gui/MainWindow.cpp:444` and `src/gui/MainWindow.cpp:1553`. All three use the pointer as non-null, and `instance()` never returns null.
4. No collateral damage (old projects load as before): no behaviour changed, since no code changed.
5. The verdict cites the code (file:line) or the intended contract: yes, `src/gui/MidiLearnGui.cpp:50-54` (function-local static, `include/MidiLearnGui.h:55`), `src/core/ControlCommandsSettings.cpp:449-459` (refusal before the call), `src/gui/MainWindow.cpp:444`, `src/gui/GuiApplication.cpp:217`.

Items 1-5 above answer the five completion-check questions in order (the questions are the exact text of `submit_check.py`'s non-feature list).

Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-74 > .../logs/submit-check-1.log 2>&1` gave EXIT=1 (log `submit-check-1.log`):

- PASS: commits on the branch; working tree clean; report `docs/qwen-fixes-2026-10/cluster-new-p21-74.md`; tests added or changed (claim-only closure).
- FAIL: `gates (feature_check.py)`. This is the environment blocker above: `feature_check.py` cannot create `<worktree>-feature-check` next to the worktree, because the sandbox write-jail denies that path. The denial is the jail itself (the parent directory mode is `drwxrwxrwx`), so it was not bypassed. The six simple gates and the two ratchets were run directly and are logged above; the ratchets exit 1 on train items not in files this branch changes.
- The five `?` items are answered in items 1-5 above.

Verdict: FALSE-POSITIVE. The one FAIL is the jail blocking `feature_check.py`'s log directory, not a code problem. It needs the runner's output directory moved inside the jail, or a run of `feature_check.py` from an unjailed shell, to turn green.
