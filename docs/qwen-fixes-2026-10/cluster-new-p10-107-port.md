# Cluster new-p10-107-port — FIXED

**Verdict: FIXED** — the Delay plug-in's XY pad could neither display nor set either maximum.

The pad divided each model range by the widget's pixel **count** instead of its drawable
**extent**, so a model maximum landed on the coordinate `width()` / `height()` (one pixel
outside the drawable area) and the last in-bounds mouse coordinate stopped one pixel short
of the maximum. The range is now mapped over `width() - 1` / `height() - 1` by one shared,
clamped mapping that the paint path and the drag path both use — the same contract the
main window's pad already follows (`src/gui/XyPad.cpp:176-178` and `:200-201`: "The last
pixel is the end of the range here too, as in moveTo(): drawing over width() would put the
dot's centre outside the widget").

This is a port of the fix accepted on `bugs/qwen-new-p10-107` (commits `49d1913c2`
"Fix Delay XY pad max mapping" and `9c1db493d` "Strengthen Delay XY pad boundary
regression"), written by hand against the newest line. The original branch could not be
merged because `XyPadPairTest` added `plugins/Delay/DelayControlsDialog.cpp` to the test
target while the newest line's `tests/src/gui/XyPadPairTest.cpp` already `#include`s that
source (`#define PLUGIN_NAME delay` / `#include "../../../plugins/Delay/DelayControlsDialog.cpp"`
/ `#undef PLUGIN_NAME`, lines 46-48) — the two together would give duplicate symbols. **This
port changes no CMake file at all**: the regression lives in the existing `XyPadPairTest`
source, which already compiles the Delay pad's own sources.

## Evidence

| Area | Evidence |
|---|---|
| Bug, before the fix | `plugins/Delay/DelayControlsDialog.cpp:104-110` (paint) divided the range by `width()` / `height()`; `:148-155` (drag) did the same. `git show HEAD:plugins/Delay/DelayControlsDialog.cpp` (base `ded355440`) is the quoted code. |
| Bug, after the fix | `plugins/Delay/DelayControlsDialog.h:42-65` holds the shared mapping `xyPadCoordinates::pixelForValue` / `valueForPixel` (clamped, zero- and one-pixel-guarded). `plugins/Delay/DelayControlsDialog.cpp:99` and `:141` return early for a zero-sized pad; `:106-108` (paint) and `:149-153` (drag) both use the shared mapping. |
| Sibling already consistent | `src/gui/XyPad.cpp:176-178` (paint) and `:200-201` (`moveTo`) map over `width() - 1` / `height() - 1` with a `std::max(1, …)` guard — the contract this fix gives the Delay pad. No other plug-in has an XY pad (`rg "class XyPad"` finds only `plugins/Delay/DelayControlsDialog.h` and `include/XyPad.h`). |
| Regression (new) | `tests/src/gui/XyPadPairTest.cpp:216` `delayPadLastPixelSetsMaximum` — a drag to the pad's last pixel must set **both** models to their maximum; `:232` `delayPadMaximumDrawsAtLastPixel` — the dot for the maximum must be painted **at** the last pixel (the pen is eight pixels wide, so a dot at the last pixel covers the column four pixels to its left; a dot one pixel further right, outside the widget, leaves that column empty). The pad is rendered with `QWidget::DrawChildren` (no window background), so only the dot is on the image. |
| Regression (existing, re-based) | `tests/src/gui/XyPadPairTest.cpp:186` and `:198` pinned the interior mapping to the old (buggy) divisor: `0.8f` for pixel 80 of a 100-pixel pad. The range is now mapped over the drawable extent, so the same pixel is `80/99` of the range. The two assertions were re-based to `80.f / 99.f` and `70.f / 99.f`; the *intent* of both cases (a left drag moves both models; another button's release does not stop the left drag) is unchanged and both still fail without the fix. |
| RED (regression written first) | `ctest -R '^XyPadPairTest$'` → **EXIT=8**, `Totals: 10 passed, 1 failed`: `delayPadLastPixelSetsMaximum() Compared floats are not the same` (the last pixel gave `0.99`, not the maximum `1.0`). `test-2.log`. |
| GREEN | `ctest -R '^XyPadPairTest$'` → **EXIT=0**, 1/1 registered ctest passed. `test-6.log`, `test-10.log`, `test-13.log`. |
| Reverse-patch proof | `git diff -- plugins/Delay/DelayControlsDialog.cpp plugins/Delay/DelayControlsDialog.h > …/fix.patch` (EXIT=0), `git apply -R …/fix.patch` (EXIT=0), rebuild (EXIT=0) → `ctest -R '^XyPadPairTest$'` **EXIT=8**, `Totals: 7 passed, 4 failed`: `delayPadLastPixelSetsMaximum`, `delayPadMaximumDrawsAtLastPixel` and the two re-based interior cases all fail. `git apply …/fix.patch` (EXIT=0), rebuild (EXIT=0) → **EXIT=0**, 1/1 passed. `fix.patch`, `build-9.log`, `test-9.log`, `build-10.log`, `test-10.log`. |
| Affected family | `ctest -R 'Delay\|XyPad'` → **EXIT=0**, 5/5 registered ctests passed (`MonoDelayTest`, `StereoDelay`, `XyPadPairTest`, `DelayControlsTest`, `DelayLfoAutomationTest`). `test-8.log`. |
| Pre-existing failure, not mine | `ctest -R 'PluginPortsMigration'` → **EXIT=8** with one failure, `nonRealtimeLadspaEffectsAreInvalid()` ("SWH Decimator test fixture was not loaded"). The same single failure appears with the fix reverted (rebuild EXIT=0, ctest **EXIT=8**, `Totals: 44 passed, 1 failed, 2 skipped`, `test-12.log`), so it is an unbuilt LADSPA fixture in this tree, not this change. The Delay-specific cases of that test pass in both states: `migratedPluginsPreserveBehaviour()` and `delayLfoControlDescribesItsPeriod()`. |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → **EXIT=0**, `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS; complexity and file-length "no new item in your files"). `gate-3.log`. The raw ratchets are red on the train (`tests/complexity-gate.sh --check` EXIT=1, `tests/file-length-gate.sh --check` EXIT=1) with only pre-existing items: `gate-1.log`, `gate-2.log`. |

## Commands and exit codes

All build and test output is under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-107-port.r1/logs`. The
worktree is `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-107-port`;
`ctest` ran from `build/tests`.

| Phase | Exact command | Result |
|---|---|---|
| Baseline (before any edit) | `cmake --build build --target XyPadPairTest delay > …/build-1.log 2>&1` | EXIT=0 |
| Baseline test | `ctest -R '^XyPadPairTest$' --output-on-failure > …/test-1.log 2>&1` | EXIT=0 (1/1 passed) |
| RED build (regression added, product untouched) | `cmake --build build --target XyPadPairTest > …/build-2.log 2>&1` | EXIT=0 |
| RED test | `ctest -R '^XyPadPairTest$' --output-on-failure > …/test-2.log 2>&1` | **EXIT=8** — 1 QtTest failure: `delayPadLastPixelSetsMaximum` |
| GREEN build (fix applied) | `cmake --build build --target XyPadPairTest delay > …/build-6.log 2>&1` | EXIT=0 |
| GREEN test | `ctest -R '^XyPadPairTest$' --output-on-failure > …/test-6.log 2>&1` | **EXIT=0** (1/1 passed) |
| Header dependents rebuilt | `cmake --build build --target DelayControlsTest DelayLfoAutomationTest zene > …/build-7.log 2>&1` | EXIT=0 |
| Affected family | `ctest -R 'Delay\|XyPad' --output-on-failure > …/test-8.log 2>&1` | **EXIT=0** (5/5 passed) |
| Save reverse patch | `git diff -- plugins/Delay/DelayControlsDialog.cpp plugins/Delay/DelayControlsDialog.h > …/fix.patch` | EXIT=0 |
| Reverse patch | `git apply -R …/fix.patch` | EXIT=0 |
| Reverse-proof build | `cmake --build build --target XyPadPairTest delay > …/build-9.log 2>&1` | EXIT=0 |
| Reverse-proof test | `ctest -R '^XyPadPairTest$' --output-on-failure > …/test-9.log 2>&1` | **EXIT=8** — `Totals: 7 passed, 4 failed` |
| Reapply patch | `git apply …/fix.patch` | EXIT=0 |
| Proof-green build | `cmake --build build --target XyPadPairTest delay > …/build-10.log 2>&1` | EXIT=0 |
| Proof-green test | `ctest -R '^XyPadPairTest$' --output-on-failure > …/test-10.log 2>&1` | **EXIT=0** (1/1 passed) |
| Migration test with the fix | `ctest -R 'PluginPortsMigration' --output-on-failure > …/test-11.log 2>&1` | EXIT=8 — 1 pre-existing LADSPA-fixture failure (44 passed, 1 failed, 2 skipped) |
| Migration test without the fix | `git apply -R …/fix.patch` (EXIT=0); `cmake --build build --target delay > …/build-12.log 2>&1` (EXIT=0); `ctest -R 'PluginPortsMigration' --output-on-failure > …/test-12.log 2>&1` | EXIT=8 — the same single failure, so it is pre-existing |
| Final state restored | `git apply …/fix.patch` (EXIT=0); `cmake --build build --target delay XyPadPairTest > …/build-13.log 2>&1` (EXIT=0); `ctest -R '^XyPadPairTest$' --output-on-failure > …/test-13.log 2>&1` | **EXIT=0** (1/1 passed) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > …/gate-3.log 2>&1` | **EXIT=0** — `RESULT: PASS` |
| Raw complexity ratchet | `bash tests/complexity-gate.sh --check > …/gate-1.log 2>&1` | EXIT=1 (pre-existing items only; none in the files this change touches) |
| Raw file-length ratchet | `bash tests/file-length-gate.sh --check > …/gate-2.log 2>&1` | EXIT=1 (pre-existing items only; no file of this change is over 500 lines) |

## Files committed

- `plugins/Delay/DelayControlsDialog.cpp` — the paint and drag paths use the shared mapping; a zero-sized pad returns early.
- `plugins/Delay/DelayControlsDialog.h` — the clamped `pixelForValue` / `valueForPixel` mapping shared by both paths.
- `tests/src/gui/XyPadPairTest.cpp` — the two regression cases; the two interior expectations re-based from `80/100` to `80/99`.
- `tests/upstream-modifications.txt` — the two changed inherited files registered with their reasons.
- `docs/qwen-fixes-2026-10/cluster-new-p10-107-port.md` — this report.

No new file was created, so no entry was needed in `tests/fork-sources.txt`
(`tests/src/gui/XyPadPairTest.cpp` is already registered there, line 2456) and no
`tests/CMakeLists.txt` change was needed (the target already exists and already compiles the
Delay pad's sources — that is what the original branch's duplicate-symbol clash was about).

## Self-check

- **"The regression test FAILS with your product change reverted and PASSES with it (exit
  codes)."** — `git apply -R fix.patch` (EXIT=0) + `cmake --build build --target XyPadPairTest delay`
  (EXIT=0) → `ctest -R '^XyPadPairTest$'` **EXIT=8** (`Totals: 7 passed, 4 failed`; the new
  cases `delayPadLastPixelSetsMaximum` and `delayPadMaximumDrawsAtLastPixel` fail, and so do
  the two re-based interior cases). `git apply fix.patch` (EXIT=0) + rebuild (EXIT=0) →
  `ctest -R '^XyPadPairTest$'` **EXIT=0**, 1/1 registered ctest passed. Logs `test-9.log`,
  `test-10.log`.
- **"The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
  injected values it uses)."** — No clock, no sleep, no thread, no timer: both new cases work
  on a 100×100 `XyPad` built over two plain `FloatModel`s (`DelayXyPadFixture`,
  `tests/src/gui/XyPadPairTest.cpp:132-144`). The drag case drives the widget with
  `QTest::mousePress` / `mouseMove` / `mouseRelease` and compares the model values with the
  models' own `maxValue()`; the paint case sets the two models to their maxima, renders the
  widget synchronously into a `QImage` (`QWidget::render`) and inspects one pixel. The
  model's value is the only input; the mapping's inputs are the model's min/max and the
  widget's pixel extent.
- **"The fix is at the cause and complete: every caller and sibling of what you changed was
  checked and stays consistent."** — The Delay plug-in's `XyPad` has exactly two paths that
  touch the mapping: `XyPad::paintEvent` (`plugins/Delay/DelayControlsDialog.cpp:97-113`) and
  `XyPad::mouseMoveEvent` (`:140-157`); both now call the same two helpers, so a value and
  the pixel that shows it cannot disagree. `XyPad::mousePressEvent` / `mouseReleaseEvent`
  (`:118-137`) only gate the left button and are untouched (that is cluster new-p10-108's
  behaviour, still covered by `delayXyPadIgnoresRightDrag` / `…MiddleDrag` /
  `…OtherButtonReleaseDuringLeftDrag`, all still passing). The sibling pad in the main
  window (`src/gui/XyPad.cpp:176-178`, `:200-201`) already maps over `width() - 1` /
  `height() - 1`, so the two pads now agree. `rg "class XyPad"` finds no third XY pad, and
  `rg "xInc|yInc"` shows the old `range / width()` pattern existed only in this file (and in
  the historical reference copy `tests/reference/Delay/DelayControlsDialog.cpp`, which is
  deliberately left untouched: it is the "before" source the plug-in migration test compares
  against). Header dependents rebuilt: the `delay` plug-in, `DelayControlsTest`,
  `DelayLfoAutomationTest`, `XyPadPairTest` and `zene`.
- **"No collateral damage: old projects and saved data still load as before, and any limit or
  check you added does not hit other callers that legitimately exceed it."** — The change
  touches no model, no range, no step size, no serialisation and no saved attribute: the
  Delay plug-in's `m_delayTimeModel` (0.01 … 5 s) and `m_feedbackModel` (0 … 1) are unchanged,
  so every saved project loads and renders exactly as before. The only behavioural change is
  the pad's own pixel↔value mapping; the interior of the range moves by the factor
  `width/(width-1)` (0.5 % on the plug-in's 200-pixel pad), which is the direct consequence
  of the fix the finding prescribes ("map the range across `width() - 1` and `height() - 1`")
  and of the contract the main window's pad already follows. The new guards are internal to
  the pad: `extent <= 1` (a one-pixel or zero-sized pad) and `maximum <= minimum` (an empty
  range) return the neutral value instead of dividing by zero, and both inputs are clamped
  into the range, so no caller can push a value outside its own model.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract that shows it."** — Not applicable: the verdict is FIXED, and the bug was present
  on this branch at base `ded355440` (`plugins/Delay/DelayControlsDialog.cpp:104-110` and
  `:148-155` divided the range by `width()` / `height()`), proven by the RED run above.
