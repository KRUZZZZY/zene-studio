# Cluster new-p9-185

**Verdict: FALSE-POSITIVE.** The reported period transition cannot happen on the current engine path while a routed instrument bus remains alive.

## Evidence

- [AudioEngine.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-185/src/core/AudioEngine.cpp:76) initializes `m_framesPerPeriod` once, in the constructor, as `min(framesPerAudioBuffer, DEFAULT_BUFFER_SIZE)`. [AudioEngine.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-185/include/AudioEngine.h:60) defines `DEFAULT_BUFFER_SIZE` as 256, so a 512-frame internal period is not supported on this branch.
- [AudioEngine.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-185/include/AudioEngine.h:359) exposes the period through a getter. [setAudioDevice()](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-185/src/core/AudioEngine.cpp:673) replaces the device and restarts processing, but does not update the engine period.
- The VST3 and CLAP hosts prepare plug-ins with `engine->framesPerPeriod()` and pass the output view's frame count to `InstrumentOutputBuses::deliver()` ([Vst3Instrument.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-185/plugins/Vst3Instrument/Vst3Instrument.cpp:121), [ClapInstrument.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-185/plugins/ClapInstrument/ClapInstrument.cpp:140)). LV2 passes its current output span size to bus delivery ([Lv2Instrument.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-185/plugins/Lv2Instrument/Lv2Instrument.cpp:191)). These are engine-sized blocks; a device switch does not make an existing bus receive a new internal period.
- Replacing the engine requires `Engine::destroy()`, which clears the project before deleting the engine ([Engine.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-185/src/core/Engine.cpp:96)). The hosted instrument and its `InstrumentOutputBuses` are project-owned, so an old bus slot does not span that teardown.

Therefore `setLayout()` does not need to resize slots for the described event. No production code or regression test was added. A test that passed a different frame count directly to `routedBuffer()` would exercise its documented size-mismatch guard, not the reported device/host transition.

## Verification

| Check | Command / result |
| --- | --- |
| RED command | Not run; false-positive finding, so no reachable regression command or exit code applies. |
| GREEN command | Not run; no production fix was made, so no GREEN command or exit code applies. |
| Affected family | `InstrumentRoutingTest`: 1/1 ctest target passed. |
| Build | `cmake --build build --target InstrumentRoutingTest` — exit 0. |
| Test | From `build/tests`: `ctest -R '^InstrumentRoutingTest$' --output-on-failure` — exit 0; 1 test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` — exit 0. |
| File length gate | `bash tests/file-length-gate.sh --check` — exit 0. |

Build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-185.r1/logs/`. The requested `fff grep` tool was not available in this session; log terms were checked with `rg` separately (`error`, `FAILED`, `Assertion`).

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-185.md` (this report; no product files changed)
