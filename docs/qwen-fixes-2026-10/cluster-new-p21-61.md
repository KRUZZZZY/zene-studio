Verdict: FALSE-POSITIVE

new-p21-61: `window.sample_editor` cannot receive a null `SampleSelectionView` from `SampleEditorDialog::openFor`. The finding's trigger ("`openFor` returns a dialog whose `view()` is null (initialization failure)") has no code path on this branch, so no product change is made. A guard test pins the invariant.

## Evidence

- `src/gui/SampleEditorDialog.cpp:109` - the view is created in the constructor's member-initialiser list: `m_view(new SampleSelectionView(this))`. It is set before the constructor body runs and is never reassigned to null in this file.
- `include/SampleEditorDialog.h:158` - `SampleSelectionView* view() const { return m_view; }` returns the member unconditionally.
- `src/gui/SampleEditorDialog.cpp:142-156` - `openFor` returns one of two non-null pointers: an already-open dialog for the same clip found in `QApplication::topLevelWidgets()` (`:144-150`, `qobject_cast` then `open->show(); return open;`), or `new SampleEditorDialog(clip, parent)` (`:154`, `return dialog;` at `:156`). The only construction site in `src/` is `:154`.
- `src/gui/ControlCommandsSampleEditor.cpp:70-78` - the caller uses `dialog->view()` at `:75-76` and `:78`. The `dialog` pointer comes straight from `openFor` (`:70`), so the view it reads is the member at `SampleEditorDialog.cpp:109`.
- The other `openFor` caller, `src/gui/shell/ShellCommandMenus.cpp:330`, discards the return value, so nothing else depends on a null-view outcome.
- Standard C++ `new` throws `std::bad_alloc` on failure and does not return null, so an "initialization failure" that leaves `m_view` null does not exist in the constructor.

Result: the reported crash path is unreachable. No product code changed.

## Changes committed

- `tests/src/gui/SampleEditorDialogTest.cpp` - new test case `openForGivesEveryDialogAView` (after `editorOwnsOnlyItsTransportToolbar`). It opens a fresh dialog through `openFor`, asserts the dialog and `view()` are non-null, asserts `clip()` is the clip passed in, reopens the same clip and asserts the same dialog is returned with its view still non-null. Deterministic: no timing, sleeps or races; it uses the file's existing `toneClip` helper.
- `tests/upstream-modifications.txt` - one line registering the test file's change with its reason (required by the file-registration rule).
- `docs/qwen-fixes-2026-10/cluster-new-p21-61.md` - this report.

## RED / GREEN

No RED/GREEN regression pair applies: there is no defect to revert. The test was run on the unchanged product code and passes, which is the expected outcome for a FALSE-POSITIVE. It guards the invariant the finding assumed was broken; it is not a regression test for a bug. Step 4b (revert-then-reapply) was therefore not run; there is no product diff to revert (`git diff -- src include` is empty).

## Commands and exit codes

Build logs are in the run's `logs/` folder.

| # | Command | EXIT | Log |
|---|---------|------|-----|
| 1 | `cmake --build build --target zene SampleEditorDialogTest -j4` | 2 | `build-1.log` - failed before compiling: ccache could not create temp files in `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (outside the write-jail) |
| 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-p21-61 cmake --build build --target zene SampleEditorDialogTest -j4` | 0 | `build-2.log` (0 ccache errors; `Built target SampleEditorDialogTest`) |
| 3 | `ctest -R SampleEditorDialogTest --output-on-failure` (from `build/tests`) | 0 | `test-1.log` - 1/1 tests run, 100% passed |
| 4 | `./SampleEditorDialogTest openForGivesEveryDialogAView` (from `build/tests`) | 0 | `test-1-direct.log` - 3 passed, 0 failed (initTestCase, openForGivesEveryDialogAView, cleanupTestCase) |
| 5 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `feature-check-1.log` - the wrapper stopped at `logs.mkdir` with `PermissionError`: it writes to `<worktree>/../<name>-feature-check`, which is outside the write-jail. Its six simple gates were **not** run by it. |
| 6 | the six simple gates, run directly from the worktree (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`) | 0, 0, 0, 0, 0, 0 | `gate-<name>.log` |
| 7 | the two ratchet gates, run directly (`complexity-gate.sh --check`, `file-length-gate.sh --check`) | 1, 1 | `gate-complexity.log` (7 REGRESSION lines), `gate-file-length.log` (22 REGRESSION lines) |
| 8 | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-61` | 1 | `submit-check-2.log` (after the commit): commits PASS, working tree clean PASS, report PASS, tests added or changed PASS, **gates FAIL** - the only failing row. The gates row is `feature_check.py`'s exit 1, which is the `PermissionError` in row 5. First run (`submit-check-1.log`, before the commit): FAIL on commit, report, self-check, tests and gates. |

Gate reading (the wrapper's rule, applied by hand): the ratchet rows PASS when no REGRESSION item is in a file this branch changed. The changed files are `tests/src/gui/SampleEditorDialogTest.cpp` and `tests/upstream-modifications.txt`. Neither appears in either ratchet log (`grep -c 'SampleEditorDialogTest\|upstream-modifications' gate-complexity.log gate-file-length.log` = 0), and the feature-check gate baseline comparison was not run, because it needs a checkout inside `zene-cachyos` (outside the write-jail). That comparison cannot change the result here: no listed item names a changed file.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** Not applicable: there is no product change, so there is nothing to revert. The test passes on unchanged code (test 3: EXIT=0, test 4: EXIT=0). That is the expected result for a false-positive guard, and this report does not claim it as a RED/GREEN regression.
- **The test is deterministic: no wall-clock timing, sleeps or races.** Yes. It uses no timers, sleeps or event-loop waits. Its inputs are the file's existing `toneClip(m_track)` (fixed 1 s tone at 44.1 kHz), a fixed `Engine::init(true)` fixture, and direct pointer comparisons. The dialog is created without a parent and deleted directly, not via `close()`/`deleteLater()`.
- **The fix is at the cause and complete.** No fix was made. The invariant is established by the constructor initialiser (`SampleEditorDialog.cpp:109`), the sole constructor call site (`:154`), and both `openFor` return paths (`:144-156`); the callers (`ControlCommandsSampleEditor.cpp:70-78`, `ShellCommandMenus.cpp:330`) are consistent with it.
- **No collateral damage.** No product code, header, saved-data format, limit or command changed. The only other file touched is the test file and its upstream-modifications line. No new control command was added, so the ControlCommandsSnapshot canary does not move.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.** See Evidence: `src/gui/SampleEditorDialog.cpp:109`, `include/SampleEditorDialog.h:158`, `src/gui/SampleEditorDialog.cpp:142-156`, `src/gui/ControlCommandsSampleEditor.cpp:70-78`.

## Open items for the reviewer

- The feature-check wrapper cannot run from this write-jail (log folder outside it). The six simple gates and the two ratchets were run directly with the same commands; the wrapper's own table has not been produced.
- The test build needed `CCACHE_TEMPDIR` pointed at a directory under `~/.cache/zene-tmp`; the default ccache temp path is outside the jail. No build cache or product source outside the worktree was changed.
