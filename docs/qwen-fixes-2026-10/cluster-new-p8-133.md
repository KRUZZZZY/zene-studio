# Cluster new-p8-133

Verdict: **NEEDS-CONTEXT**

The discard is real: `InstrumentOutputBuses::deliver()` passes only the first channel and, at most, the second channel of a routed bus to the stereo `SampleFrame` interleaver. The third and later channels are ignored. A temporary four-channel regression reproduced this: with only the third or fourth bus channel nonzero, the routed mixer channel received silence.

The bus data does not specify speaker roles or a port layout. `InstrumentOutputBus` records only the channel count and flat starting channel (`include/InstrumentOutputBus.h:35-40`); the VST3 descriptor similarly records a count without a speaker arrangement (`plugins/Vst3Effect/Vst3BusMap.h:37-57`). The destination mixer channel is stereo. Choosing how channels beyond the first pair map into stereo would therefore invent product behavior. A policy decision is needed before a production fix can be made.

## File and line evidence

- `src/core/InstrumentOutputBuses.cpp:67-75`: `interleave()` writes only its left and right input pointers to the stereo frame.
- `src/core/InstrumentOutputBuses.cpp:223-233`: `deliver()` selects `firstChannel` and at most `firstChannel + 1`, then sends those two pointers to `interleave()`.
- `include/InstrumentOutputBus.h:35-40`: the bus exposes a channel count and flat offset, without per-channel roles.
- `plugins/Vst3Effect/Vst3BusMap.h:52-57`: the VST3 bus descriptor retains `channelCount` but no speaker arrangement.

## Evidence

| Item | Command/result |
| --- | --- |
| RED build | `cmake --build build --target InstrumentRoutingTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-133.r1/logs/build-1.log 2>&1; echo EXIT=$?` — EXIT=0 |
| RED test | From `build/tests`: `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-133.r1/logs/test-1.log 2>&1; echo EXIT=$?` — EXIT=8. CTest ran 1/1 registered test; QtTest reported 8 passed and 1 failed, the temporary multichannel regression. |
| GREEN test | Not run: no downmix policy was selected, so there is no behavior to verify as green. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-133.r1/logs/gate-1.log 2>&1; echo EXIT=$?` — EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-133.r1/logs/gate-2.log 2>&1; echo EXIT=$?` — EXIT=0 |

Affected family: `InstrumentRoutingTest` (1 CTest executable; 9 QtTest cases in the run).

The temporary regression was removed so this report-only commit does not leave the test suite failing. No production behavior or test source is committed. Explicit file committed: `docs/qwen-fixes-2026-10/cluster-new-p8-133.md`.
