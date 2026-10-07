# Cluster new-p8-126 — non-finite cue settings on hardware outputs

Verdict: **FIXED**

## Finding

`std::clamp()` does not sanitize NaN read from project XML: a hand-edited project with
`level="nan"` or `pan="nan"` on a cue send or cue master loads NaN into the cue state; NaN
levels pass the `<= 0.f` guards in `sumCue`/`mixOutputs` and are multiplied into the output
samples (NaN pan contaminates the send's left/right factors).

## Evidence

- `src/core/HardwareIo.cpp:88-89` (`loadCueSends`): `send.level = std::clamp(cue.attribute("level").toFloat(), 0.f, 2.f)` and the same for `pan` — `std::clamp(NaN, lo, hi)` returns NaN (both comparisons false).
- `src/core/HardwareIo.cpp:281` (`loadMixer`): same pattern for the cue master's `level`.
- Reachability verified empirically on this build's Qt 6.4.2: `QString("nan").toFloat()` returns NaN (probe compiled against the system Qt6Core).
- Consumers: `HardwareIo.cpp:127` `if (send.level <= 0.f ...)` — NaN fails the guard; `HardwareIo.cpp:133-134` multiply level/pan into samples; `HardwareIo.cpp:238/240` the cue master's level into `bus.add`.

## Fix

Sanitize non-finite attribute values to the attribute's own default before clamping, matching the codebase convention (`DeviceOptions.cpp:106`, `ModulationFormula.cpp:281`):

- `loadCueSends`: `level` → `std::isfinite(v) ? std::clamp(v, 0.f, 2.f) : 0.f`; `pan` → same with `: 0.f`.
- `loadMixer`: cue master `level` → `std::isfinite(v) ? std::clamp(v, 0.f, 2.f) : 1.f` (the cuemix's default level).
- Added `#include <cmath>` to `src/core/HardwareIo.cpp` (only `<algorithm>` was present).

Finite values (including ±inf, which `std::clamp` already bounds) keep their previous behaviour.

## Regression

New slot `nonFiniteCueSettingsSanitizeToDefaults()` in `tests/src/core/HardwareIoTest.cpp`: loads a
`<hardwareio>` element with `level="nan"`/`pan="nan"` through `ChannelHardwareIo::load` and a
`<cuemix level="nan">` through `hardwareio::loadMixer`, asserting the results are finite and equal
the defaults (0 / 0 / 1).

## Commands and exit codes (logs in ~/.local/share/zene-bunny-fix/qwen/runs/new-p8-126.r1/logs)

RED (original code):

- `cmake --build build --target HardwareIoTest > LOGS/build-1.log 2>&1` → EXIT=0
- `QT_QPA_PLATFORM=offscreen ctest -R HardwareIoTest --output-on-failure > LOGS/test-1.log 2>&1` (from `build/tests`) → EXIT=8
  - `test-1.log:22`: `FAIL! : HardwareIoTest::nonFiniteCueSettingsSanitizeToDefaults() 'std::isfinite(loaded.cues[0].level) && std::isfinite(loaded.cues[0].pan)' returned FALSE.`
  - `test-1.log:29`: `0% tests passed, 1 tests failed out of 1`

GREEN (after fix):

- `cmake --build build --target HardwareIoTest > LOGS/build-2.log 2>&1` → EXIT=0
- `QT_QPA_PLATFORM=offscreen ctest -R HardwareIoTest --output-on-failure > LOGS/test-2.log 2>&1` (from `build/tests`) → EXIT=0
  - `test-2.log:5`: `100% tests passed, 0 tests failed out of 1`
- `cmake --build build --target zene > LOGS/build-3.log 2>&1` → EXIT=0

Gates:

- `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` → EXIT=0
- `bash tests/file-length-gate.sh --check >> LOGS/gate-1.log 2>&1` → EXIT=0

## Files committed

- `src/core/HardwareIo.cpp` (fix)
- `tests/src/core/HardwareIoTest.cpp` (regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p8-126.md` (this report)

No new product files: `src/core/HardwareIo.cpp` and `tests/src/core/HardwareIoTest.cpp` are already
registered in `tests/fork-sources.txt` (lines 1440 and 1949); no inherited upstream file changed, so
`tests/upstream-modifications.txt` is untouched.
