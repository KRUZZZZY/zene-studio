FALSE-POSITIVE

Finding: "window.select can dereference a null shell" (`src/gui/ControlCommandsSelection.cpp:52`, `mounted()` calls `g->mainWindow()->shell()->panel(...)` without a null check).

Evidence: the crash needs a live main window whose `shell()` is null. The shipped code cannot produce one.

- `shell()` returns `m_shell` ([include/MainWindow.h:57](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25/include/MainWindow.h:57)); the member is initialised to `nullptr` at [include/MainWindow.h:296](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25/include/MainWindow.h:296).
- The only assignment in `src/` is `m_shell = new Shell( this );` in the `MainWindow` constructor at [src/gui/MainWindow.cpp:175](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25/src/gui/MainWindow.cpp:175). Nothing resets it afterwards. `new` either returns a valid object or throws, so the pointer is non-null once the constructor returns.
- `GuiApplication` stores the window only after construction: `m_mainWindow = new MainWindow;` at [src/gui/GuiApplication.cpp:217](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25/src/gui/GuiApplication.cpp:217). So `getGUI()->mainWindow()` is non-null only when `m_shell` is already set.
- The only other write to `m_mainWindow` is the reset to `nullptr` in `childDestroyed` at [src/gui/GuiApplication.cpp:286](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25/src/gui/GuiApplication.cpp:286). After destruction, `mainWindow()` returns `nullptr`, which the existing guards already refuse.
- Every path into `mounted()` is behind a `mainWindow()` check. `select()` refuses with a typed `Requires` error at [src/gui/ControlCommandsSelection.cpp:96](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25/src/gui/ControlCommandsSelection.cpp:96). `shellSelectionState()` returns an empty object at [src/gui/ControlCommandsSelection.cpp:120](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25/src/gui/ControlCommandsSelection.cpp:120), and it is called from [src/gui/ControlCommandsWindow.cpp:156](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25/src/gui/ControlCommandsWindow.cpp:156).
- The sibling helper that does check `shell()` is `interfaceOrNull()` at [src/gui/ControlCommandsWindow.cpp:129](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25/src/gui/ControlCommandsWindow.cpp:129). That is defence in depth, not evidence that a null shell is a reachable state.

Verdict reasoning: the finding's precondition ("a main window that has no shell") has no reachable path in this tree. Per procedure step 2 the finding is not real, so there is no product change, no RED/GREEN regression test, and no step-4b patch reversal. Adding a null check for an unreachable state would be untestable with the real `MainWindow` and would change no behaviour. Confidence in this verdict is high because the only `m_shell` write is in the constructor and the only `m_mainWindow` write outside construction is the reset to `nullptr`.

Not verified by running: I did not build a window without a shell, because no such window can be constructed through the shipped code. This verdict rests on the code reading above.

| Evidence | Command | Result |
| --- | --- | --- |
| Find every write to `m_shell` and `m_mainWindow` | `rg -n 'm_shell *=|m_mainWindow *=' src include` | EXIT=0; lines listed above (MainWindow.cpp:175, GuiApplication.cpp:217, GuiApplication.cpp:286) |
| Feature gate (first attempt) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-25.r1/logs/feature-check-1.log 2>&1; echo EXIT=$?` | EXIT=1; crashed with `PermissionError` creating `zene-qwen-new-p23-25-feature-check` outside the worktree (write-jail). No gate ran. Log: `logs/feature-check-1.log` |
| Feature gate (same script, log directory redirected) | A copy of `feature_check.py` in `$TMPDIR` with two lines changed: `ROOT` pinned to the shared `zene-bunny-fix/qwen` directory (so `FIX-BASE` and the cached `gate-baselines` resolve) and `logs` pointed at `logs/feature-check-1/`. Gate logic and limits are unchanged. Run as `python3 -I <copy> <worktree> > logs/feature-check-2.log 2>&1; echo EXIT=$?` | EXIT=0; 8/8 PASS, RESULT: PASS. Log: `logs/feature-check-2.log` |

Gate table (from `logs/feature-check-2.log`, base `639e1d23e`, 0 changed product files):

| Gate | Result |
| --- | --- |
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS (no new item in your files) |
| file-length | PASS (no new item in your files) |

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p23-25.md` only. No product or test file changed, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no registration.

## Self-check

Completion check, run before this commit: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-25 new-p23-25 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-25.r1/logs/submit-check-1.log 2>&1; echo EXIT=$?` gave EXIT=1 (`logs/submit-check-1.log`). Its items, with the answer for each:

- **Commits on the branch / clean tree:** these two FAILs are closed by this commit and the clean `git status` that follows it. Re-run after commit: `logs/submit-check-2.log`.
- **Report has a `Self-check` section:** this section.
- **Tests added or changed:** PASS by the checker (claim-only closure). No test was added, because the finding is not real (verdict above).
- **Gates (`feature_check.py`):** FAILS in the completion check, for an environmental reason and not a gate result. The shared `feature_check.py` creates its log folder `<worktree>-feature-check` in the worktree's parent directory, and the write-jail denies that write (`PermissionError`, `logs/feature-check-1.log`). I did not edit the shared script. The same gate logic, run from a copy whose only changes are the two path lines listed above, gave 8/8 PASS and EXIT=0 (`logs/feature-check-2.log`). The completion check needs its log directory writable to show this as PASS.
- **Regression test RED/GREEN exit codes:** not applicable. No product change was made, so there is no fix to reverse for step 4b and no RED/GREEN run.
- **Deterministic test:** not applicable. No test was written.
- **Fix at the cause, every caller and sibling:** not applicable, no fix. Callers checked for the verdict: `select()` (ControlCommandsSelection.cpp:96), `shellSelectionState()` (ControlCommandsSelection.cpp:120, called from ControlCommandsWindow.cpp:156), and the sibling guard `interfaceOrNull()` (ControlCommandsWindow.cpp:129).
- **No collateral damage:** no code changed, so saved projects and control commands are untouched. No control command was added and the canary is unaffected.
- **FALSE-POSITIVE evidence (file:line):** `m_shell` is assigned only at src/gui/MainWindow.cpp:175, inside the constructor; `m_mainWindow` is assigned after construction at src/gui/GuiApplication.cpp:217 and reset to `nullptr` only at GuiApplication.cpp:286. So `mainWindow()` non-null implies `shell()` non-null. The `rg` search that shows this is in `logs/rg-1.log` (EXIT=0).
