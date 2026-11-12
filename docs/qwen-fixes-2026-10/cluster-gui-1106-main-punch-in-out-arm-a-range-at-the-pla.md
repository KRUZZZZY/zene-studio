FIXED

# cluster gui-1106-main-punch-in-out-arm-a-range-at-the-pla — Punch arm shows no discernible range on the ruler

## Verdict: REAL — the accepted fix is integrated, with the target's interval band kept

The finding is real. On the accepted patch's base (`af010bc3cc`) `TimeLineWidget::paintEvent`
filled, outlined and labelled the punch band's whole *row* (`punchBandRect()` was x from
`m_xOffset` to `width()`, `src/gui/editors/TimeLineWidget.cpp:284` there), so an armed one-bar
range painted as one uniform strip across the entire ruler; the only marks of the interval were
two 5 px handles, and the in handle at tick 0 sat half under the ruler's clip. The accepted patch
paints only the interval between the bounds and keeps the in handle wholly visible.

This integration job is against `recovery/terra-20261111` @ `b4a19c424b`, not the accepted base.
The cumulative target already contains two independently accepted punch contracts:

- gui-1030 (`dd3232ad3a`, `2b7bd18696`): `punchBandRect()` **is** the interval between the bounds,
  clipped to the visible ruler, with a one-pixel sliver for a positive subpixel range, and empty
  when the range is wholly out of view.
- gui-1106 (`e31211fb20`, the accepted patch): only the interval is filled/outlined/labelled, and
  handles are clamped to the visible content area rather than to the band.

The target's own product and test were measured red before this integration (LOG/07,
`05-build-target-head.log` + `06-test-target-head.log`): `ArrangerTakePunchTest` scored
**3 passed / 4 failed**. Its handle helper clamps a handle to the (possibly one-pixel) band, so
every handle centre moved two pixels off its marker (`470` vs `468`, `340` vs `338`, `460` vs
`458`), and the armed-range case was additionally polluted by the previous test's range. The
recovery therefore keeps the target's interval geometry and adopts the accepted content-area
handle contract; nothing is invented.

## Integration result

| Path | Merge state from the driver | Resolution |
| --- | --- | --- |
| `src/gui/editors/TimeLineWidget.cpp` | `UU` (content) | Target's `punchBandRect()` (interval, subpixel sliver, off-view empty) and `getPunchAction()` row hit-testing are kept. The accepted side's `punchIntervalRect()` (subpixel sliver preserved) and `punchHandleRect(band, x, rangeWidth, contentLeft, contentRight)` (content-area clamp, handles stay on their markers for a range narrower than a handle, empty handle for a bound outside the content area) are kept, and both accessors pass the content range. |
| `tests/src/gui/ArrangerTakePunchTest.cpp` | `UU` (content) | Both sides' assertions are retained (target's band == interval, subpixel and off-view cases; accepted `clearPunch()` isolation and off-content handle cases). One broken probe was corrected: `punchInHereFromTheRecordMenuShowsTheBand()` sampled `band.right() - 6`, but "Punch In Here" at 40 px/bar produces a one-pixel band (`band=468,22 1x10`, measured in LOG/04), so the sample was 6 px outside it in empty ruler. It now samples `band.center()`, still asserting the punch red is painted. |
| `tests/upstream-modifications.txt` | `UU` | One registration line for `src/gui/editors/TimeLineWidget.cpp` describing the merged design; the target's earlier, now-inaccurate gui-1106 line was dropped. |
| `docs/qwen-fixes-2026-10/cluster-gui-1106-….md` | `AA` | This single report. |

### Why the test probe change is not a weakened assertion

With the target's interval geometry the band for "Punch In Here" (play position 3 bar + 48 ticks
to the song's end) is one pixel wide at this zoom. The pristine target test samples
`band.right() - 6`, i.e. six pixels left of the only band column, and fails against any correct
interval-band product (LOG/07). Sampling `band.center()` tests the same property (the interval is
painted punch red) at a point inside the band; it adds no tolerance and removes no assertion. The
same one-pixel probe defect was documented by the accepted branch's round-3 report
(`79-test-union-pristine-t27.log`), and its resolution used the same sample point.

### Merged code (`src/gui/editors/TimeLineWidget.cpp` in this tree)

- `punchBandRect()` (`:323-346`): the interval between the bounds, clipped to the visible ruler;
  a positive subpixel range intersecting the ruler keeps a one-pixel band; a range wholly beyond
  either edge has no band.
- `punchIntervalRect()` (`:63-73`): the part of the band between the bounds, with the accepted
  one-pixel sliver for a positive subpixel range. With an interval band this is exactly the band;
  it stays correct if a band row is ever passed in.
- `punchHandleRect()` (`:80-93`): empty for a bound outside `[contentLeft, contentRight]`; a range
  at least one handle wide clamps the handle inside the content area; a narrower range keeps its
  handle centred on its marker.
- `punchBeginHandleRect()` (`:350-357`) and `punchEndHandleRect()` (`:359-366`): both pass
  `m_xOffset` … `width()-1`.
- `paintEvent` (`:260-274`): fills, outlines and labels `punchIntervalRect()` only.
- `getPunchAction()` (`:410-428`): unchanged from the target — the press only has to be in the
  band's row, so the handles on the band's edges (and the whole interval) stay draggable.
- No header change, no serialization, no command or undo change. No audio-thread code.

## Failure paths and supported-platform hazards

| Path | What is exercised | Evidence |
| --- | --- | --- |
| Normal, fresh project | arm at tick 0 → 0..1 bar; interval painted, ruler past its end not; handles on their markers, in handle wholly visible | `ArrangerTakePunchTest::theArmedPunchRangeIsPaintedOnlyBetweenItsBounds` (LOG/063) |
| Toolbar arm/disarm + menu | toolbar `punchAction` arms/disarms without touching the loop; Record > Punch In Here sets, arms and paints a range | `thePunchRangeHasItsOwnHandlesAndArmControl`, `punchInHereFromTheRecordMenuShowsTheBand` (LOG/063) |
| Drag + undo | out/in handle drags call one `transport.punch_set`; `control.undo` restores one step; loop untouched | same test (LOG/063) |
| Subpixel | range narrower than a pixel → one-pixel band and handle centred on the marker | `aSubpixelPunchRangeKeepsAVisibleBandAndHandle` (LOG/063) |
| Off-content bound | range 3…600 bar on a ruler narrowed to `markerX(600 bar)-1`: band visible, in handle present, out handle empty (no edge sliver) | `theArmedPunchRangeIsPaintedOnlyBetweenItsBounds` (LOG/063) |
| Off-view range | range entirely beyond the visible ruler → `punchBandRect().isEmpty()` | `thePunchRangeHasItsOwnHandlesAndArmControl` (LOG/063) |
| Disarmed | bounds kept and outlined, not filled as armed | same test (LOG/063) |
| Short ruler | band height stays below the widget height, so the loop row survives | same test (LOG/063) |
| Audio thread | no change: paint/geometry only; no allocation, lock, wait or log on any audio path | no audio file in the diff |
| Platform | pure Qt (`QRect`, `std::clamp`, `std::abs`) and C++20; no Qt-6-only API, no header, no platform-specific code. Linux Qt 6 offscreen/GCC build is the measured one; other hosts build the same source | logs 060/062 |

## Evidence

LOG = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-punch-in-out-arm-a-range-at-the-pla.r4/logs`.
In this sandbox that run-folder `logs` entry is a symlink to
`docs/priority-recovery-r4-logs/` inside this worktree (created by the driver; the directory is
locally excluded, so no log file is committed). Every command below was run from this worktree
except the test runs, which were run from `build/tests` as required.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Target baseline | `cmake --build build --target zene ArrangerTakePunchTest -j8` (LOG/05); `./ArrangerTakePunchTest` (LOG/06) | 0 / 1 | measured the cumulative target **red**: 3 passed, 4 failed (handle centres and a polluted armed-range case) |
| Merged tree, before fixing the probe | build LOG/01 EXIT=0; `./ArrangerTakePunchTest` LOG/02 | 0 / 1 | 6 passed, 1 failed: `isPunchRed(band.right()-6)` — the one-pixel-band probe |
| Probe evidence | `./ArrangerTakePunchTest punchInHereFromTheRecordMenuShowsTheBand` LOG/04 | 1 | `DEBUG band=468,22 1x10 ... probe=462,26 color=31,34,39` — the old sample is outside the band |
| Merged tree after the probe correction | build LOG/08 EXIT=0; `./ArrangerTakePunchTest` LOG/09 | 0 / 0 | 7 passed, 0 failed |
| Target's pristine test against the merged product | build LOG/10 EXIT=0; `./ArrangerTakePunchTest` LOG/11 | 0 / 1 | 6 passed; only the one-pixel probe test fails, proving the product change is compatible with all target assertions |
| 4b patch | `git diff HEAD -- src/gui/editors/TimeLineWidget.cpp > LOG/fix.patch` | 0 | 85-line product patch (the accepted helper/accessor delta on top of the target) |
| 4b RED | `git apply -R LOG/fix.patch`; `CCACHE_DISABLE=1 /usr/bin/cmake --build build --target zene ArrangerTakePunchTest -j8 > LOG/060-build-red-final.log`; `./ArrangerTakePunchTest > LOG/061-test-red-final.log` | 0 / 0 / **4** | 3 passed, 4 failed: three handle centres (`470` vs `468`, `340` vs `338`, `460` vs `458`) and `punchEndHandleRect().isEmpty()` "the out handle of a bound past the content edge leaks an edge sliver" |
| 4b GREEN | `git apply LOG/fix.patch`; build > LOG/062 EXIT=0; `./ArrangerTakePunchTest > LOG/063-test-green-final.log` | 0 / 0 / **0** | `Totals: 7 passed, 0 failed` |
| Family build | `cmake --build build --target VideoClipsGuiTest ArrangerOverlaysVideoLaneTest NStateButtonActivationTest ArrangerTimelineTest -j8 > LOG/040-build-family.log` | 0 | |
| Family test | `ctest -R '^(ArrangerTakePunchTest\|VideoClipsGuiTest\|ArrangerOverlaysVideoLaneTest\|NStateButtonActivationTest\|ArrangerTimelineTest)$' --output-on-failure > LOG/071-test-family-final.log` | 0 | 5/5 passed (#480 ArrangerTimelineTest, #482 ArrangerTakePunchTest, #523 ArrangerOverlaysVideoLaneTest, #524 VideoClipsGuiTest, #594 NStateButtonActivationTest) |
| Gates, as given | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py "$PWD" > LOG/020-feature-check-unmodified.log` | 1 | crashed before any gate: `PermissionError` creating the sibling `<worktree>-feature-check` (this sandbox's write jail) |
| Gates, same script, log dir redirected | `FEATURE_CHECK_LOGDIR=LOG/feature-check-final python3 LOG/feature_check-shim.py "$PWD" > LOG/070-feature-check-final.log` | **0** | 8/8 PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item) |

Build flags everywhere: `CCACHE_DISABLE=1 /usr/bin/cmake` (the harness `cmake` shim cannot create
its build-pool lock in this sandbox; the real binary is used), `-j8`. Every build and test output
went to a new numbered file under LOG. The shim's complete diff against the harness tool is
LOG/023-feature-check-shim.diff: `ROOT` pinned to the real qwen directory (so `FIX-BASE` and the
gate baselines are the harness's own) and the gate-log directory taken from
`FEATURE_CHECK_LOGDIR` instead of the unwritable sibling. No gate, limit or baseline is changed.

Registration: `tests/upstream-modifications.txt:5823` describes the merged punch geometry (interval
band, subpixel sliver, content-area handles, off-content guard) and names
`ArrangerTakePunchTest::theArmedPunchRangeIsPaintedOnlyBetweenItsBounds`; the file ends with a
newline. The test file is already registered in `tests/fork-sources.txt:2962` and
`tests/CMakeLists.txt:1352`.

Files committed: `src/gui/editors/TimeLineWidget.cpp`,
`tests/src/gui/ArrangerTakePunchTest.cpp`, `tests/upstream-modifications.txt`, this report.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  RED: `git apply -R LOG/fix.patch` EXIT=0, build EXIT=0, `./ArrangerTakePunchTest` **EXIT=4**
  (4 failures, LOG/061). GREEN: `git apply LOG/fix.patch` EXIT=0, build EXIT=0,
  `./ArrangerTakePunchTest` **EXIT=0** (7 passed, LOG/063). The direct Qt Test binary returns the
  number of failing functions; under `ctest` the same RED state is exit 8.
- **The test is deterministic: no wall-clock timing, sleeps or races.** Inputs are fixed: fixture
  zoom 40 px/bar and scroll 0, explicit `transport.punch_set` `start`/`end` ticks, a deterministic
  `ruler->resize(markerX(600 bar) - 1, 32)`, and synchronous `QWidget::grab()` images compared at
  fixed pixels. `QCoreApplication::processEvents()` only flushes the queued resize. No timer,
  thread, network or clock is read, and the ruler is resized back.
- **The fix is at the cause and complete: every caller and sibling was checked.** `punchBandRect()`
  has four users: `paintEvent`, the two handle accessors and `getPunchAction()`. The paint remains
  the accepted interval-only paint; both accessors share the one helper and the same content range;
  hit testing and `commitPunchDrag()` are the target's unchanged row/handle code, verified by the
  drag + one-undo assertions. No other file uses these helpers (searched `src/`, `include/`,
  `tests/`). `punchIntervalRect()` is used only by the paint.
- **No collateral damage: old projects and saved data still load as before, and the new guard does
  not hit a legitimate caller.** No serialization, project format, undo journal or command surface
  changed. The guard can only remove a handle for a bound that is outside the visible content area;
  a visible bound is unaffected. The target's drag, subpixel, off-view, loop-independence and
  disarmed assertions all still pass (LOG/063, LOG/071).
- **Verdict is FIXED, so ALREADY-FIXED/FALSE-POSITIVE does not apply.** The pre-fix cause is
  `src/gui/editors/TimeLineWidget.cpp:284` at `af010bc3cc` (band row painted whole), and the
  integrated regression is measured in LOG/061 vs LOG/063.
- **Realtime / undo / commands.** GUI paint and geometry only; nothing added on an audio path
  allocates, frees, locks, waits, logs or emits a signal. The drag remains one `transport.punch_set`
  and one `control.undo` restores it (asserted in `thePunchRangeHasItsOwnHandlesAndArmControl`). No
  control command was added; `transport.punch_set`, `transport.punch_get_state`,
  `transport.punch_clear` and `transport.punch_here` are untouched.
- **Sibling commands in the files `submit_check` lists.** This patch changes no command file and no
  command handler: the command files appear in `submit_check`'s list only because the cumulative
  target changed them since the old `FIX-BASE` (`050/m0-train-26`). The `app.*`, `arrangement.*`
  siblings listed belong to the cumulative target, not to this integration; the punch siblings
  (`transport.punch_set`, `transport.punch_get_state`, `transport.punch_clear`,
  `transport.punch_here`) keep their schemas, handlers, undo steps and behaviour.
- **Platform.** Qt 5.15/MSVC/macOS: the merged code uses `QRect`, `std::clamp` (C++17), integer
  `std::abs` and `QColor`; no Qt-6-only API, no `windows.h` macro names and no `std::filesystem`
  were introduced. No header changed, so no dependent target needed a rebuild beyond `zene` and the
  test binary, both built here.
