FIXED

# Cluster 5284-port-port — SRC mode changes on the audio thread

## Decision and evidence

Verdict: REAL. On the fresh branch base 09ce5f1c8af68deaae315ed1fa3b1a480f77932d, src/core/Sample.cpp:116-121 read the selected SRC quality and called setMode() from Sample::play(). src/core/AudioResampler.cpp:118 called src_new() to create the selected converter, which can allocate. Sample::play() is the playback path used by sample handles.

The fix removes mode selection from Sample::play() (current src/core/Sample.cpp:100 onward). PlaybackState now starts with the selected mode in include/Sample.h:51. Quality changes prepare converters for retained playback states on the control side while AudioEngine::requestChangesGuard() excludes rendering, publish the selection, and adopt prepared converters together (src/core/AudioEngine.cpp:264-279). If preparation fails, prepared states are discarded and the selection is not published. The propagation covers the engine voice pool, song instruments and their AudioFileProcessor/SlicerT pools, live sample handles and comp-slip readers, and note playback states.

The existing export.set_src_quality command reports allocation failure before adding its undo step (src/core/ControlCommandsExport.cpp:243-251). ProjectRenderer and the CLI also handle a failed quality change. No control command was added.

The AudioFileProcessor reference fixture now initializes its legacy playback state with the selected export quality. Previously it relied on Sample::play() replacing the fixture's interpolation mode. This preserves the fixture's prior selected-quality output after that audio-thread switch is removed. PluginPortsMigrationTest confirms the migrated plugin render remains sample-exact.

## Regression and verification

The deterministic regression is AudioResamplerRatioTest::thePlayPathDoesNotReCreateTheConverter. It renders the same sine input with a Linear playback state while selecting Linear and SincBest, then compares both outputs. A SincBest control render proves the fixture distinguishes the converters. With the old Sample::play() path restored, the selected SincBest output differs at the first sample (0.002512895 versus Linear 0). With the fix, the pinned state remains Linear and the comparison passes.

The RED proof used the product-only patch at /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/fix.patch. It was reversed, zene and the regression target were rebuilt, and the regression failed. The patch was restored, affected targets rebuilt, and the regression passed.

| Check | Command (CTest commands run from build/tests) | Exit | Result |
|---|---|---:|---|
| Reverse product patch | git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/fix.patch; echo EXIT=$? | 0 | Product changes reversed for RED proof. |
| RED build | cmake --build build --target zene AudioResamplerRatioTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/build-5.log 2>&1; echo EXIT=$? | 0 | Rebuilt zene and regression target without product changes. |
| RED test | ctest -R '^AudioResamplerRatioTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/test-6.log 2>&1; echo EXIT=$? | 8 | Regression failed as expected: 0.002512895 versus 0. |
| Restore product patch | git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/fix.patch; echo EXIT=$? | 0 | Restored product changes. |
| GREEN build | cmake --build build --target zene AudioResamplerRatioTest PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/build-6.log 2>&1; echo EXIT=$? | 0 | Rebuilt zene and both affected test targets. |
| GREEN regression | ctest -R '^AudioResamplerRatioTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/test-7.log 2>&1; echo EXIT=$? | 0 | 1/1 CTest target passed. |
| Regression case count | ctest -R '^AudioResamplerRatioTest$' -V --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/test-10.log 2>&1; echo EXIT=$? | 0 | 16 passed, 0 failed, 0 skipped. |
| Migration fixture build | cmake --build build --target decimator_1202 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/build-7.log 2>&1; echo EXIT=$? | 0 | Built the optional fixture required by PluginPortsMigrationTest. |
| Migration test | ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/test-9.log 2>&1; echo EXIT=$? | 0 | 1/1 CTest target passed; migratedPluginsPreserveBehaviour passed. |
| Migration case count | ctest -R '^PluginPortsMigrationTest$' -V --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/test-11.log 2>&1; echo EXIT=$? | 0 | 73 passed, 0 failed, 2 skipped. |
| Final feature gate | python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5284-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/gate-5.log 2>&1; echo EXIT=$? | 0 | Every gate row passed. |

The migration target needs decimator_1202 built explicitly because the configured test target does not depend on it. Its two skipped cases are reported as unproven by the test harness because optional external fixtures are unavailable; the suite has no failures.

The first scope-gate run found the fresh base's tests/all-sources.txt listed 1,148 vendored plugins/SfzPlayer/sfizz paths that its published recipe excludes. I regenerated the entry list from that recipe verbatim, removing only those excluded vendor paths. The final feature gate passes fork-sources and all-sources-reproduce. tests/all-sources.txt is registered in tests/upstream-modifications.txt.

## Registration

No new product or test source file was added. The regression is in the already registered tests/src/core/AudioResamplerRatioTest.cpp. The new report is registered in tests/fork-sources.txt. Changed inherited files are recorded in tests/upstream-modifications.txt. Sample::play() is recorded in tests/rt-safety-scope.txt.

## Self-check

- Regression proof: the product-only patch was reversed (exit 0), rebuilt with zene (build-5 exit 0), and AudioResamplerRatioTest failed (test-6 exit 8). The patch was restored (exit 0), rebuilt (build-6 exit 0), and AudioResamplerRatioTest passed (test-7 exit 0; 16 passed).
- Determinism and coverage: the regression uses fixed sine samples, explicit converter modes and selected qualities, and compares generated samples; it uses no sleeps, timestamps, or races. PluginPortsMigrationTest passes its exact output comparison (test-9 and test-11 exit 0; 73 passed and 2 skipped).
- Undo and sibling commands: export.set_src_quality adds one undo step only after propagation succeeds. No command was added. export.get_settings is read-only; export.set_dither and export.set_loudness_report change separate settings; export.preset_list/add/apply/remove operate on stored output presets. These siblings do not change the live SRC converter. The CLI and ProjectRenderer callers handle propagation failure.
- Realtime: Sample::play() only uses its already-owned resampler and playback buffers. Converter creation, preparation, disposal and adoption occur during control-side propagation while the render guard excludes audio processing; no converter mode change remains on the playback path.
- Compatibility: no project serialization or saved-data format changed. The default converter remains Linear. PluginPortsMigrationTest confirms migrated plugin output is unchanged; the source-quality selection remains an export setting.
- Portability: the changes use C++20 facilities already used by this project and add no Qt-version-specific API, Windows macro, or std::filesystem dependency.
- Gates: feature_check.py passed with exit 0 (gate-5.log). The submit_check.py result will be recorded after the required commit and completion check.

## Files in the port

Product files: include/AudioEngine.h, include/AudioResampler.h, include/CompSlip.h, include/ExportRenderSettings.h, include/Instrument.h, include/Sample.h, include/SamplePlayHandle.h, include/Song.h, plugins/AudioFileProcessor/AudioFileProcessor.cpp, plugins/AudioFileProcessor/AudioFileProcessor.h, plugins/AudioFileProcessor/SamplerVoicePool.h, plugins/Patman/Patman.cpp, plugins/Patman/Patman.h, plugins/SlicerT/SlicerT.cpp, plugins/SlicerT/SlicerT.h, src/core/AudioEngine.cpp, src/core/AudioResampler.cpp, src/core/CompSlip.cpp, src/core/ControlCommandsExport.cpp, src/core/ExportRenderSettings.cpp, src/core/ProjectRenderer.cpp, src/core/Sample.cpp, src/core/SamplePlayHandle.cpp, src/core/Song.cpp, and src/core/main.cpp.

Test, reference and registration files: tests/all-sources.txt, tests/fork-sources.txt, tests/reference/AudioFileProcessor/AudioFileProcessor.cpp, tests/rt-safety-scope.txt, tests/src/core/AudioResamplerRatioTest.cpp, and tests/upstream-modifications.txt. Report: docs/qwen-fixes-2026-10/cluster-5284-port-port.md.
