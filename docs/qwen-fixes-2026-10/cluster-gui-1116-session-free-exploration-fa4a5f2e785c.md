FIXED

# cluster gui-1116-session-free-exploration-fa4a5f2e785c - Closing Settings leaves the main window unresponsive

## Verdict and evidence

**REAL.** `SetupDialog` made itself application-modal and called `show()` from its own constructor:
`src/gui/modals/SetupDialog.cpp:173` (`setModal(true)`) and, before this fix, the last line of the constructor
(`show()`, formerly line 994). The command's non-modal path is
`src/gui/ControlCommandsShell.cpp:139-151`: construct the dialog, then `setModal(false)`, then `show()`. Because the
constructor had already shown it while modal, Qt had entered the dialog into its modal state. The later
`setModal(false)` did not remove it from `QApplication`'s modal stack: `QApplication::activeModalWidget()` still
returned the Settings dialog. Qt routes real input to the active modal widget, so the main window dropped menu
clicks and Space - exactly the reported "after Settings closed, nothing responds". The first input after the close
was consumed while the delete-on-close dialog was still being deleted.

The regression reproduces the mechanism directly on the old code:

```
FAIL!  : MainWindowTest::settingsOpensWithoutBecomingTheActiveModalWidget()
         'QApplication::activeModalWidget() != settings' returned FALSE.
         (Settings is the active modal widget, so the main window cannot answer input)
```

The other path into this dialog is `MainWindow`'s first-run and audio-device setup
(`src/gui/MainWindow.cpp:615-648`), which calls `SetupDialog sd; sd.exec()`; `exec()` shows the dialog itself, so
removing the constructor's `show()` leaves that modal path exactly as it was.

## Fix

`src/gui/modals/SetupDialog.cpp` no longer calls `show()` at the end of its constructor. The caller that wants a
non-modal dialog (`window.settings`) can now set `setModal(false)` before the first show, so the dialog never enters
Qt's modal state. `MainWindow`'s `exec()` paths are unchanged, and no header, command, schema or result changed.

Every caller was checked: `src/gui/MainWindow.cpp:620` and `:647` (`exec()` - still shown and modal),
`src/gui/ControlCommandsShell.cpp:145` (sets non-modal, then shows), and the tests that construct the dialog to
inspect or accept it (`tests/src/gui/UpdateCheckWindowsTest.cpp:284,288`,
`tests/src/gui/UpdateCheckHiddenTest.cpp:97`, `tests/src/gui/TrackAddRoutingTest.cpp:536`,
`tests/src/gui/AccessibilityWalkTest.cpp:233`; the accessibility walk calls `show()` itself, the others only need
the widget tree and are unchanged).

## Regression test

`tests/src/gui/MainWindowTest.cpp:168` `settingsOpensWithoutBecomingTheActiveModalWidget` drives the registered
`window.settings` command over the real shell:

1. the visible `SetupDialog` exists and `isModal()` is false;
2. `QApplication::activeModalWidget() != settings` - the old code fails here, which is the user-visible loss of
   main-window input;
3. asking `window.settings` again raises the same single dialog and it stays non-modal;
4. closing deletes it (`WA_DeleteOnClose`), no active modal widget remains, and project journalling is restored.

The test cleans up with a scope guard even if an assertion fails, so a failure cannot leak the dialog into later
slots. No timer, sleep or wall-clock wait is involved; the only event pumping is `processEvents()` plus an explicit
`sendPostedEvents(..., QEvent::DeferredDelete)` for the deterministic delete-on-close step.

| scenario | without fix | with fix |
| --- | --- | --- |
| `window.settings` opens the dialog | ACTIVE MODAL WIDGET (main window loses input) | non-modal, `activeModalWidget()` null; main window keeps input |
| ask again while open | raises the same dialog (but still modal) | raises the same dialog, still non-modal |
| close via `WA_DeleteOnClose` | stale modal state until deletion; first input swallowed | deleted, no active modal widget, journalling restored |
| MainWindow first-run / audio setup `exec()` | modal | unchanged: modal |
| tests constructing `SetupDialog` directly | shown as a side effect | hidden until the caller shows/execs it; assertions unchanged |
| audio thread | not touched | not touched (no allocation, lock, wait, I/O or log on an audio path) |
| save/load, undo, commands | unchanged | unchanged (no command added or modified) |

## Commands and results

LOG FOLDER = `~/.local/share/zene-bunny-fix/qwen/runs/gui-1116-session-free-exploration-fa4a5f2e785c.r1/logs`.
All commands were run from the worktree; `ctest` was run from `build/tests`.

| step | command (log) | EXIT |
| --- | --- | --- |
| build the test before the fix | `cmake --build build --target MainWindowTest -j12` (`build-2-red.log`) | 0 |
| **RED before the fix** | `ctest -R '^MainWindowTest$' --output-on-failure` (`test-2-red.log`, 1 test): `settingsOpensWithoutBecomingTheActiveModalWidget` fails at `activeModalWidget() != settings` | **8** |
| product-only patch | `git diff -- src/gui/modals/SetupDialog.cpp > LOGS/fix.patch` (`fix.patch`) | 0 |
| **4b revert** | `git apply -R LOGS/fix.patch` | 0 |
| build reverted product | `cmake --build build --target MainWindowTest -j12` (`build-9-reverse-red.log`) | 0 |
| **4b RED** | `ctest -R '^MainWindowTest$' --output-on-failure` (`test-9-reverse-red.log`, 1 test): only the new slot fails, at the modal assertion | **8** |
| 4b re-apply | `git apply LOGS/fix.patch` | 0 |
| **GREEN build** | `cmake --build build --target zene MainWindowTest UpdateCheckWindowsTest UpdateCheckHiddenTest TrackAddRoutingTest AccessibilityWalkTest -j12` (`build-10-green.log`) | 0 |
| **4b GREEN / affected family** | `ctest -R '^(MainWindowTest\|UpdateCheckWindowsTest\|UpdateCheckHiddenTest\|TrackAddRoutingTest\|AccessibilityWalkTest)$' --output-on-failure` (`test-10-green.log`, **5/5 passed**) | **0** |
| **gates** | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-2.log`, 8/8 PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) | **0** |

The first gate invocation (`gates-1.log`, EXIT=1) was stopped by the lane guard because the shipped script checks out
the base commit into its shared baseline tree. No gate limit or baseline was changed. The base listing was computed
from a pristine `git archive fa4a5f2e785c...` in the approved scratch area (`base-complexity.log`,
`base-file-length.log`, both EXIT=0, no regression lines), cached in the gate tool's documented
`gate-baselines/fa4a5f2e785c....json`, and the exact shipped script then passed (`gates-2.log`, EXIT=0).

## Files committed

- `src/gui/modals/SetupDialog.cpp` (constructor no longer shows; cause fix)
- `tests/src/gui/MainWindowTest.cpp` (regression slot)
- `tests/upstream-modifications.txt` (declared divergence for the inherited dialog)
- this report

No new file was added, so `tests/fork-sources.txt` needs no entry; `tests/all-sources.txt` already lists both source
files. No command was added, so the command snapshot canary is untouched.

## Self-check

- **Regression fails with the product change reverted and passes restored:** `git apply -R LOGS/fix.patch` + rebuild
  (`build-9-reverse-red.log`, EXIT=0) then `ctest` (`test-9-reverse-red.log`) = **EXIT=8**, only the new slot fails
  at `activeModalWidget() != settings`; `git apply LOGS/fix.patch` + rebuild (`build-10-green.log`, EXIT=0) then the
  family `ctest` (`test-10-green.log`) = **EXIT=0**, 5/5 passed.
- **Deterministic:** no timers, sleeps, threads or wall-clock. The test invokes the registered command synchronously,
  pumps events, closes the dialog, and flushes the deferred delete with `sendPostedEvents(..., DeferredDelete)`; it
  has no timing assertion.
- **At the cause and complete:** the cause is the constructor showing an application-modal dialog before the command
  can mark it non-modal. The fix is in that constructor. Callers checked:
  `src/gui/MainWindow.cpp:620,647` (`exec()`, unchanged modal behaviour),
  `src/gui/ControlCommandsShell.cpp:145-151` (now sets non-modal before the first show), and the four test files
  listed above. No header changed, so no extra include-dependent target needed rebuilding beyond `zene` and the five
  affected tests.
- **No collateral damage:** no project/save format, undo step, audio path or command changed. The first-run and
  audio-device `exec()` paths still show a modal Settings dialog, old projects load exactly as before, and the five
  directly affected tests pass (`test-10-green.log`). No limit or check was added.
- **Not ALREADY-FIXED / FALSE-POSITIVE:** the defect is present on this branch before the change; the RED run above
  shows `QApplication::activeModalWidget()` is the Settings dialog on the unmodified product. The fix is the
  constructor change, not a test-only claim.
- **Sibling commands/dialogs:** no command file was changed. `window.settings` keeps its schema and `shown` result;
  `app.about` and the other `window.*` handlers still set `setModal(false)` before their own `show()` and are
  unaffected. The only shared class changed is `SetupDialog`, whose two real entry points are covered above.
