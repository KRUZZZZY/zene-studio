Verdict: FALSE-POSITIVE

Cluster new-p21-174: `FollowActionEditor::rowOf` "dereferences a possibly null cell widget" (`src/gui/FollowActionEditor.cpp:228`, inside `rowOf` at `:221-232`; declared `include/FollowActionEditor.h:62`).

The reachable path exists, and a null cell is read at line 228 during a row removal. The read does not dereference anything: the call `cell->isAncestorOf(widget)` on a null `cell` returns normally on this build and the loop continues, so the stated harm (a null dereference causing a fault or a wrong row) does not occur. Strictly, a non-virtual member call through a null pointer is undefined behaviour in C++, and the guard the finding proposes would remove it with no change in behaviour. I did not apply that guard: both versions return the same row, so no test can ever fail for it, and an unprovable change is not a fix under this lane's rules. The owner may add it as hygiene.

Round 1 (`REWORK`) is addressed as follows. The disposition is now decided with a code path and runtime evidence (sections 1-2). The post-Add invariant test is removed, because it is not regression proof for `rowOf`. Section 3 corrects two round-1 claims and records the Remove re-entry crash as a separate candidate.

## 1. Reachable path

Code:
- `addRow` (`src/gui/FollowActionEditor.cpp:176-195`) sets all five cell widgets before installing the event filter (`:194`) and calling `nameRows` (`:195`). Complete rows always have five cells.
- `removeRow` (`:157`) removes whole rows, then calls `nameRows`.
- The FocusIn branch of `eventFilter` (`:201-204`) calls `rowOf` and then `setCurrentCell`.

Runtime (gdb, experiment A: focus on row 0's Type cell, then `table->removeRow(0)`; stopped at the first `rowOf` call that sees a null cell, `gdb-a-window2.log`, `gdb-a-window.log`):

```
QTableWidget::removeRow(0)
 QAbstractItemModel::beginRemoveRows
  QAbstractItemView::rowsAboutToBeRemoved
   QWidgetPrivate::hide_helper          (focused row-0 cell is hidden)
    QAbstractItemView::focusNextPrevChild
     QAbstractItemView::currentChanged -> edit -> focus moves -> FocusIn
      FollowActionEditor::eventFilter
       FollowActionEditor::rowOf(widget in row 1)   rowCount() == 3, row-0 cells already null
```

The window also opens without any focus set by the test. Experiment C (`removeRow(0)` after `show()`, `instr-zzExpC_removeRowNoFocus.log:8-14`): `show()` leaves focus on row 0's Type combo, and removing row 0 moves focus, so `rowOf` runs with row 0 cells null.

## 2. The null read does not dereference

- `gdb-a-window.log:16-22`: cells (0,0) through (0,4) are `(nil)`. The loop continues to row 1 and returns at (1,1). Five `cell->isAncestorOf(widget)` calls on a null pointer returned normally, with no fault.
- `instr-zzExpC_removeRowNoFocus.log:9-14`: the first in-window call reads (0,0) as null and returns at (0,1); the second reads (0,0) and (0,1) as null and returns at (0,2). Neither faults.

So the null read happens in a normal removal, and the call completes without touching memory, because `QWidget::isAncestorOf` only compares pointers on this Qt 6.12 build. The Qt docs describe it as an ancestry query and do not state the null case; the runtime observation above is the evidence here. The result of the null read is `false` and its effect is the same as the guarded version's.

## 3. Corrections to round 1, and the separate candidate

Corrections:
- Round 1 said that clicking Remove after focusing row 1's Linked checkbox crashes. It does not reach the click. The debug log has no `EXPB` marker, and the crash happens at `table->cellWidget(1, 2)->setFocus()` (`instr-zzExpB_removeButtonWithCellFocus.log`; EXIT 139).
- Round 1 said the fault was inside `QTableWidget::cellWidget`. That was only where the stack ran out, not a null read. The `rowOf` frames in that trace are stack exhaustion, not evidence about line 228.
- Round 1's "the guard does not stop the crash" is consistent with section 2: the guard removes the harmless null call, but the re-entry continues.

Separate candidate (not this finding; needs its own triage). The FocusIn branch of `eventFilter` calls `setCurrentCell` synchronously, and that re-enters the focus chain. Two triggers were reproduced on offscreen Qt 6.12:
- (a) Focus on row 1's Linked checkbox with no removal (experiment B). Focus ping-pongs between row 1's Type combo and Linked checkbox, 9690 `rowOf` calls, then SIGSEGV (`instr-zzExpB_removeButtonWithCellFocus.log`, EXIT 139). The loop is in focus handling, not in removal.
- (b) Removing the row that holds focus (experiment A, section 1). The re-entry runs through `hide_helper` and `currentChanged`. In the instrumented build it did not finish (it hung with debug output, and the process was stopped by hand). In the uninstrumented round-1 build it crashed with a stack overflow.

Neither trigger has been checked on a real display. Trigger (a) could matter for real clicks on that checkbox and should be triaged first.

## 4. Test

The post-Add invariant test (`FollowActionEditorTest::everyRowKeepsAllItsCellWidgets`, added in `7707d9b18`) is removed from the tree. It checks construction and Add, not `rowOf`, and it passes with or without the finding. No replacement test is committed, because no deterministic test can distinguish the guarded from the unguarded `rowOf`: both return the same row and differ only in undefined behaviour, which no test can observe.

## Self-check

- Regression test fails with the product change reversed and passes restored: not applicable. There is no product change, and the verdict is FALSE-POSITIVE, so no regression applies. The post-Add test is removed and is not presented as regression proof.
- Deterministic (no timing, sleeps or races): no test is committed.
- Fix at the cause and complete: not applicable, no fix. The caller-side question was checked in section 1: `nameRows` (`:208-219`) and `chain` (`:234-246`) run on complete rows only (`addRow` `:195`; `removeRow` `:157`).
- No collateral damage: no product code changed.
- FALSE-POSITIVE evidence: section 1 (`:157`, `:176-195`, `:201-204`, `:221-232`, gdb backtrace) and section 2 (null reads that return normally, `gdb-a-window.log:16-22`, `instr-zzExpC_removeRowNoFocus.log:9-13`). Undefined-behaviour caveat stated above.

## Evidence table

Run folder `runs/new-p21-174.r2`, logs in `logs/`. Commands run from the worktree unless noted.

| Step | Command | EXIT | Log |
|---|---|---|---|
| Instrumented build (temporary `fprintf` in `rowOf`) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target FollowActionEditorTest -j4` | not captured; ends "Built target FollowActionEditorTest" | `build-1-instr.log` |
| Experiment C, `removeRow` after `show()` | `timeout 60 ./FollowActionEditorTest zzExpC_removeRowNoFocus` (in `build/tests`) | 0 | `instr-zzExpC_removeRowNoFocus.log` |
| Experiment B, focus on row 1 Linked | `timeout 60 ./FollowActionEditorTest zzExpB_removeButtonWithCellFocus` | 139 | `instr-zzExpB_removeButtonWithCellFocus.log` |
| Experiment A, stopped at first null-cell `rowOf` | `timeout 180 gdb -batch -ex "break …rowOf" -ex run -ex "ignore 1 1" -ex run/continue -ex "bt 22"/"bt 80" --args ./FollowActionEditorTest zzExpA_removeRowWithCellFocus` | gdb 0 (inferior stopped at the breakpoint) | `gdb-a-window.log`, `gdb-a-window2.log` |
| Experiment A, uninstrumented run (round 1, `r1`) | `./FollowActionEditorTest zzExpA_removeRowWithCellFocus` | 139 (round-1 log) | `exp-a-1.log` (r1) |
| Experiment A, instrumented, no timeout | `./FollowActionEditorTest zzExpA_removeRowWithCellFocus` | none: hung in sleep, no output for 10+ minutes; stopped by hand (`kill`) | `instr-zzExpA_removeRowWithCellFocus.log` |
| Revert instrumentation | `git checkout -- src/gui/FollowActionEditor.cpp tests/src/gui/FollowActionEditorTest.cpp` | 0 (status ran after it) | none |
| Remove post-Add test | `git checkout c04953b60 -- tests/src/gui/FollowActionEditorTest.cpp` | 0 | none |
| Rebuild on restored tree | `CCACHE_TEMPDIR=… cmake --build build --target zene FollowActionEditorTest -j4` | 0 | `build-2-restored.log` |
| Family test | `cd build/tests && ctest -R '^FollowActionEditorTest$' --output-on-failure` | 0, "100% tests passed out of 1" | `test-1-restored.log` |
| Gates, redirected copy | `python3 -I $TMPDIR/gatecopy/feature_check.py <worktree>` (only log folder and ROOT redirected) | 0, RESULT: PASS (8/8 PASS, 2 changed files: report and test-file revert, staged) | `gates-1.log` |

Affected-family test count: 1 test executed in `FollowActionEditorTest`.

Deviations, disclosed:
- `feature_check.py` and `submit_check.py` cannot create their log folder beside the worktree (outside the write area). The gates were run from a scratch copy with the log folder and `ROOT` redirected. The checks are unchanged.
- ccache's configured temp dir is outside the write area. Builds used `CCACHE_TEMPDIR`; the config is unchanged.
- Experiment A's instrumented run hung and was stopped with `kill` on its own PID. The debug instrumentation and experiment slots were reverted before the final build.
