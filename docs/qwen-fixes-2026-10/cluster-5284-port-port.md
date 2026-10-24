FIXED

# Cluster 5284-port-port — SRC converter creation on the render path

## Decision and code evidence

**REAL.** `SampleTrack::play()` creates a `SamplePlayHandle` for a clip at `src/tracks/SampleTrack.cpp:293`. Its clip constructor runs the tagged state setup at `src/core/SamplePlayHandle.cpp:137-138`; before this fix, the embedded `Sample::PlaybackState` used the ordinary `AudioResampler` constructor, whose `createConverterState()` calls `src_new()` at `src/core/AudioResampler.cpp:84-92, 362-368`. `PatmanInstrument::selectSample()` created another default `PlaybackState` at `plugins/Patman/Patman.cpp:528`, reached while a note is rendered (`src/core/NotePlayHandle.cpp:358`).

Comped sample clips had the same sibling path: `CompSlipReaders::openReaders()` emplaced up to four default playback states at `src/core/CompSlip.cpp:79-85` while the clip handle was being constructed. The deterministic sample test exposed that sibling separately: with the main clip state already prepared, it still observed one converter allocation until comp-slip readers were tagged.

The original `Sample::play()` body now sets the ratio and processes audio only (`src/core/Sample.cpp:100-121`); quality changes for retained states already use the engine transaction. This round closes the remaining new-state paths. Tagged clip, comp-slip, and Patman states claim prepared slots (`src/core/SamplePlayHandle.cpp:137-138`, `src/core/CompSlip.cpp:84`, `plugins/Patman/Patman.cpp:528`). `AudioEngine::initDevices()` initializes the pool off the render path (`src/core/AudioEngine.cpp:769-775`); converter states are created in pool chunk creation (`src/core/AudioResampler.cpp:292`). Quality changes prepare and adopt pool states under the engine change guard before publishing the new quality (`src/core/AudioEngine.cpp:275-289, 294-337`).

The pool grows in chunks as the play-handle capacity grows (`src/core/AudioResampler.cpp:324-346`, `src/core/AudioEngine.cpp:1390-1397, 1489-1495`), with room for the primary state plus the four `CompSlipReaders::MaxReaders` states per play handle (`src/core/AudioEngine.cpp:137-145`). This adds no fixed voice limit. Project serialization and saved SRC settings are unchanged.

## Regression and verification evidence

All `ctest` commands below ran from `build/tests`; build commands ran from the worktree root. Each command’s output was saved in a new numbered log under the requested run log folder.

| Stage | Exact command | Exit | Evidence |
|---|---|---:|---|
| Build the expanded regression before its comp-slip fix | `cmake --build build --target AudioResamplerRatioTest -j4` | 0 | `build-13.log` |
| Comp-slip RED with the main SampleTrack state already prepared | `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` | 8 | `test-10.log`: `sampleTrackVoiceUsesPreparedConverterOnRenderPath` observed 1 RT converter allocation; the existing ratio and mismatch-rate checks passed. |
| Save and reverse all production hunks | `git diff -- include/AudioResampler.h include/Sample.h include/SamplePlayHandle.h plugins/Patman/Patman.cpp src/core/AudioEngine.cpp src/core/AudioResampler.cpp src/core/CompSlip.cpp src/core/SamplePlayHandle.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r2/logs/fix.patch` | 0 | `fix.patch` contains production paths only; tests and allocation seam remained present. |
| Build the two regressions with production changes reversed | `cmake --build build --target AudioResamplerRatioTest PatmanLoadTest -j4` | 0 | `build-15.log` |
| RED with production hunks reversed | `ctest -R '^(AudioResamplerRatioTest|PatmanLoadTest)$' --output-on-failure` | 8 | `test-12.log`: SampleTrack/comp-slip observed 2 allocations, Patman observed 1; both expected 0. Existing output/input ratio and mismatch-rate tests passed in the same reversed run. |
| Restore production hunks | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r2/logs/fix.patch` | 0 | Restored the exact patch used for the reversal. |
| Rebuild the app, plugin modules, and nine focused test targets | `cmake --build build --target zene AudioResamplerRatioTest PluginPortsMigrationTest AudioFileProcessorPlayheadTest AudioFileProcessorWaveViewTest PatmanLoadTest SlicerTPlayheadTest SlicerTWaveformTest SlicerTZoomTest TapTempoTest slicert patman -j4` | 0 | `build-16.log` |
| GREEN for both creation-path regressions | `ctest -R '^(AudioResamplerRatioTest|PatmanLoadTest)$' --output-on-failure` | 0 | `test-13.log`: 2/2 passed. |
| Focused test family | `ctest -R '^(AudioResamplerRatioTest|PluginPortsMigrationTest|AudioFileProcessorPlayheadTest|AudioFileProcessorWaveViewTest|PatmanLoadTest|SlicerTPlayheadTest|SlicerTWaveformTest|SlicerTZoomTest|TapTempoTest)$' --output-on-failure` | 0 | `test-14.log`: 9/9 passed. |
| Feature gate after report was written | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5284-port-port` | 0 | `gate-4.log`: all eight checks passed; complexity and file-length reported no new item. |

The new tests are `AudioResamplerRatioTest::sampleTrackVoiceUsesPreparedConverterOnRenderPath` (`tests/src/core/AudioResamplerRatioTest.cpp:461-479`) and `PatmanLoadTest::patmanVoiceUsesPreparedConverterOnRenderPath` (`tests/src/plugins/PatmanLoadTest.cpp:192-220`). Both set `SincBest`, enter `RealtimeScope`, and assert the `LMMS_TESTING` converter-allocation counter is zero. The sample case creates a slipped comp segment, so the same assertion covers the primary sample state and comp-slip reader state.

## Self-check

1. **Fails with the production fix reversed and passes with it restored:** `test-12.log` records the production-only reversal and CTest exit 8, with actual allocation counts of 2 for the sample/comp-slip path and 1 for Patman. `test-13.log` records the same two tests passing 2/2 after `git apply` restored the patch. The earlier ratio regression remains in `AudioResamplerRatioTest`; its output/input and mismatch-rate tests pass in the reversed run.
2. **Deterministic:** the tests use `RealtimeScope` plus the `LMMS_TESTING` counter around `createConverterState()` (`src/core/AudioResampler.cpp:84-92`). They inject no timestamps, sleep, or race. The sample test invokes the real clip-handle constructor and comp-slip setup; the Patman test invokes `instrument.playNote()` under the scope.
3. **Cause and sibling paths checked:** SampleTrack clip handles, their comp-slip readers, and Patman note states are tagged. SlicerT’s retained playback slots and AudioFileProcessor’s sampler voice pool are already constructed off-thread and remain on their existing prepare/adopt path. Preview, metronome, and file-browser `SamplePlayHandle(Sample*)` callers retain the ordinary constructor; they are not the render-created SampleClip path.
4. **No collateral data or limit change:** no project/file format or saved setting changed. The selected `SrcQuality` still determines the converter; new states claim a slot in that mode. The reserve grows with play-handle capacity and comp-reader headroom instead of rejecting voices at a new fixed cap.
5. **Realtime, undo, and commands:** tagged RT construction only claims/releases an existing slot with atomics; `src_new` and `src_delete` for converter changes happen during control-thread prepare/adopt work. No new control command was added. `export.set_src_quality` still publishes only after converter preparation succeeds and records its existing undo step on success (`src/core/ControlCommandsExport.cpp:243-251`, `src/core/AudioEngine.cpp:275-289`).
6. **Verdict-only prompt:** not applicable; the verdict is FIXED and the REAL mechanism is evidenced above.
7. **Sibling export commands checked:** `export.get_settings` is read-only; `export.preset_add`, `export.preset_apply`, `export.preset_list`, and `export.preset_remove` only store/apply/list/remove sample rate, bit depth, and stereo-mode presets (`src/core/ControlCommandsExportPresets.cpp:28-31, 161-170, 221-229, 328-336, 414-425`). `export.set_dither` changes dither only, and `export.set_loudness_report` changes the report flag only (`src/core/ControlCommandsExport.cpp:118-197, 273-319`). `export.set_src_quality` is the only sibling that changes converter mode; it uses the existing guarded quality transaction described above.

## Files committed for this cluster

- Test-seam commit `fba4e6ad1`: `include/AudioResampler.h`, `src/core/AudioResampler.cpp`, `tests/src/core/AudioResamplerRatioTest.cpp`, `tests/src/plugins/PatmanLoadTest.cpp`, `tests/upstream-modifications.txt`.
- Comp-slip regression commit `8958b5157`: `tests/src/core/AudioResamplerRatioTest.cpp`.
- Fix/report commit: `include/AudioResampler.h`, `include/Sample.h`, `include/SamplePlayHandle.h`, `plugins/Patman/Patman.cpp`, `src/core/AudioEngine.cpp`, `src/core/AudioResampler.cpp`, `src/core/CompSlip.cpp`, `src/core/SamplePlayHandle.cpp`, `tests/upstream-modifications.txt`, and this report.
