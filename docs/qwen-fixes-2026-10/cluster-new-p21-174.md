Verdict: NEEDS-CONTEXT

Cluster new-p21-174, `FollowActionEditor::rowOf` null cell widget (`include/FollowActionEditor.h:62`, `src/gui/FollowActionEditor.cpp:221-232`).

The finding's stated defect, a null `cell` reaching `cell->isAncestorOf` at line 228, is not reachable in any complete row. But the editor has a real, reproducible crash on the Remove path, reached through `rowOf` during re-entrant focus handling, and the proposed guard does not stop it. Fixing that crash needs a decision about what focus and the current row should do while a row is being removed, so no product code is changed in this lane.

## 1. Why the null cell is not reachable in a complete row (static)

- `addRow` (`src/gui/FollowActionEditor.cpp:176-195`) inserts the row, sets all five cell widgets (188-192), installs the event filter on them (194) and only then calls `nameRows` (195). Nothing else creates rows.
- `removeRow` removes whole rows (`:157`). No code removes a single cell widget.
- The event filter is installed only on those cell widgets (`:194`). So `watched` is always a cell of a complete row, and `rowOf` returns at that row (`:223-228`) before it can reach a later row.
- `nameRows` (`:208-219`), `chain` (`:234-246`) and `apply` (`:248-253`) run on complete rows only: `nameRows` after `addRow` (`:195`) and after `removeRow` (`:157`).

So line 228 is only reachable with a null cell while a row is partly built or partly torn down, and no reachable caller hits that window except through the removal path below.

## 2. The reachable window: `removeRow` re-enters `eventFilter` (dynamic)

Reproduction, run on the unmodified tree from `build/tests`, offscreen, Qt 6.12.0. These were throwaway test slots added to `tests/src/gui/FollowActionEditorTest.cpp` and reverted; nothing is committed for them.

- EXP-A: a 3-row editor, focus the row-0 Type cell, call `table->removeRow(0)`. Result: SIGSEGV, EXIT=139 (`exp-a-1.log`).
- EXP-B: a 3-row editor, focus the row-1 Linked cell, then `QTest::mouseClick` on the Remove button. Result: SIGSEGV, EXIT=139 (`exp-b-1.log`).

gdb backtrace of EXP-A (`gdb-a-1.log`): the fault is inside `QTableWidget::cellWidget` (frames #0-#4), called from `FollowActionEditor::rowOf` (#5) and `eventFilter` (#6). Above that, the chain repeats: `eventFilter` (#18) → `QTableWidget::setCurrentCell` (#17) → `QAbstractItemView::setCurrentIndex` → `currentChanged` → `QAbstractItemView::edit` (#12) → focus change → `eventFilter` again. The fault address is in the stack range, so this is unbounded re-entry (stack exhaustion), not a single bad read.

## 3. The proposed guard does not fix it

Guard applied temporarily to line 228, as the finding suggests (`cell != nullptr && (cell == widget || cell->isAncestorOf(widget))`):

- EXP-A still SIGSEGV under gdb (`gdb-a-guard.log`), now in the same re-entrant chain (`QItemSelectionModel::emitSelectionChanged` ← `setCurrentCell` ← `eventFilter`).
- EXP-B still EXIT=139 (`exp-b-guard-plain.log`).
- At no point was a null `cell` at line 228 observed.

The guard was reverted (see commands below). The tree matches HEAD.

## 4. What is still open (needs a decision)

The Remove crash is the real defect here, and it is distinct from the one in the finding. The eventFilter's `FocusIn` handler (`:201-204`) calls `setCurrentCell`, which changes the current index, which opens an editor and moves focus again, which re-enters the handler. Options, none implemented:

- (a) Ignore `FocusIn` while `removeRow` is running (a member flag set around the call at `:157`). Smallest change; it alters when rows become current during removal.
- (b) In `eventFilter`, skip `setCurrentCell` when that cell is already current. Needs checking that it stops the cycle seen in section 2.
- (c) Capture the row to remove before `removeRow`, then apply the current-row change after the removal. Changes what "Remove takes the row being edited" means when focus is in another row.

Needs owner sign-off on which contract applies. I recommend (a) because it does not change the user-visible current-row rule, but I have not run it.

Caveats: the runs are on the offscreen platform, not a real display. The re-entry is in Qt's model/view code, so it is probably platform independent, but I have not checked that on a real display.

## Files

- Product code: unchanged (HEAD `639e1d23e`; the temporary guard, the mutation and the experiment slots were reverted).
- Committed: this report, and one passing test, `FollowActionEditorTest::everyRowKeepsAllItsCellWidgets` (`tests/src/gui/FollowActionEditorTest.cpp`, a product-new file already listed in `tests/fork-sources.txt`, so no upstream-modifications entry). It pins the invariant the verdict depends on. It is a guard, not a regression proof: it passes on the unchanged code.
- A RED test for the Remove crash is not committed: the crash kills the process, which a test cannot absorb, and committing it would fail the suite until the design is decided.
- Step 4b (revert-and-prove): not applicable, no product fix.

## Evidence table

| Step | Command (cwd `zene-qwen-new-p21-174`, logs in `runs/new-p21-174.r1/logs`) | EXIT | Log |
|---|---|---|---|
| Build, first try | `cmake --build build --target zene FollowActionEditorTest -j4` | 2 (ccache could not write its temp dir, a path outside the write area) | `build-1.log` |
| Build, ccache temp redirected | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target zene FollowActionEditorTest -j4` | 0 | `build-2.log` |
| Baseline family test on original code | `cd build/tests && ctest -R '^FollowActionEditorTest$' --output-on-failure` | 0, "100% tests passed out of 1" | `test-1.log` |
| Experiment build | `CCACHE_TEMPDIR=… cmake --build build --target FollowActionEditorTest -j4` | 0 | `build-3-experiment.log` |
| EXP-A on original code | `./FollowActionEditorTest zzExperimentRemoveRowWithFocus` | 139 | `exp-a-1.log` |
| EXP-B on original code | `./FollowActionEditorTest zzExperimentRemoveButtonWithCellFocus` | 139 | `exp-b-1.log` |
| EXP-A backtrace, original | `gdb -batch -ex run -ex "bt 40" --args ./FollowActionEditorTest zzExperimentRemoveRowWithFocus` | gdb ran; inferior SIGSEGV | `gdb-a-1.log` |
| Guard build | `CCACHE_TEMPDIR=… cmake --build build --target FollowActionEditorTest zene -j4` | 0 | `build-4-guard-experiment.log` |
| EXP-A with guard | `gdb -batch … zzExperimentRemoveRowWithFocus` | inferior SIGSEGV | `gdb-a-guard.log` |
| EXP-B with guard | `./FollowActionEditorTest zzExperimentRemoveButtonWithCellFocus` | 139 | `exp-b-guard-plain.log` |
| Invariant test, direct run | `./FollowActionEditorTest everyRowKeepsAllItsCellWidgets` | 0, "Totals: 3 passed, 0 failed" (3 = init, the test, cleanup) | `inv-direct.log` |
| Invariant test, family | `cd build/tests && ctest -R '^FollowActionEditorTest$' --output-on-failure` | 0, "100% tests passed out of 1" | `test-3-invariant.log` |
| Mutant: `addRow` without the Jump `setCellWidget` (commented out), run | `./FollowActionEditorTest everyRowKeepsAllItsCellWidgets` | 139 (SIGSEGV; the test does not reach a clean assertion) | `inv-mutant.log` |
| Mutant reverted | `git checkout -- src/gui/FollowActionEditor.cpp` (status then shows only the test file) | 0 | none |
| Final family test | `cd build/tests && ctest -R '^FollowActionEditorTest$' --output-on-failure` | 0, "100% tests passed out of 1" | `test-4-final.log` |
| Revert experiment edits | `git checkout -- src/gui/FollowActionEditor.cpp tests/src/gui/FollowActionEditorTest.cpp && git status --short` (status printed nothing) | 0 (status ran, so checkout succeeded) | none |
| Rebuild after revert | `CCACHE_TEMPDIR=… cmake --build build --target zene FollowActionEditorTest -j4` | 0 | `build-5-restored.log` |
| Family test after revert | `cd build/tests && ctest -R '^FollowActionEditorTest$' --output-on-failure` | 0, "100% tests passed out of 1" | `test-2-restored.log` |
| Gates, as shipped | `python3 …/feature_check.py <worktree>` | 1 (cannot create `zene-qwen-new-p21-174-feature-check` beside the worktree, outside the write area) | `gates-1.log` |
| Gates, copy with log folder and ROOT redirected | `python3 -I $TMPDIR/gatecopy/feature_check.py <worktree>` | 0, RESULT: PASS | `gates-3.log` |

Affected-family test count: 1 test executed in `FollowActionEditorTest` (both runs), not zero.

## Self-check

- Regression test fails with the product change reverted and passes with it: not applicable, there is no product change. The committed test passes on the unchanged code. Its sensitivity was checked with a mutant (`inv-mutant.log`, exit 139), which is a crash, not a clean assertion. The Remove crash has no passing test; see section 4.
- Deterministic: yes. No timing, sleeps or races. It drives the Add button with `QTest::mouseClick` and checks cell widgets with `QVERIFY`. Row counts are exact (`QCOMPARE` on 2, then 3).
- Fix at the cause and complete: not applicable, no fix. The verdict is backed by the sibling check in section 1: `nameRows` (`:215`), `chain` (`:239-243`) also dereference cell widgets unguarded, and `apply` reaches them through `chain()` (`:251`), and they are safe only because of the same invariant. A guard at `:228` alone would leave the rest on that invariant too.
- No collateral damage: no product code changed, so no saved data or caller is affected.
- Verdict evidence: section 1 (`addRow` `:176-195`, `removeRow` `:157`, filter `:194`) and section 2 (EXP-A and EXP-B, exit 139, gdb logs). The "not reachable" claim is about complete rows only; the removal window is reachable and crashes.

Gates, last run (`gates-4-final.log`, base `639e1d23e`, 2 changed files, both in the commit): RESULT: PASS, exit 0, all eight gates PASS.

Earlier gate run (`gates-3.log`, before the test was added, 0 changed files), same eight gates, each PASS: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in changed files), file-length PASS (no new item in changed files).

Deviations, disclosed:
- `feature_check.py` writes its log folder to `<worktree>-feature-check`, a sibling of the worktree, which the write area does not allow. I ran a copy from `$TMPDIR/gatecopy/` with only two lines changed: the log folder now goes to the run folder's `logs/feature-check-1`, and `ROOT` points at the original qwen folder so it reads the existing `FIX-BASE` and `gate-baselines` without writing to them. The checks themselves are unchanged.
- The ccache config (`~/.cache/ccache-bunny/ccache.conf`) sets a temp dir outside the write area. I overrode it per build with `CCACHE_TEMPDIR`; the config is unchanged.
