FIXED

# Cluster gui-1106-main-edit — "Edit footer switch does not activate the Edit view"

## Verdict and root cause

REAL as a user-visible failure, but **not** in `window.set_view`. The finding's hint pointed at
`src/gui/ControlCommandsWindow.cpp:173-187`, and that handler is correct: it calls `Shell::setView`
(`src/gui/shell/Shell.cpp:391-395`) synchronously, and the footer word dispatches it
(`src/gui/shell/Shell.cpp:252-255`). The click never reached the button. It landed off the screen.

Evidence, from the sweep run `~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-26`:

- The screen and screenshots are 1600 x 1000 (`shots/0084-before.png`). The inventory, taken while the window still
  fit, had the footer's Edit switch at x=1531 (`inv-main.jsonl`). At click time (`results.jsonl` line 85) the sweep
  re-measured it at **x=1729**, which is past the right edge of the screen. Arrange (1635), Mix (1689) and Browser
  (1778) were also past the edge, and all four were judged FAIL/UNSURE (`verdicts.jsonl`). The Arrange verdict
  says "the Arrange switch is outside the visible screenshot".
- The shots show when the window outgrew the screen: `0000`–`0074` fit (TASKS ARRANGE MIX EDIT and the Browser are
  visible). `0075` is the click that opened the **Mixer** in the bottom region, and from `0076` the window's right part
  is off the screen. Shot `0096` (the Mixer tab view) goes wide again.
- Measured in `MixerModesTest` with the Mixer open (probe, `logs/04-test-probe.log` run, direct output): main
  window `minimumSizeHint` = **2000** px, `MixerView` = 1576, `MixerModeBar` = **1576** (min = hint), the
  mixer's zone splitter = 763, the footer = 286. The mode bar's full row of buttons and chips
  (`src/gui/MixerModeBar.cpp:98-149`) was laid straight into the view's outer layout, so its width became the
  panel's minimum. With the Inspector and Browser regions beside it, the main window's minimum (2000 px) was larger
  than the screen. The window therefore grew past the screen's edge and took the footer's right-hand switches with it.

## Fix

`src/gui/MixerView.cpp`: the mode bar is now held by `ModeBarScroller`, a file-local `QScrollArea` (frameless,
widget-resizable, vertical bar off, horizontal bar only when needed, fixed height = bar height, plus the scroll
bar's height only while the row does not fit). The bar is still the same widget with the same controls, signals,
object names and `MixerView::modeBar()` accessor. At normal widths nothing changes visually. When the panel is
narrower than the row, the row scrolls sideways instead of growing the window. The height is re-fitted on
resize, and on the bar's `LayoutRequest` (a chip's text can change the row's width).

Considered and rejected: capping minimum widths in the shell's region hosts would clip every panel's content
(other panels' controls would become unreachable). Re-laying the bar as a wrapping flow layout would be a far
larger change. The Mixer is the only panel the sweep showed widening the window. With it fixed, the window's
minimum is <= 1600 (the test asserts it).

Siblings: `MixerView` is the only place that mounts `MixerModeBar`
(`rg -n "new MixerModeBar" src` → `src/gui/MixerView.cpp`). No code reaches the bar through its parent or a
layout index (`rg "modeBar\(\)->parent|m_modeBar->parent"` → none in src). The other queued footer clusters
from the same sweep (`gui-1106-main-mix`, `gui-1106-main-browser`, and Arrange as UNSURE) have this same cause.

## Regression test

`tests/src/gui/MixerModesTest.cpp::anOpenMixerKeepsTheMainWindowOnA1600PixelScreen` opens the Mixer panel and
asserts that the main window's `minimumSizeHint().width() <= 1600`. It also asserts that the bar sits in a
`QScrollArea` and that, when narrowed to 400 px, the bar keeps its full row (the scroll bar has a range and Routing
Matrix is still in the bar). The test is deterministic: no sleeps and no timing.

## Commands and results (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-edit.r1/logs/`)

ccache's temp dir is outside the write-jail, so every build used `CCACHE_DISABLE=1`.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target MixerModesTest -j4 > 05-build-red.log` | 0 | |
| RED test | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > 06-test-red.log` | 8 | `FAIL! ... 'window->minimumSizeHint().width() <= 1600' returned FALSE. (main window minimum width 2000)` |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > 07-build-green.log` | 0 | |
| GREEN test | `ctest -R '^MixerModesTest$' --output-on-failure > 08-test-green.log` | 0 | 100% tests passed out of 1 |
| Family build | `cmake --build build --target <23 targets of ctest -N -R 'Mixer\|^ShellTest$'> -j4 > 11-build-family.log` | 0 | |
| Family test | `ctest -R 'Mixer\|^ShellTest$' -j4 --output-on-failure > 12-test-family.log` | 0 | 100% tests passed out of 23 |
| 4b revert | `git diff -- src/gui/MixerView.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 | |
| 4b rebuild + test | `13-build-reverted.log` / `ctest -R '^MixerModesTest$' > 14-test-reverted.log` | 0 / 8 | same FAIL, minimum width 2000 |
| 4b reapply | `git apply logs/fix.patch` | 0 | |
| 4b rebuild + test | `15-build-reapplied.log` / `ctest -R '^MixerModesTest$' > 16-test-reapplied.log` | 0 / 0 | 100% tests passed out of 1 |
| Gates | `python3 <run>/scratch/feature_check_jail.py <worktree> > 18-gates.log` | 0 | all 8 PASS, RESULT: PASS |

Gate note: the stock `feature_check.py` exited 1 with `PermissionError` (`17-gates.log`), because it creates
`<worktree>-feature-check` next to the worktree, outside the write-jail. The jail copy is byte-identical except for two
lines: the logs path (moved into the run's log folder) and `ROOT` (pinned to the original script's directory, so it
uses the same FIX-BASE and gate baselines).

Files committed: `src/gui/MixerView.cpp`, `tests/src/gui/MixerModesTest.cpp`, `tests/upstream-modifications.txt`,
`docs/qwen-fixes-2026-10/cluster-gui-1106-main-edit.md`.

## Integration onto 050/m0-train-26 (round 2)

The accepted patch (`bugs/qwen-gui-1106-main-edit` @ `c1965faa4f`) was applied to the train snapshot
`af010bc3cc` by the driver. The three-way merge attempt reported a conflict in `src/gui/MixerView.cpp` and fell
back to direct application, which applied cleanly (`integration-apply.log`: "Applied patch to
'src/gui/MixerView.cpp' cleanly"). The integrated tree is **byte-identical** to the accepted patch:
`git diff --cached c1965faa4f -- src/gui/MixerView.cpp tests/src/gui/MixerModesTest.cpp
tests/upstream-modifications.txt docs/qwen-fixes-2026-10/cluster-gui-1106-main-edit.md` is empty, and the index
blobs match the accepted commit's blobs exactly (MixerView.cpp `544130dab5`, MixerModesTest.cpp `1cd1e0ca52`,
upstream-modifications.txt `d71f201088`, report `d9b6ecc255`). The target's `MixerView.cpp` differs from the
accepted branch only by this fix (`git diff c1965faa4f af010bc3cc -- src/gui/MixerView.cpp` shows only the fix's
inverse), so no newer target-side line was lost and no new design was needed.

### Affected failure paths

- **Normal path** (Mixer open, panel at least as wide as the 1576-px row): the horizontal scroll bar is hidden and
  the scroller's fixed height equals the bar's own height, so the layout is the same as before the fix.
- **Narrow window** (panel narrower than the row): the row scrolls sideways; every mode control stays in the bar
  and reachable. The main window's minimum width no longer includes the row, so the footer's Arrange/Mix/Edit and
  Browser switches stay on the screen and the finding's click lands.
- **Reconfiguration** (a chip's text changes the row's width, e.g. the Faders/Solo/Listen/Master/Zones chip
  labels): the bar's `LayoutRequest` is filtered and `fitHeight()` re-runs, so the scroll bar comes or goes and the
  height follows.
- **Resize**: `resizeEvent` re-runs `fitHeight()`.
- **The finding's path**: footer Edit -> `window.set_view` -> `Shell::setView` is unchanged and still synchronous;
  the fix only keeps the switch on the screen.

### Supported-platform hazards

`ModeBarScroller` uses only `QScrollArea`, `QScrollBar::sizeHint()`, `QEvent::LayoutRequest`,
`setWidgetResizable()`, `setFixedHeight()` and `QSizePolicy` - all present and unchanged in Qt 5.15 and Qt 6, on
Linux, macOS and MSVC/clang. The class is file-local in an anonymous namespace; no header changes, so no
additional target needs a rebuild. The regression test reads `QWidget::minimumSizeHint()`, resizes one widget
synchronously and reads the scroll bar's range - no platform-specific API.

### Round-2 evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-edit.r2/logs/`)

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > 01-build-green.log` | 0 | |
| GREEN test | `ctest -R '^MixerModesTest$' --output-on-failure > 02-test-green.log` | 0 | 1/1 passed |
| Family build | `cmake --build build --target <23 Mixer*/ShellTest targets> -j4 > 04-build-family.log` | 0 | |
| Family test | `ctest -R 'Mixer\|^ShellTest$' -j4 --output-on-failure > 05-test-family.log` | 0 | 23/23 passed |
| 4b revert | `git diff --cached -- src/gui/MixerView.cpp > fix.patch && git apply -R fix.patch` | 0 | |
| 4b rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > 06-build-reverted.log` | 0 | |
| 4b test | `ctest -R '^MixerModesTest$' --output-on-failure > 07-test-reverted.log` | 8 | `'window->minimumSizeHint().width() <= 1600' returned FALSE. (main window minimum width 2000)` |
| 4b reapply | `git apply fix.patch` | 0 | |
| 4b rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > 08-build-reapplied.log` | 0 | |
| 4b test | `ctest -R '^MixerModesTest$' --output-on-failure > 09-test-reapplied.log` | 0 | 1/1 passed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py . > 10-gates.log` | 0 | all 8 PASS, RESULT: PASS |

## Round 3 — re-verification on the pinned target

Round 3 is a re-queue of the same integration: the source is now the round-2 integration branch
(`integration/gui-1106-main-edit-af010bc3cc` @ `38dcfdbe43`) and the target is still pinned at
`050/m0-train-26` @ `af010bc3cc`. The driver's `integration_jobs.apply()` computes the patch as
`git diff af010bc3cc..38dcfdbe43` and applies it to a worktree whose HEAD is already `38dcfdbe43`, so it
returns without changing anything (`if head != target: return`). The integrated result is therefore the
round-2 tree, and this round re-verifies it.

### The "newer line" conflict is on train-27, not on the pinned target

The request's reason ("merge conflict with the newer line in: src/gui/MixerView.cpp") comes from the train
driver's forward merge of the integration branch into `050/m0-train-27` (`a1a98b67b2`), not from the pinned
target:

- `git merge-tree --write-tree --messages a1a98b67b2 38dcfdbe43` reports
  `CONFLICT (content): Merge conflict in src/gui/MixerView.cpp` and
  `CONFLICT (content): Merge conflict in tests/upstream-modifications.txt` (`12-conflict-analysis.log`).
- Train-27's `MixerView.cpp` carries a *different* fix for the same cause: `ModeBarStrip` (4 occurrences, from
  the gui-1030/gui-1105 mixer merges) and `outer->addWidget(new ModeBarStrip(this, m_modeBar));`. It has no
  `ModeBarScroller`.
- The pinned target `af010bc3cc` has neither: its line 118 is still `outer->addWidget(m_modeBar);`. The
  accepted patch's only deletion is that line; it adds the `ModeBarScroller` class and two includes.

So on the pinned target the accepted patch applies cleanly and no target-side line is lost. The train-27
conflict is a duplicate-fix collision between `ModeBarScroller` (this cluster) and `ModeBarStrip`
(gui-1030/gui-1105); resolving it would require merging train-27 into this branch, which the job's pinned
target and the no-merge rule both forbid. It is reported here rather than silently dropped.

### Integration identity re-check (this tree)

- `git diff --stat af010bc3cc HEAD` = 4 files, 244 insertions, 1 deletion (the report, `MixerView.cpp`,
  `MixerModesTest.cpp`, `upstream-modifications.txt`).
- Blob equality with the accepted branch `c1965faa4f`: `src/gui/MixerView.cpp` `544130dab5`,
  `tests/src/gui/MixerModesTest.cpp` `1cd1e0ca52`, `tests/upstream-modifications.txt` `d71f201088` — all identical.
- The accepted fix commit's parent is `af010bc3cc` and its `MixerView.cpp` blob is `30aadfb983`, exactly the
  target's blob: the accepted patch's preimage is the pinned target, so it applies cleanly.

### Round-3 evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-edit.r3/logs/`)

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > 01-build-green.log` | 0 | |
| GREEN test | `ctest -R '^MixerModesTest$' --output-on-failure > 02-test-green.log` | 0 | 1/1 passed |
| Family build | `cmake --build build --target <23 Mixer*/ShellTest targets> -j4 > 04-build-family.log` | 0 | |
| Family test | `ctest -R 'Mixer|^ShellTest$' -j4 --output-on-failure > 05-test-family.log` | 0 | 23/23 passed |
| 4b revert | `git diff af010bc3cc HEAD -- src/gui/MixerView.cpp > fix.patch && git apply -R fix.patch` | 0 | 1 insertion, 52 deletions |
| 4b rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > 06-build-reverted.log` | 0 | |
| 4b test | `ctest -R '^MixerModesTest$' --output-on-failure > 07-test-reverted.log` | 8 | `FAIL! ... 'window->minimumSizeHint().width() <= 1600' returned FALSE. (main window minimum width 2000)` at `MixerModesTest.cpp:819` |
| 4b reapply | `git apply fix.patch` | 0 | |
| 4b rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > 08-build-restored.log` | 0 | |
| 4b test | `ctest -R '^MixerModesTest$' --output-on-failure > 09-test-restored.log` | 0 | 1/1 passed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py . > 10-gates.log` | 0 | all 8 PASS, RESULT: PASS |
| Completion | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py . gui-1106-main-edit > 11-submit-check.log` | 0 | RESULT: PASS - submit |
| Conflict analysis | `git merge-tree --write-tree --messages a1a98b67b2 38dcfdbe43 > 12-conflict-analysis.log` | 0 | 2 conflicts (MixerView.cpp, upstream-modifications.txt) |

Note: `03-test-family.log` is the first family attempt, which exited 8 only because the other 22 test
executables were not built yet; `04-build-family.log` builds them and `05-test-family.log` passes 23/23.

## Self-check

Answers to the submit_check ASK items. Round 3 re-ran the same proof on this exact tree (logs in
`~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-edit.r3/logs/`); the round-2 evidence is in
`~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-edit.r2/logs/`:

1. **The regression test FAILS with the product change reverted and PASSES with it.** Round 3 re-ran this on the
   exact integrated tree: `git diff af010bc3cc HEAD -- src/gui/MixerView.cpp > fix.patch && git apply -R fix.patch`
   (EXIT 0; 1 insertion, 52 deletions), rebuild `zene` + `MixerModesTest` (EXIT 0, `06-build-reverted.log`),
   `ctest -R '^MixerModesTest$'` gave **EXIT 8**: `'window->minimumSizeHint().width() <= 1600' returned FALSE.
   (main window minimum width 2000)` at `MixerModesTest.cpp:819` (`07-test-reverted.log`). `git apply fix.patch`
   (EXIT 0), rebuild (EXIT 0, `08-build-restored.log`), same ctest gave **EXIT 0**, 1/1 passed
   (`09-test-restored.log`). The round-2 proof is the same result on the same source/test content
   (`r2/logs/07-test-reverted.log`, `r2/logs/09-test-reapplied.log`), and the round-1 negctl proof (`r1/negctl`)
   predates it.
2. **The test is deterministic.** It only reads layout size hints (`QWidget::minimumSizeHint`), resizes one widget
   synchronously, and reads the scroll bar's range. It has no timer, sleep, wait or event-loop race. The panel is
   opened with the synchronous `MainWindow::revealPanel`. No timestamps are involved, so there is no injected value
   to name.
3. **The fix is at the cause and complete.** The cause is the mode bar's minimum width flowing into the panel's
   minimum. `MixerModeBar` is created only at `src/gui/MixerView.cpp:165`, and that is the only place changed. No code
   depends on the bar's parent or its layout index. `MixerView::modeBar()` and every object name are unchanged. All 23
   `Mixer*`/`ShellTest` tests pass (`05-test-family.log`, EXIT 0). The sibling footer clusters from the sweep (Mix,
   Browser, Arrange) share this cause. The sweep showed no other panel widening the window (`0000`-`0074` fit;
   widening starts only after `0075`, the Mixer click).
4. **No collateral damage.** Nothing is saved or loaded differently: the change is widget nesting only, with no
   model, project, config or command change. No limit or check was added. At widths where the row fits, the scroll
   bar is hidden and the scroller's height equals the bar's own height, so the layout is the same as before.
   GUI thread only; nothing touches the audio thread, undo or the command registry. The integrated tree is
   byte-identical to the accepted patch, so the round-1 compatibility result carries over.
5. **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: the verdict is FIXED.

- **Gate row in submit_check.** In round 2 the stock `feature_check.py` ran in place and passed: EXIT 0,
  RESULT: PASS (`10-gates.log`). The round-1 PermissionError (creating `<worktree>-feature-check` next to the
  worktree) did not recur, so the jail-safe copy is no longer needed for this report.
- **Integration is byte-identical to the accepted patch.** `git diff --cached c1965faa4f` over the four files is
  empty and the index blobs match the accepted commit's blobs; the target's `MixerView.cpp` differs from the
  accepted branch only by this fix, so no target-side line was lost. The round-3 RED/GREEN reversal was re-run on
  this exact tree: EXIT 8 without the product change, EXIT 0 with it (`07-test-reverted.log`, `09-test-restored.log`).
- **The train-27 conflict is reported, not hidden.** The "newer line" is train-27's `ModeBarStrip`
  (`a1a98b67b2`), a different fix for the same cause; the pinned target `af010bc3cc` has neither fix, so the
  accepted patch applies cleanly there. `git merge-tree --write-tree --messages a1a98b67b2 38dcfdbe43`
  (`12-conflict-analysis.log`) shows the two forward-merge conflicts; resolving them needs a merge that the
  job's pinned target and the no-merge rule forbid, so the conflict is documented instead.
