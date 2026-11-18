# cluster new-p10-377 — Zooming out can make the waveform disappear

Verdict: **FIXED**

## Finding

`plugins/SlicerT/SlicerTWaveform.cpp:475` (historical): zoom is clamped only at
zero and used as a scale factor in waveform drawing. At zero the editor sample
rect has zero height (waveform vanishes); there is no upper bound (out-of-range
geometry conversions).

## Evidence

- `plugins/SlicerT/SlicerTWaveform.cpp:475` (original):
  `m_zoomLevel = std::max(0.0f, m_zoomLevel);` — floor is exactly 0, no ceiling.
- Consumer `plugins/SlicerT/SlicerTWaveform.cpp:186` and `:193`
  (`drawEditorWaveform`):
  `long zoomOffset = (m_editorHeight - m_zoomLevel * m_editorHeight) / 2;`
  `.sampleRect = QRect(0, zoomOffset, m_editorWidth, static_cast<long>(m_zoomLevel * m_editorHeight))`
  — at zoom 0 the rect height is 0; unbounded zoom blows the rect past the widget.
- `plugins/SlicerT/SlicerTWaveform.h:101`: `float m_zoomLevel = 1.0f;` (default).
- Per wheel event the level moves `120/360 * s_zoomSensitivity(0.5) = 1/6`, so
  ten wheel-downs from 1.0 hit the old floor exactly (0.0), and 120 wheel-ups
  from there reach 20.0.

## Fix

Clamp zoom to a useful positive range instead of only flooring at zero:

- `plugins/SlicerT/SlicerTWaveform.h` (next to `s_zoomSensitivity`):
  `static constexpr float s_minZoom = 0.1f;` and `static constexpr float s_maxZoom = 10.f;`
- `plugins/SlicerT/SlicerTWaveform.cpp` (`wheelEvent`):
  `m_zoomLevel = std::clamp(m_zoomLevel, s_minZoom, s_maxZoom);`
  (replacing `std::max(0.0f, m_zoomLevel)`; `<algorithm>` already used in this TU).

Behaviour-preserving elsewhere: in-range scroll steps are untouched
(asserted by the test's sanity case).

## Regression test

New file `tests/src/plugins/SlicerTZoomTest.cpp`, registered in
`tests/CMakeLists.txt` as its own executable that compiles the SlicerT plugin
sources into the test binary (the `WasmSandboxTest` precedent — the plugin is a
module, not linkable into tests). It drives `wheelEvent` with Qt6 `QWheelEvent`
events and asserts:

1. `inRangeScrollStepIsPreserved` — one wheel-up from 1.0 gives 7/6 exactly
   (passes before and after the fix; guards against over-clamping).
2. `zoomingOutNeverCollapsesTheWaveform` — after ten wheel-downs,
   `m_zoomLevel > 0.0f` (fails on the original code: zoom was exactly 0.0).
3. `zoomingBackUpStaysBounded` — after ten downs + 120 ups,
   `m_zoomLevel <= 15.0f` (fails on the original code: zoom reached 20.0).

## Commands and exit codes

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-377.r1/logs`
(`LOGS` below). The fix was authored first, then reverse-applied via
`fix.patch` to restore the original code for the RED run, then re-applied for
GREEN — this is the step-4b proof: the test fails without the fix and passes
with it.

| Step | Command | EXIT |
|---|---|---|
| Regenerate build tree for the new target | `cmake --build build --target rebuild_cache > LOGS/regen-1.log 2>&1` | 0 |
| Save fix patch + reverse-apply (restore original) | `git diff -- plugins/SlicerT/SlicerTWaveform.cpp plugins/SlicerT/SlicerTWaveform.h > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| RED build (original code) | `cmake --build build --target SlicerTZoomTest > LOGS/build-3.log 2>&1` | 0 |
| RED run (must FAIL) | `cd build/tests && ctest -R SlicerTZoomTest --output-on-failure > LOGS/test-1.log 2>&1` | 8 |
| Re-apply fix | `git apply LOGS/fix.patch` | 0 |
| GREEN build (plugin + test) | `cmake --build build --target slicert SlicerTZoomTest > LOGS/build-4.log 2>&1` | 0 |
| GREEN run (must PASS) | `cd build/tests && ctest -R SlicerTZoomTest --output-on-failure > LOGS/test-2.log 2>&1` | 0 |
| Family run | `cd build/tests && ctest -R SlicerT --output-on-failure > LOGS/test-3.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` | 0 |

RED log (`test-1.log`) recorded failures:

```
FAIL!  : SlicerTZoomTest::zoomingOutNeverCollapsesTheWaveform() 'wf.m_zoomLevel > 0.0f' returned FALSE.
   Loc: [../../tests/src/plugins/SlicerTZoomTest.cpp(95)]
FAIL!  : SlicerTZoomTest::zoomingBackUpStaysBounded() 'wf.m_zoomLevel <= 15.0f' returned FALSE.
   Loc: [../../tests/src/plugins/SlicerTZoomTest.cpp(122)]
```

GREEN (`test-2.log`): `100% tests passed, 0 tests failed out of 1`.
Family (`test-3.log`): `ctest -R SlicerT` selected 1 test, 0 failed.

## Committed files

- `plugins/SlicerT/SlicerTWaveform.cpp` (clamp in `wheelEvent`)
- `plugins/SlicerT/SlicerTWaveform.h` (clamp constants)
- `tests/src/plugins/SlicerTZoomTest.cpp` (new regression test)
- `tests/CMakeLists.txt` (test registration)
- `tests/fork-sources.txt` (new test registered)
- `tests/upstream-modifications.txt` (SlicerTWaveform.cpp/.h reasons appended)
- `docs/qwen-fixes-2026-10/cluster-new-p10-377.md` (this report)
