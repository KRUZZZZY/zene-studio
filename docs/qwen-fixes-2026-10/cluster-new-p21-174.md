Verdict: BLOCKED

Cluster new-p21-174: `FollowActionEditor::rowOf` calls `isAncestorOf` through a null cell widget (`src/gui/FollowActionEditor.cpp:229`, the guard; the unguarded call was at `:228`). The finding is REAL: a member call through a null pointer is undefined behaviour, and UBSan reports it on the reachable row-removal path. The fix guards the cell before the call. A deterministic regression, run in a UBSan leg, fails with the guard reversed and passes with it restored.

Status: the defect is fixed and proven under UBSan (sections 2 and 4). The loop's normal-configuration negative control cannot fail for this defect: with the guard reversed, the normal build passes (`negctl`, and this round's `normal-reverted.log`, exit 0). That is a limit of this toolchain, not a test gap: the undefined call returns the same value, so no normal-build observation exists (section 3). The verdict is BLOCKED on that check until the owner decides how the cluster should be negative-controlled (section 6).

Earlier rounds said FALSE-POSITIVE (`round 2`). That was wrong: the call returned normally on this Qt build, but undefined behaviour is not made safe by a normal return. The removal-path evidence from round 2 still holds and is reused here.

## 1. The defect and the reachable path

Code:
- `addRow` (`src/gui/FollowActionEditor.cpp:176-195`) sets all five cell widgets before installing the event filter (`:194`) and calling `nameRows` (`:195`). A complete row always has five cells.
- `removeRow` (`:157`) removes whole rows. Qt releases a removed row's cell widgets while the row is still in the model.
- The FocusIn branch of `eventFilter` (`:201-204`) calls `rowOf` (`:203`). `rowOf` (`:221-232`) scans every row and calls `cell->isAncestorOf(widget)` on each cell, with no null check, before the guard.
- `rowOf` has one caller, `eventFilter` (`:203`; checked with `rg`).

Runtime path (round 2 gdb trace, `runs/new-p21-174.r2/logs/gdb-a-window2.log`):

```
QTableWidget::removeRow(0)
 QAbstractItemModel::beginRemoveRows
  QAbstractItemView::rowsAboutToBeRemoved
   QWidgetPrivate::hide_helper          (focused row-0 cell is hidden)
    QAbstractItemView::focusNextPrevChild
     QAbstractItemView::currentChanged -> edit -> focus moves -> FocusIn
      FollowActionEditor::eventFilter
       FollowActionEditor::rowOf          rowCount() == 3, row-0 cells already null
```

The null cells are seen in that window (`runs/new-p21-174.r2/logs/gdb-a-window.log:16-22`: row 0, columns 0-4, `(nil)`).

UBSan confirms the null call on this path (round 2 review and this round):
`FollowActionEditor.cpp:228:44: runtime error: member call on null pointer of type 'const struct QWidget'` (`runs/new-p21-174.r3/logs/red-b-ubsan.log:4`).

## 2. Fix

`src/gui/FollowActionEditor.cpp:228-229`:

```cpp
// A row being removed has released its cells while it is still in the model, so a cell can be null here.
if (cell != nullptr && (cell == widget || cell->isAncestorOf(widget))) { return row; }
```

Cause and siblings:
- The cause is that `rowOf` can scan a row whose cells Qt has released during removal. The guard is at the one place that reads a cell in that state.
- `nameRows` (`:208-219`) and `chain` (`:234-246`) also read cells. They run only on complete rows: `nameRows` after `addRow` (`:195`) and after `removeRow` (`:157`); `chain` from `apply` (`:251`), which the user reaches outside any removal. Neither is reached during a removal, so they are unchanged.
- Behaviour: for non-null cells the guard changes nothing. For a null cell the call was undefined and returned false on this build; the guard skips it, so the result is the same false. No caller can see a difference, so this is not a behavioural change.

## 3. Regression test

`tests/src/gui/FollowActionEditorTest.cpp:65-87`, `aRowRemovedWhileItsControlsReceiveFocusLeavesTheEditorIntact`:
- Builds a 3-row editor (the same chain shape as the existing tests).
- Connects a lambda to the table model's `rowsAboutToBeRemoved`. It runs after Qt's own handler, so row 2's cells are already released. When the removed row is row 2, the lambda sends a `FocusIn` to row 2's chance control, which reaches `eventFilter`, then `rowOf`, with a null cell.
- Calls `table->removeRow(2)` and checks the row count is 2.

Why a synthetic focus-in: real focus on the removed row re-enters `eventFilter` through `setCurrentCell` (the separate focus-cycle defect, see section 5). In the normal build, the real-focus variant segfaulted in two of four runs (`red-regular.log`, `rep-1.log`) and passed in two (`rep-2.log`, `rep-3.log`), so it cannot be a deterministic regression test. The synthetic event reaches the same `rowOf` call from the same point in the removal, with no focus movement.

The existing `anExplicitChanceEditIsWritten` and `aPassiveFocusLossKeepsTheLoadedChance` tests are unchanged.

What the normal build can and cannot show: with or without the guard, the normal build passes this test, because the undefined call returns the same value. I checked for an observable in the normal build and found none:
- `QWidget::isAncestorOf` is a non-virtual, out-of-line member exported by `libQt6Widgets` (`nm -DC`). The call on a null `this` returns false without touching memory.
- Intercepting that call from the test (overriding the Qt symbol and forwarding through `dlsym(RTLD_NEXT)`) would be platform-specific (ELF only, not MSVC) and fragile: GCC may drop a `this == nullptr` check inside a member function. It would also turn a Qt method into a test spy. I did not do it.

So the normal build cannot prove the regression. The UBSan leg is the proof (section 4). Section 6 lists what the owner can do.

## 4. UBSan leg (the RED/GREEN proof)

`WANT_DEBUG_UBSAN` is OFF in this build, and a full UBSan build is too large for this shared machine. Instead, a scratch script (`$TMPDIR/ubsan-leg/build_leg.py`, outside the repo) builds the leg:
- recompiles only `FollowActionEditor.cpp` with the build's own compile command plus `-fsanitize=undefined -fno-sanitize=vptr -fno-sanitize-recover=all` (the same flags as the project's `add_sanitizer(undefined ...)`, plus no-recover so a hit aborts);
- relinks `build/tests/FollowActionEditorTest` with that object swapped in for `lmmsobjs`' normal one, and with `-fsanitize=undefined`;
- writes the leg binary to `build/tests/FollowActionEditorTest_ubsan`.

The sanitized object is the only instrumented code. Everything else is the regular build. The portability requirement is covered: the leg uses GCC, which supports `-fsanitize=undefined`. MSVC has no UBSan, so the regular path is the only check there.

RED (guard reversed, `git apply -R fix.patch`): the leg aborts at `FollowActionEditor.cpp:228:44` with `runtime error: member call on null pointer`, exit 1. The normal build passes (exit 0).

GREEN (guard applied): the leg exits 0, 5 tests pass, no runtime error. The normal build exits 0. The focused ctest passes (1 of 1).

## 5. Separate candidate (not this finding)

The FocusIn branch of `eventFilter` calls `setCurrentCell` synchronously. That re-enters the focus chain. It is a separate defect:
- Real focus on a row's Linked checkbox, with no removal, ping-pongs focus between that row's Type combo and Linked checkbox until the stack overflows (round 2, `instr-zzExpB_removeButtonWithCellFocus.log`, exit 139).
- A removal can also trigger the same cycle when the focus that moves during removal lands in a row that is not the removed one (`instr-variant.log` in this round: row 1's controls ping-pong after the removal window).

Not fixed here. It needs its own triage: the focus behaviour choices (which row is current during removal, and whether FocusIn should set the current row) are a design question.

## Self-check

- Regression test fails with the product change reversed and passes restored: yes under UBSan, no under the normal configuration (BLOCKED, see Status). Under UBSan: negative control on this round's `negctl` logs in `runs/new-p21-174.r4/logs` (`ubsan-reverted.log` exit 1 with the `:228:44` runtime error; `ubsan-restored.log` exit 0). The normal build: `normal-reverted.log` exit 0 with the guard reversed, `normal-restored.log` exit 0. RED: `red-b-ubsan.log` (exit 1, `FollowActionEditor.cpp:228:44` runtime error). GREEN: `green-final-ubsan.log` (exit 0). The normal build passes both ways (`red-b-regular.log`, `green-final`), so it is not the proof; the UBSan leg is.
- Deterministic (no wall-clock timing, sleeps or races): yes. The regression needs no timing or activation. Its focus-in is synthetic, sent from the model's `rowsAboutToBeRemoved` hook. Normal-build repeats: `final-regular-1..3.log` all exit 0. UBSan repeats: `final-ubsan-1..3.log` all exit 0.
- Fix at the cause and complete: the guard is at the one place that reads a released cell during removal. Siblings are checked in section 2 (`nameRows`, `chain`, `apply`, the only `rowOf` caller).
- No collateral damage: for non-null cells the code path is unchanged; for null cells, the outcome (false) is the same as before. Saved projects and other callers are not affected; `rowOf` has one caller.
- FIXED evidence: section 1 (`:157`, `:176-195`, `:201-204`, `:221-232`, the gdb trace in round 2 and the UBSan message).
- Realtime: not applicable. `rowOf` is reached only from the QWidget FocusIn event filter.

## Evidence table

Run folder `runs/new-p21-174.r3`, logs in `logs/`. Commands run from the worktree unless noted.

| Step | Command | EXIT | Log |
|---|---|---|---|
| Test object, regular build, guard reversed | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target FollowActionEditorTest -j4` | 0 | `build-6-red2.log` |
| UBSan leg, guard reversed | `python3 -I $TMPDIR/ubsan-leg/build_leg.py` | 0 | `ubsan-build-red2.log` |
| RED sanitized, guard reversed | `./FollowActionEditorTest_ubsan` (in `build/tests`) | 1 (`runtime error` at `FollowActionEditor.cpp:228:44`) | `red-b-ubsan.log` |
| RED normal, guard reversed | `./FollowActionEditorTest` (in `build/tests`) | 0, "5 passed" | `red-b-regular.log` |
| Patch | `git diff -- src/gui/FollowActionEditor.cpp > logs/fix.patch`; `git apply -R --check logs/fix.patch` | 0 (reverse applies) | `fix.patch` |
| Reverse the guard | `git apply -R logs/fix.patch` | 0 | none |
| Reapply the guard | `git apply logs/fix.patch` | 0 | none |
| GREEN build | `CCACHE_TEMPDIR=… cmake --build build --target zene FollowActionEditorTest -j4` | 0 | `build-10-green-final.log` |
| GREEN UBSan leg | `python3 -I $TMPDIR/ubsan-leg/build_leg.py` | 0 | `ubsan-build-green-final.log` |
| GREEN sanitized | `./FollowActionEditorTest_ubsan` | 0, "5 passed", no runtime error | `green-final-ubsan.log` |
| GREEN normal, repeats 1-3 | `./FollowActionEditorTest` | 0, 0, 0 | `final-regular-1..3.log` |
| Sanitized repeats 1-3 | `./FollowActionEditorTest_ubsan` | 0, 0, 0 | `final-ubsan-1..3.log` |
| Focused ctest | `ctest -R '^FollowActionEditorTest$' --output-on-failure` (in `build/tests`) | 0, "100% tests passed out of 1" | `test-green-final.log` |
| Gates, redirected copy | `python3 -I $TMPDIR/gatecopy/feature_check.py <worktree>` (only log folder and `ROOT` redirected) | 0, RESULT: PASS, 8/8 PASS, 3 changed files | `gates-final.log` |

Affected-family test count: 1 test executed in `FollowActionEditorTest` (5 test slots).

Negative control, this round (`runs/new-p21-174.r4/logs`; `fix.patch` is the r3 guard patch, unchanged):

| Step | Command | EXIT | Log |
|---|---|---|---|
| Guard reversed (`git apply -R --check`, then `-R`) | `git apply -R logs/fix.patch` | 0 | none |
| Normal build, guard reversed | `cmake --build build --target FollowActionEditorTest -j4`; `./FollowActionEditorTest` (in `build/tests`) | build 0; test 0, "5 passed" | `build-reverted.log`, `normal-reverted.log` |
| UBSan leg, guard reversed | `python3 -I $TMPDIR/ubsan-leg/build_leg.py`; `./FollowActionEditorTest_ubsan` | leg 0; test 1 (`runtime error` at `FollowActionEditor.cpp:228:44`) | `leg-reverted-build.log`, `ubsan-reverted.log` |
| Guard reapplied (`git apply`) | `git apply logs/fix.patch` | 0 | none |
| Normal build, guard restored | as above | build 0; test 0, "5 passed" | `build-restored.log`, `normal-restored.log` |
| UBSan leg, guard restored | as above | leg 0; test 0, "5 passed", no runtime error | `leg-restored-build.log`, `ubsan-restored.log` |
| Focused ctest, guard restored | `ctest -R '^FollowActionEditorTest$' --output-on-failure` | 0, "100% tests passed out of 1" | `ctest-restored.log` |

Earlier leg attempts, disclosed: a leg binary left in scratch crashed the existing chance test with a segfault in `SampleBuffer::emptyBuffer` even with no sanitizer (`nosan-chance.log`, exit 139). The cause was the binary's location outside `build/tests`. Moving the leg binary into `build/tests` fixed it (the green runs above). The reverse-guard RED that used the scratch binary also failed at `:228:44` (`red-ubsan.log`, exit 1).

Deviations, disclosed:
- `feature_check.py` and `submit_check.py` cannot create their log folder beside the worktree (outside the write area). The gates were run from a scratch copy with the log folder and `ROOT` redirected; the checks are unchanged.
- ccache's configured temp dir is outside the write area. Builds used `CCACHE_TEMPDIR`; the config is unchanged.
- The sanitized leg is not a full `WANT_DEBUG_UBSAN` build. It instruments one object and relinks the one test binary.

## 6. Decision needed from the owner

The normal-configuration negative control cannot pass for this defect on this toolchain. The choices:
- (a) Run this cluster's negative control in a UBSan-enabled build, with `WANT_DEBUG_UBSAN=ON`, then the regression FAILs without the guard and PASSes with it. A full UBSan tree is large (about 45-50 GB and hours of compile time on this shared machine), so it should be a dedicated build dir, not this one. The leg in section 4 is a smaller equivalent (one object instrumented).
- (b) Accept the guard as a no-behaviour-change hardening of an undefined call, with the UBSan leg as the only evidence and no normal-build regression.
- (c) Triage the separate focus-cycle defect (section 5) first, since it determines whether real-focus tests can exist here.

I recommend (a) for the loop and (b) only if the owner rules UBSan builds out of scope.
