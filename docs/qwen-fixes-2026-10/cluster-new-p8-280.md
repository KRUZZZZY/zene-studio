# cluster new-p8-280 — Truncated CLAP output buses report their untruncated channel count

Verdict: **FIXED**

## Finding
`plugins/ClapEffect/ClapBusMap.cpp:75` — in `outputBusesOf()`, `channels` is capped to the
transport budget (`std::min(port.channelCount, maxChannels - next)`), but the bus was pushed with
`port.channelCount`. A 70-channel output port with 64 channels of budget maps 64 channels yet
reports a 70-channel bus, so consumers can describe/route channels the transport does not provide.

## Evidence
- `plugins/ClapEffect/ClapBusMap.cpp:74-76` (before fix):
  `const int channels = std::min(port.channelCount, maxChannels - next);`
  `buses.push_back({port.name, port.channelCount, channels > 0 ? next : -1});` — declared count reported.
- `include/InstrumentOutputBus.h:38` — `channels` is "the bus's own channel count"; `deliver()`
  (`src/core/InstrumentOutputBuses.cpp:230`) uses it to size the stereo interleave, so an inflated
  count describes channels the transport never provides.
- `include/lmms_constants.h:39` — `MaxChannelsPerAudioBuffer = 128`.
- CLAP hosts never set an enable hook (`plugins/ClapInstrument/ClapInstrument.cpp:138` only calls
  `setLayout`), so a budget-dropped bus can never activate: reporting the mapped count (0 for a
  fully dropped bus) is the consistent contract, matching the finding's "report the mapped
  `channels` count".

## Fix
`plugins/ClapEffect/ClapBusMap.cpp` — report the mapped count instead of the declared one:

```cpp
const int channels = std::min(port.channelCount, maxChannels - next);
const int mapped = std::max(channels, 0);
buses.push_back({port.name, mapped, channels > 0 ? next : -1});
next += mapped;
```

`firstChannel` behaviour and the budget accumulation are unchanged.

## Regression
New slot `testTruncatedOutputBusReportsMappedChannels()` in the existing
`tests/src/plugins/ClapBusMapTest.cpp`: a 64-channel main + 70-channel extra + 2-channel dropped
port. Asserts `buses[1].channels == 64` (mapped, not declared 70) and `buses[2].channels == 0`,
`firstChannel == -1` for the dropped bus.

## Commands (LOGS = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-280.r1/logs)

RED (original code):
- `cmake --build build --target ClapBusMapTest > $LOGS/build-1.log 2>&1` → EXIT=0
- `QT_QPA_PLATFORM=offscreen ctest -R ClapBusMapTest --output-on-failure > $LOGS/test-1.log 2>&1` → EXIT=8
  - test-1.log: `FAIL! : lmms::clap::ClapBusMapTest::testTruncatedOutputBusReportsMappedChannels() Compared values are not the same` / `Actual (buses[1].channels): 70` (expected 64)

GREEN (after fix):
- `cmake --build build --target ClapBusMapTest > $LOGS/build-2.log 2>&1` → EXIT=0
- `QT_QPA_PLATFORM=offscreen ctest -R ClapBusMapTest --output-on-failure > $LOGS/test-2.log 2>&1` → EXIT=0 (1/1 Passed)
- `cmake --build build --target zene > $LOGS/build-3.log 2>&1` → EXIT=0 (library compiles ClapBusMap.cpp)
- `cmake --build build --target ClapHostTest ClapLoaderErrorTest ClapEffectIntegrationTest > $LOGS/build-4.log 2>&1` → EXIT=0
- `QT_QPA_PLATFORM=offscreen ctest -R Clap --output-on-failure > $LOGS/test-3.log 2>&1` → EXIT=8
  (3 family binaries were `***Not Run` — never built before; not a regression)
- `QT_QPA_PLATFORM=offscreen ctest -R Clap --output-on-failure > $LOGS/test-4.log 2>&1` → EXIT=0 (4/4 Passed: ClapBusMapTest, ClapHostTest, ClapLoaderErrorTest, ClapEffectIntegrationTest)

Gates:
- `bash tests/complexity-gate.sh --check > $LOGS/gate-1.log 2>&1` → EXIT=0
- `bash tests/file-length-gate.sh --check > $LOGS/gate-2.log 2>&1` → EXIT=0

## Files committed
- `plugins/ClapEffect/ClapBusMap.cpp` (fix; already registered in `tests/fork-sources.txt:926`)
- `tests/src/plugins/ClapBusMapTest.cpp` (regression test; already in `tests/all-sources.txt:2654`)
- `docs/qwen-fixes-2026-10/cluster-new-p8-280.md` (this report)

No new product files (no fork-sources.txt change needed); no inherited files changed
(no upstream-modifications.txt entry). No headers changed.
