# cluster new-p10-548 — NaN note velocity: invalid float-to-integer conversion

Verdict: **FIXED**

## Finding
`plugins/ClapEffect/ClapHostNotes.cpp:83` — `setNoteOn` computed
`static_cast<std::int16_t>(std::clamp(velocity, 0.0, 1.0) * 127.0)`.
`std::clamp` passes NaN through (both comparisons are false for NaN), and
converting NaN to `std::int16_t` is undefined behavior.

## Evidence (file:line)
- `plugins/ClapEffect/ClapHostNotes.cpp:83` (pre-fix): the NaN-producing
  clamp + int16 conversion, no finiteness guard.
- `plugins/ClapEffect/ClapHost.h:197` documents the contract: velocity is
  CLAP's 0..1 scale, and events that cannot be honored are "refused ... and
  counted" (the same counted-drop pattern as `pushNote`'s no-port path,
  `ClapHostNotes.cpp:113-121`). A NaN velocity is not a representable note,
  so the host refuses it and counts it — the only defined behavior that
  matches the documented refusal pattern; sanitizing NaN to 0 would instead
  start a voice at an arbitrary level (the MIDI caller at
  `plugins/ClapInstrument/ClapInstrument.cpp:254` deliberately converts
  velocity-0 note-ons to note-offs for exactly that reason).
- ±inf is left alone: `std::clamp` handles infinities with defined behavior
  (inf → 1.0, -inf → 0.0), so only NaN changes behavior.

## Fix
`plugins/ClapEffect/ClapHostNotes.cpp` — in `setNoteOn`, before the
conversion: if `std::isnan(velocity)`, count the drop
(`notesDropped.fetch_add` + `control::clapHostNoteCounters().recordDrop()`)
and return without queueing. Added `#include <cmath>`. No header change, so
no `.h`/`.cpp` pair to rebuild; the file is already registered in
`tests/fork-sources.txt` (line 939) and no new product file was added.

## Regression test
`tests/src/plugins/ClapHostTest.cpp` — new case
`testNoteOnRefusesNaNVelocity()`: with the instrument fixture (which
accepts notes), `setNoteOn(0, 60, NaN, 0)` must leave `pushed` unchanged,
increment `dropped` by 1, deliver nothing, and start no voice. Against the
buggy code the event was queued (`pushed` +1), so the case fails before the
fix and passes after.

## Commands and exit codes (logs in runs/new-p10-548.r1/logs)
RED on the original code:
- `cmake --build build --target ClapHostTest > LOGS/build-1.log 2>&1` → EXIT=0
- `ctest -R ClapHost --output-on-failure > LOGS/test-1.log 2>&1` → EXIT=8
  (1 test run; `testNoteOnRefusesNaNVelocity()` FAILed:
  `after.pushed - before.pushed` = 1, expected 0; 18 passed, 1 failed, 0 skipped)

GREEN after the fix:
- `cmake --build build --target ClapHostTest > LOGS/build-2.log 2>&1` → EXIT=0
- `ctest -R ClapHost --output-on-failure > LOGS/test-2.log 2>&1` → EXIT=0

Test-catches-the-bug proof (step 4b):
- `git diff -- plugins/ClapEffect/ClapHostNotes.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` → APPLY_R_EXIT=0
- `cmake --build build --target ClapHostTest > LOGS/build-3.log 2>&1` → EXIT=0
- `ctest -R ClapHost --output-on-failure > LOGS/test-3.log 2>&1` → EXIT=8 (FAIL without the fix)
- `git apply LOGS/fix.patch` → APPLY_EXIT=0
- `cmake --build build --target ClapHostTest > LOGS/build-4.log 2>&1` → EXIT=0
- `ctest -R ClapHost --output-on-failure > LOGS/test-4.log 2>&1` → EXIT=0 (PASS with the fix)

Other targets that compile the changed TU rebuilt:
- `cmake --build build --target ClapLoaderErrorTest ClapEffectIntegrationTest clapeffect zene > LOGS/build-5.log 2>&1` → EXIT=0
- `cmake --build build --target ClapBusMapTest > LOGS/build-6.log 2>&1` → EXIT=0
- `ctest -R Clap --output-on-failure > LOGS/test-6.log 2>&1` → EXIT=0
  (affected family: 4 tests executed — ClapBusMapTest, ClapHostTest,
  ClapLoaderErrorTest, ClapEffectIntegrationTest — 100% passed)

Gates:
- `bash tests/complexity-gate.sh --check` → EXIT=0 (LOGS/gate-complexity.log)
- `bash tests/file-length-gate.sh --check` → EXIT=0 (LOGS/gate-length.log)

## Files committed
- `plugins/ClapEffect/ClapHostNotes.cpp` (the fix)
- `tests/src/plugins/ClapHostTest.cpp` (the regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p10-548.md` (this report)
