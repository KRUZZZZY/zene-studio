FIXED

# Cluster gui-1030-tab-mixer-density-expanded - Expanded density choice is clipped in a narrow Mixer panel

## Integration onto 050/m0-train-26 (round 2, 2026-11-09)

Integration job: the accepted patch `bugs/qwen-gui-1030-tab-mixer-density-expanded @ d3d77ccbcc` onto
`050/m0-train-26 @ af010bc3cc`. The driver had reported a conflict in `src/gui/MixerModeBar.cpp`.

**Result: the integrated tree equals the accepted patch exactly. No conflict needed resolving and there is no semantic difference.**
- `git merge-base af010bc3cc d3d77ccbcc` prints `af010bc3cc...`. The accepted branch was already cut from this
  train tip; `git log af010bc3cc..d3d77ccbcc` lists only its 3 own commits (0f78bb577b, a917ec74fc, d3d77ccbcc).
- The worktree had no conflict markers (`grep '<<<<<<<\|>>>>>>>'` on the three changed sources found nothing).
- `git diff --cached d3d77ccbcc` was **empty**. The staged integration tree is byte-identical to the accepted
  source in every file: `include/MixerModeBar.h`, `src/gui/MixerModeBar.cpp`, `tests/src/gui/MixerModesTest.cpp`,
  `tests/upstream-modifications.txt` and this report. So the accepted design (`WrapLayout`, right-aligned mode
  group) and the regression test are kept unchanged.

Affected failure paths and platform hazards, re-checked against the integrated tree:
- Narrow mixer (< 1610 px single row): controls wrap onto further rows at their size hints. Covered by the 320 px half of the test.
- Wide mixer: one row, mode chips plus Routing Matrix right-aligned as before (`alignRightFrom`, `MixerModeBar.cpp:256`).
  Covered by the wide half of the test.
- Hidden items are skipped (`shown()`). `minimumSize()` is the widest control, so the panel can't be narrowed below one control.
- Height-for-width: the host `QVBoxLayout` in `MixerView` honours `hasHeightForWidth`, so wrapped rows get their height.
- Header change (`include/MixerModeBar.h:87,90`, `QLayout*`): all 10 mixer GUI test targets that include it were rebuilt.
- Platforms: only standard Qt6 `QLayout` API (no platform code, no new files, no new dependencies). There is no audio-thread,
  saved-data, command or undo surface.

| Step (round 2; logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1030-tab-mixer-density-expanded.r2/logs`) | Command | EXIT |
| --- | --- | --- |
| build (ccache) | `cmake --build build --target zene MixerModesTest ... (10 mixer tests) -j4 > logs/build-1.log` | 2 - `ccache: error: failed to create temporary file ... Permission denied` (sandbox), not a compile error |
| build | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest VcaStripLifetimeTest AccessibilityWalkTest MixerInsertLatencyTest MixerBusFreezeMenuTest MixerStripFeaturesTest MixerSendsOnFadersTest MixerSelectionIdentityTest -j4 > logs/build-2.log` | 0 |
| family GREEN | from `build/tests`: `ctest -R '^(MixerModesTest\|VcaStripLifetimeTest\|AccessibilityWalkTest\|MixerInsertLatencyTest\|MixerBusFreezeMenuTest\|MixerZonesTest\|MixerStripFeaturesTest\|MixerSendsOnFadersTest\|MixerSelectionIdentityTest\|MixerMeteringTest)$' --output-on-failure > logs/test-1-family.log` | 0 - 100% tests passed out of 10 |
| 4b patch | `git diff --cached af010bc3cc -- src/gui/MixerModeBar.cpp include/MixerModeBar.h > logs/fix.patch` | 0 |
| 4b revert | `git apply -R logs/fix.patch` | 0 |
| 4b rebuild reverted | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > logs/build-3-reverted.log` | 0 |
| 4b RED | `ctest -R '^MixerModesTest$' --output-on-failure > logs/test-2-red.log` | 8 - `FAIL! aNarrowModeBarKeepsEveryControlOnScreen() 'expanded->width() >= expanded->minimumSizeHint().width()' returned FALSE. (Density: Expanded is squeezed out of sight)` MixerModesTest.cpp(284) |
| 4b reapply | `git apply logs/fix.patch` (afterwards `git diff` was empty, the index still equal to d3d77ccbcc) | 0 |
| 4b rebuild reapplied | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > logs/build-4-reapplied.log` | 0 |
| 4b GREEN | `ctest -R '^MixerModesTest$' --output-on-failure > logs/test-3-green.log` | 0 - 100% tests passed out of 1 |
| gates (stock) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log` | 1 - `PermissionError` creating `<worktree>-feature-check` (outside the write-jail), before any gate ran |
| gates (log dir moved) | `python3 -I logs/gatescript/feature_check_jail.py <worktree> > logs/gates-2.log` (the same script; `diff` shows only `ROOT` pinned to the real qwen dir and `logs` moved into the LOG FOLDER; baseline `gate-baselines/af010bc3cc...json` was already cached) | 0 - RESULT: PASS. All 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item in your files), file-length (no new item in your files) |

Correction to the round-1 text below: the accepted patch *does* register a file in `tests/upstream-modifications.txt`.
It adds `tests/src/gui/MixerModesTest.cpp` at line 5751 (the inherited test file changed). The two product files are fork-new
(`tests/fork-sources.txt:992,2112`) and need no entry. The fork-sources and no-upstream-regression gates pass.

## Verdict: REAL

The mixer's mode bar laid all of its controls out in ONE `QHBoxLayout` row with no wrap and no overflow
path. That row is View (2) + Density (2) + Show + filter + Pick... + Follow Selection + five mode chips +
Routing Matrix..., and its single-row width is 1610 px (measured: `sizeHint() == QSize(1610, 38)` in the
pre-fix debug run, log `test-5-dbg.log`). When the mixer panel is narrower than that, Qt's box layout
squeezes every item below its minimum. Measured at 320 px: Density: Expanded got `QRect(105,4 13x30)`,
13 px wide against a size hint of 87 px, so its text can't be read and the button can't really be used.
When the panel's parent is the thing that clips, the row runs past the right edge instead. Either way,
this is the "not on screen in its view" result the sweep reported.

## The fix (at the cause)

- `src/gui/MixerModeBar.cpp:98-204`: `WrapLayout`, a small flow layout (after Qt's flow-layout example),
  in the file's anonymous namespace. It places items left to right at their size hints and wraps onto a
  new row when the next item doesn't fit. Each item is centred vertically in its row (`placeRow`, :171).
  - It implements `hasHeightForWidth`/`heightForWidth`, so the `QVBoxLayout` in `MixerView` gives the bar
    the height its rows need.
  - `sizeHint()` (:119) is still the single row, so a wide mixer looks the same as before.
  - `minimumSize()` (:130) is the widest single control, so the mixer can be narrowed without clipping.
  - Hidden items are skipped (`shown()`).
- `src/gui/MixerModeBar.cpp:214`: the bar uses `WrapLayout` instead of `QHBoxLayout`. The old stretch
  before the mode chips and Routing Matrix is preserved by right-aligning that group when it fits on the
  last row (`alignRightFrom`, `placeRow`). The controls keep their old wide-mixer placement and wrap at
  narrow widths.
- `include/MixerModeBar.h:36,87,90`: the private `addSegment`/`addChip` helpers take `QLayout*` instead
  of `QHBoxLayout*`. They only call `addWidget`. Both are private, and these are their only callers.

Siblings checked: every control on the bar goes through the one layout, so all of them wrap. No other
widget uses this layout. The header is included only by `MixerView.cpp` and the tests. All 10 mixer GUI
test targets were rebuilt against the changed header and pass. No command, saved data, audio-thread
path or undo step is touched; this is GUI layout only. No new files were added. Both changed product
files are fork-new (`tests/fork-sources.txt:992,2112`), so nothing needs registering in
`tests/upstream-modifications.txt`.

## Regression test

`tests/src/gui/MixerModesTest.cpp:270` `aNarrowModeBarKeepsEveryControlOnScreen`. It builds a
`MixerModeBar` in a 320 px host and lays it out at that width. Assertions:
- Density: Expanded and every other visible control lie inside the bar's rect;
- every one keeps at least its `minimumSizeHint().width()` (this check catches the squeeze);
- at a wide width, the bar is still a single row (Mix, Expanded and the Zones chip share a row centre)
  and Routing Matrix stays at the right content edge.

It is deterministic: no timers, no sleeps, and no window shown. `invalidate()` + `activate()` force each
layout pass.

## Commands and results (logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1030-tab-mixer-density-expanded.r1/logs)

The sandbox can't write ccache's temp dir (build-1 failed: "ccache: error: failed to create temporary
file ... Permission denied"), so every build used `CCACHE_DISABLE=1`.

| Step | Command | EXIT |
| --- | --- | --- |
| build (test only, before fix) | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > logs/build-6.log 2>&1` | 0 |
| RED (before fix) | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > logs/test-6-red.log` | 8 - `FAIL! aNarrowModeBarKeepsEveryControlOnScreen() 'expanded->width() >= expanded->minimumSizeHint().width()' returned FALSE. (Density: Expanded is squeezed out of sight)` MixerModesTest.cpp(283) |
| build with fix | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > logs/build-9.log 2>&1` | 0 |
| GREEN family (10 tests) | `ctest -R '^(MixerModesTest\|VcaStripLifetimeTest\|AccessibilityWalkTest\|MixerInsertLatencyTest\|MixerBusFreezeMenuTest\|MixerZonesTest\|MixerStripFeaturesTest\|MixerSendsOnFadersTest\|MixerSelectionIdentityTest\|MixerMeteringTest)$' > logs/test-13-family.log` | 0 - 100% tests passed out of 10 |
| 4b revert | `git diff -- src/gui/MixerModeBar.cpp include/MixerModeBar.h > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b rebuild reverted | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > logs/build-11-reverted.log` | 0 |
| 4b test reverted | `ctest -R '^MixerModesTest$' --output-on-failure > logs/test-11-reverted.log` | 8 - the same "squeezed out of sight" failure |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b rebuild reapplied | `CCACHE_DISABLE=1 cmake --build build --target zene MixerModesTest -j4 > logs/build-12-reapplied.log` | 0 |
| 4b test reapplied | `ctest -R '^MixerModesTest$' --output-on-failure > logs/test-12-reapplied.log` | 0 - 100% tests passed out of 1 |

One run of the family (`test-9-green.log`) had a single failure in
`MixerStripFeaturesTest::theStripMenuSetsThatStripsMeterMode` ("VU -0.1" where VU 0.0 was expected). That
is a meter reading and has nothing to do with layout. It passed on both immediate reruns
(`test-10-stripfeatures-1.log`, `-2.log`: EXIT=0) and in the full family run `test-13` (EXIT=0), so I treat
it as an existing load flake.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 with a `PermissionError`
before running any gate. It writes its logs to `<worktree>/../<name>-feature-check`, which is outside this
lane's write-jail (`logs/gates-1.log`). A copy that changes only `ROOT` and the logs path
(`logs/gatescript/feature_check_jail.py`) got through the six simple gates. It then exited 1 because no
baseline is cached for base `af010bc3cc` and the bunny-guard refuses the script's own
`git -C .../gate-baselines/tree checkout` (`logs/gates-2.log`). So I ran every gate directly from the worktree:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity | `bash tests/complexity-gate.sh --check` | 1 (red on the train on purpose; none of its 7 REGRESSION lines names a file this branch changes) |
| file-length | `bash tests/file-length-gate.sh --check` | 1 (red on the train on purpose; none of its 26 REGRESSION lines names a file this branch changes; MixerModeBar.cpp is 420 lines) |

The ratchet comparison was done by hand: `grep -h REGRESSION logs/gates/complexity.log
logs/gates/file-length.log | grep -i MixerMode` printed nothing. Because the script never finished, its
PASS/FAIL table was not produced.

Independent review, after preserving the previous wide-mixer alignment:

- `cmake --build build --target zene MixerModesTest VcaStripLifetimeTest AccessibilityWalkTest MixerInsertLatencyTest MixerBusFreezeMenuTest MixerZonesTest MixerStripFeaturesTest MixerSendsOnFadersTest MixerSelectionIdentityTest MixerMeteringTest -j4` — EXIT 0 (`review-build-final.log`).
- From `build/tests`, `ctest -R '^(MixerModesTest|VcaStripLifetimeTest|AccessibilityWalkTest|MixerInsertLatencyTest|MixerBusFreezeMenuTest|MixerZonesTest|MixerStripFeaturesTest|MixerSendsOnFadersTest|MixerSelectionIdentityTest|MixerMeteringTest)$' --output-on-failure` — EXIT 0, 10/10 (`review-test-family-rerun.log`). The first family run had the existing meter readout failure (`VU -0.1`, EXIT 8); `ctest -R '^MixerStripFeaturesTest$' --output-on-failure` then passed alone (EXIT 0), and the full focused family passed on rerun.
- For the negative control, `git apply -R logs/qa-layout-fix.patch` and `git apply -R negctl/product.patch` both exited 0; `cmake --build build --target zene MixerModesTest -j4` exited 0; `ctest -R '^MixerModesTest$' --output-on-failure` from `build/tests` exited 8 at `aNarrowModeBarKeepsEveryControlOnScreen`, with the original squeeze assertion. Restored with `git checkout -- src/gui/MixerModeBar.cpp include/MixerModeBar.h` (EXIT 0) and `git apply logs/qa-layout-fix.patch` (EXIT 0), then rebuilt and reran the focused family above.
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1030-tab-mixer-density-expanded` — EXIT 0, RESULT: PASS; complexity and file-length report no new item in changed files (`review-gate-3.log`).

## Files committed

- `src/gui/MixerModeBar.cpp`
- `include/MixerModeBar.h`
- `tests/src/gui/MixerModesTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-gui-1030-tab-mixer-density-expanded.md`

## Self-check

- **The test fails with the fix reverted and passes with it:** yes. With the fix reverted
  (`git apply -R logs/fix.patch`, EXIT 0; rebuild EXIT 0), `ctest -R '^MixerModesTest$'` gave EXIT 8 with
  "Density: Expanded is squeezed out of sight" at MixerModesTest.cpp(283). With it reapplied
  (`git apply logs/fix.patch`, EXIT 0; rebuild EXIT 0), the same command gave EXIT 0, 1/1 passed.
- **The test is deterministic:** no timers, sleeps, event-loop waits or shown windows. The bar is laid out
  synchronously at fixed widths (320 px, then a wide width 120 px beyond its size hint). `QLayout::invalidate()` +
  `activate()` are the seam that forces each layout pass. The inputs are those two widths.
- **The fix is at the cause and complete:** the cause is the single `QHBoxLayout` row
  (MixerModeBar.cpp:190, formerly `new QHBoxLayout(this)`). Every control on the bar is added through that
  one layout, via the constructor, `addSegment` and `addChip`, so all of them now wrap. `addSegment` and
  `addChip` are private and are only called from the constructor; their parameter type changed in the
  header (:87,:90) and the .cpp together. `MixerView.cpp:114-118` is the only place that hosts the bar;
  its `QVBoxLayout` honours height-for-width. Every test target that includes the header or `MixerView.h`
  was rebuilt and passes, 10/10 (`test-13-family.log`).
- **No collateral damage:** layout only. No saved data, project format, command, schema, audio-thread
  code or undo step is touched, so old projects load as before. No limit or check was added. On a wide
  mixer the bar remains one row and the mode chips plus Routing Matrix retain their prior right alignment;
  the regression checks both properties.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable; the verdict is FIXED (REAL). Pre-fix
  evidence: Expanded was laid out at `QRect(105,4 13x30)` against a size hint of 87 px (`test-5-dbg.log`).
- **Original lane gate attempt:** `feature_check.py` couldn't finish inside that session's write-jail. Its logs directory
  `<worktree>/../*-feature-check` is unwritable, and the guard refuses its baseline-tree `git checkout`.
  The independent review rerun above completed the required gate table successfully.
