# Cluster new-p9-507 — FIXED

## Verdict

**FIXED.** `Vst3Instrument::processImpl()` now zeros every supplied output channel for `out.frames()` before returning when the VST3 processor is unprepared. The loop uses bounded `std::fill_n` calls on the audio thread. The existing `HostedPlugin::process()` clear also remains in place for channels outside the plug-in's active buses.

## File and line evidence

- `plugins/Vst3Instrument/Vst3Instrument.cpp:338-348`: the unprepared branch clears all planar output channels, records the re-prepare request, and returns.
- `plugins/Vst3Effect/Vst3Host.cpp:78,615`: the host-level clear handles caller outputs before its own unprepared return and bus mapping.
- `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp:338-361`: renders a sustained nonzero block, releases the hosted processor, then checks sentinel-initialized audio through `Instrument::play()` and `AudioPortsRouter`.
- `tests/src/plugins/Vst3InstrumentTest.cpp:271-289`: a mono fixture declares one bus channel while the host supplies two output channels; the second channel must be zero.
- `tests/data/vst3-test-instrument/vst3-test-instrument.cpp:63-70,163,206-211`: the fixture builds a separate mono VST3 variant with its own class ID and one-channel arrangement.

## Verification evidence

Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs`. Build commands ran from the repository root; ctest commands ran from `build/tests`.

| Evidence | Exact command | Result |
| --- | --- | --- |
| Original-code route RED | `ctest -R '^Vst3InstrumentIntegrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | ctest exit 8; the unprepared-route assertion failed on stale samples. |
| Mono fixture build and pre-fix family | `cmake --build build --target zene vst3instrument Vst3InstrumentTest Vst3InstrumentIntegrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` then `ctest -R '^(Vst3InstrumentTest|Vst3InstrumentIntegrationTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` | Build exit 0; ctest exit 8 because the route regression was still RED. `Vst3InstrumentTest`, including the mono two-output case, passed. |
| Fix build and GREEN | `cmake --build build --target zene vst3instrument Vst3InstrumentTest Vst3InstrumentIntegrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/build-5.log 2>&1; result=$?; echo EXIT=$result` then `ctest -R '^(Vst3InstrumentTest|Vst3InstrumentIntegrationTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` | Build exit 0; ctest exit 0, 2/2 tests passed. |
| 4b: save and reverse only this round's product change | `git diff -- plugins/Vst3Instrument/Vst3Instrument.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/fix.patch; result=$?; echo EXIT=$result` | Exit 0. |
| 4b: rebuild with the fix reversed | `cmake --build build --target zene vst3instrument Vst3InstrumentIntegrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/build-6.log 2>&1; result=$?; echo EXIT=$result` | Build exit 0. |
| 4b: route RED with the fix reversed | `ctest -R '^Vst3InstrumentIntegrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` | ctest exit 8; the same unprepared-route assertion failed again. |
| Restore the saved product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/fix.patch; result=$?; echo EXIT=$result` | Exit 0. |
| Final build | `cmake --build build --target zene vst3effect vst3instrument Vst3HostTest Vst3ChunkProbeTest Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/build-10.log 2>&1; result=$?; echo EXIT=$result` | Build exit 0. |
| Final affected VST3 family | `ctest -R '^(Vst3HostTest|Vst3ChunkProbeTest|Vst3InstrumentFixtureProbe|Vst3InstrumentTest|Vst3InstrumentIntegrationTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/test-8.log 2>&1; result=$?; echo EXIT=$result` | ctest exit 0; 5/5 tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/gate-7.log 2>&1; result=$?; echo EXIT=$result` | Exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r2/logs/gate-8.log 2>&1; result=$?; echo EXIT=$result` | Exit 0. |

Harness wiring had two earlier build failures while exposing `HostedPlugin::release()` to the integration test: the first link attempt lacked the host implementation, and the next had duplicate core support symbols. The host implementation sources were added and duplicate core sources removed; the subsequent target builds above passed. The first attempt's wrapper exit echo was not captured, so no exit code is claimed for that log.

## Committed files

- `plugins/Vst3Instrument/Vst3Instrument.cpp`
- `tests/CMakeLists.txt`
- `tests/data/vst3-test-instrument/CMakeLists.txt`
- `tests/data/vst3-test-instrument/vst3-test-instrument.cpp`
- `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp`
- `tests/src/plugins/Vst3InstrumentTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-507.md`
