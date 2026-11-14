FALSE-POSITIVE

# Cluster gui-1106-main-pop-out-the-note-editor-panel: "Pop-out button click leaves the Note Editor docked"

Verdict: **FALSE-POSITIVE**. The product code is unchanged. The sweep's click missed the button: it clicked the
Browser's column, not the Note Editor's pop-out button. Clicking the real button floats the Note Editor. The live
`window.detach` command, which the button dispatches, floats it too.

## Evidence

1. **The click went to the wrong place** (sweep run `terra-pilot/buttons/runs/m0-train-26`):
   - `inv-main.jsonl` (the sweep's own inventory) puts "Pop out the Note Editor panel" at **x=1257, y=712**, w=31 h=28.
     That is the right end of the Note Editor header, which spans the centre column. The Arranger button sits at
     x=1257, y=99, in the same column.
   - `results.jsonl:21` records the click for this control at **x=1539, y=712**. x=1539 is the Browser column: the
     Browser's own pop-out button is at x=1539, y=99.
   - `shots/0020-pair.png` shows the click marker on the Browser's instrument list, on the "Vibed" row, at about
     (769, 357) in the half-scale shot, which is (1538, 714) at full scale. It is not on the Note Editor header,
     which ends at x≈1320. So nothing could pop out, and the judge's "remains docked" is what a click on a list row
     produces.
2. **The button's code path is sound.**
   - `src/gui/shell/ShellFloating.cpp:424`: the header button's `clicked` calls `dispatchFloat(id, !isDetached(id))`.
   - `src/gui/shell/ShellFloating.cpp:220-231`: `dispatchFloat` invokes `window.detach {panel}` when the shell
     belongs to the control MainWindow, and calls `detachPanel(id)` directly otherwise.
   - `src/gui/ControlCommandsLayouts.cpp:228-244` (`movePanel`) → `detachTo` (`:219-226`) → `Shell::detachPanel`
     (`ShellFloating.cpp:237-265`). `detachPanel` refuses only an id with no panel frame. "detail" (Note Editor,
     `ShellLayout.cpp:79`) is mounted at `MainWindow.cpp:659`, so it is not refused.
   - Luna's "discards its result; a rejected invocation" describes a rejection that does not occur for this panel. The
     live probe below shows `ok: true, changed: true`.
3. **Live instance**: `build/zene` was started headless (offscreen, Dummy audio) and driven over the control socket.
   `window.detach {"panel":"detail"}` returned
   `{"ok": true, "result": {"changed": true, "floating": true, "panel": "detail"}}`. Afterwards `window.get_state`
   reported `detail` as `floating: true`. `window.attach` brought it back (`floating: false`). Log:
   `logs/01-live-probe.log`, script `logs/probe_detail.py`.
4. **Button-level regression guard (new)**: `tests/src/gui/ShellTest.cpp` → `theNoteEditorsPopOutButtonFloatsItAndDocksItBack`.
   The test clicks the Note Editor header's `panelFloat` button. It checks that the button's accessible name is
   "Pop out the Note Editor panel", that the click floats the panel in its own visible window, and that a second
   click docks it back in `shellBottom`. It passes on the unchanged product, which is what a false positive predicts.
   With no product change there is no fix patch to revert, so step 4b (RED without the fix) does not apply.

## Commands run (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-pop-out-the-note-editor-panel.r1/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| Live probe | `QT_QPA_PLATFORM=offscreen timeout 300 python3 $L/probe_detail.py build/zene > $L/01-live-probe.log 2>&1` | 0 |
| Build (1st) | `cmake --build build --target ShellTest -j4 > $L/02-build-ShellTest.log 2>&1` | 2 (ccache temp dir outside the write area; not a code error) |
| Build | `CCACHE_DISABLE=1 cmake --build build --target ShellTest -j4 > $L/04-build-ShellTest.log 2>&1` | 0 |
| ShellTest | `cd build/tests && ctest -R '^ShellTest$' --output-on-failure > $L/05-ctest-ShellTest.log 2>&1` (1 test, 100% passed) | 0 |
| New case alone | `./ShellTest theNoteEditorsPopOutButtonFloatsItAndDocksItBack > $L/06-ShellTest-new-case.log 2>&1` (3 passed, 0 failed) | 0 |
| Family build | `CCACHE_DISABLE=1 cmake --build build --target ShellLayoutTest ShellHomesTest -j4 > $L/08-build-shell-family.log 2>&1` | 0 |
| Shell family | `cd build/tests && ctest -R Shell --output-on-failure > $L/09-ctest-Shell-family.log 2>&1` (4 tests, 100% passed) | 0 |
| Gates | `python3 $L/feature_check_logs_redirected.py <worktree> > $L/11-feature-check.log 2>&1` (all 8 rows PASS) | 0 |

The plain `feature_check.py <worktree>` exited 1 (`logs/10-feature-check.log`) with a PermissionError. It creates its
log folder beside the worktree (`<worktree>-feature-check`), which is outside this lane's write area. The redirected
copy differs from the original in two lines only, shown by `diff`: `ROOT` is pinned to the original directory so it
uses the same FIX-BASE and gate baselines, and the log folder is under the run's logs. Every gate command and the
pass logic are unchanged.

## Files committed

- `tests/src/gui/ShellTest.cpp`: one new test case. It is an existing fork test file, so no registration change.
- `docs/qwen-fixes-2026-10/cluster-gui-1106-main-pop-out-the-note-editor-panel.md`: this report.

No product source changed: no realtime, undo or command-surface impact, and the ControlCommandsSnapshot is untouched.

## Note for the sweep

The same mis-targeting probably explains the sibling `gui-1106-main-pop-out-the-arranger-panel`. That case was not
checked here, so it is unverified. The harness appears to have used a stale or wrong x coordinate for this control:
the inventory says 1257, but the click went to 1539.

## Self-check

- **Test FAILS with the product change reverted and PASSES with it:** this does not apply because there is no product
  change (FALSE-POSITIVE). The new case `ShellTest::theNoteEditorsPopOutButtonFloatsItAndDocksItBack` passes on the
  unchanged product: `./ShellTest theNoteEditorsPopOutButtonFloatsItAndDocksItBack` EXIT=0 (3 passed, 0 failed), and
  `ctest -R '^ShellTest$'` EXIT=0. It is a guard against the behaviour the finding claims, not a fix proof.
- **Deterministic:** no timers, sleeps or waits. The test calls `QToolButton::click()`, which emits `clicked`
  synchronously, and checks `Shell::isDetached`, `floatingWindow` and the frame's host right after each call. The test
  World has no control MainWindow, so `dispatchFloat` takes its direct branch (`ShellFloating.cpp:223-227`). The
  registry branch (`:229`) was covered by the live probe (`logs/01-live-probe.log`, `ok: true, floating: true`).
- **At the cause and complete:** nothing in the product was changed. The cause of the report is the sweep clicking
  x=1539 (`results.jsonl:21`) instead of the inventory's x=1257 (`inv-main.jsonl`). Both callers of the shared path were
  checked: the header button (`ShellFloating.cpp:424`) and the float action/shortcut (`:210-216`) both go through
  `dispatchFloat` (`:220-231`) → `window.detach` (`ControlCommandsLayouts.cpp:228-244`) → `detachPanel`
  (`ShellFloating.cpp:237-265`).
- **No collateral damage:** only a test case and this report were added. There is no product, saved-data, limit or
  command change, and the Shell family is 4/4 passing (`logs/09-ctest-Shell-family.log`).
- **FALSE-POSITIVE evidence:** see Evidence 1-3 above. The click landed at (1539, 712) on the Browser list
  (`shots/0020-pair.png`, `results.jsonl:21`), while the button is at (1257, 712) (`inv-main.jsonl`). `detachPanel`
  refuses only an unknown id (`ShellFloating.cpp:239-244`), and "detail" is a mounted panel (`MainWindow.cpp:659`,
  `ShellLayout.cpp:79`). On a live instance, `window.detach {"panel":"detail"}` returned
  `ok: true, changed: true, floating: true`.
- **Gates:** `submit_check.py` reports "gates (feature_check.py)" as FAIL because `feature_check.py` exits 1 with
  PermissionError: it creates `<worktree>-feature-check` beside the worktree, outside this lane's write area
  (`logs/10-feature-check.log`). The same gates run through a copy that differs only in the log folder and pinned
  `ROOT` all pass, RESULT: PASS, EXIT=0 (`logs/11-feature-check.log`).
