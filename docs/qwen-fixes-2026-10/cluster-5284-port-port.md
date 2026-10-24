FIXED

# Cluster 5284-port-port — SRC mode changes on the audio thread

## Decision and evidence

Verdict: REAL. On the port base 09ce5f1c8, src/core/Sample.cpp:116-121 compared the playback state's mode with ExportRenderSettings::srcQuality() and called AudioResampler::setMode() from Sample::play(). AudioResampler::setMode() calls src_new() at src/core/AudioResampler.cpp:143, which creates converter state and can allocate. Sample::play() is the sample playback path.

The manual port implementation is commit 5e1379547, directly on top of 09ce5f1c8. The verification below was rerun on this branch.

The fix removes mode selection from Sample::play(); current src/core/Sample.cpp:100-108 only sets the existing resampler's ratio before processing. Sample::PlaybackState now defaults to the selected mode at construction (include/Sample.h:51-55). A quality change prepares every retained converter under AudioEngine::requestChangesGuard(), publishes only after all preparations succeed, then adopts them (src/core/AudioEngine.cpp:264-279). Failure discards prepared states and leaves the published setting unchanged. The walk covers engine voices, song instruments and their retained pools, sample handles and comp-slip readers, and note states.

The existing export.set_src_quality command only records its undo step after propagation succeeds (src/core/ControlCommandsExport.cpp:242-251). ProjectRenderer and the CLI handle a failed selection. No command was added.

The legacy AudioFileProcessor reference fixture now constructs its playback state with the selected quality (tests/reference/AudioFileProcessor/AudioFileProcessor.cpp:132-136). Its old play path relied on Sample::play() replacing the model-selected converter. This keeps the sample-exact migration comparison aligned with the behavior under test.

## Regression and verification

AudioResamplerRatioTest::thePlayPathDoesNotReCreateTheConverter renders fixed sine input through a Linear state while selecting Linear and SincBest, with a SincBest control proving the fixture distinguishes converter output. With the product patch reversed, the pinned-state comparison fails at tests/src/core/AudioResamplerRatioTest.cpp:415; with the patch restored, it passes. The test uses no clock, sleep, or race.

The product-only patch was generated from the port diff and reversed for RED:

git diff 09ce5f1c8..HEAD -- include/AudioEngine.h include/AudioResampler.h include/CompSlip.h include/ExportRenderSettings.h include/Instrument.h include/Sample.h include/SamplePlayHandle.h include/Song.h plugins/AudioFileProcessor/AudioFileProcessor.cpp plugins/AudioFileProcessor/AudioFileProcessor.h plugins/AudioFileProcessor/SamplerVoicePool.h plugins/Patman/Patman.cpp plugins/Patman/Patman.h plugins/SlicerT/SlicerT.cpp plugins/SlicerT/SlicerT.h src/core/AudioEngine.cpp src/core/AudioResampler.cpp src/core/CompSlip.cpp src/core/ControlCommandsExport.cpp src/core/ExportRenderSettings.cpp src/core/ProjectRenderer.cpp src/core/Sample.cpp src/core/SamplePlayHandle.cpp src/core/Song.cpp src/core/main.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/fix.patch && git status --short
Exit 0.

| Check | Command (CTest runs from build/tests) | Exit | Result |
|---|---|---:|---|
| RED build | cmake --build build --target zene AudioResamplerRatioTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/build-9.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" | 0 | Rebuilt zene and the regression target with the product patch reversed. |
| RED test | ctest -R '^AudioResamplerRatioTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/test-12.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" | 8 | The new comparison failed; 15 passed and 1 failed. |
| Restore patch | git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/fix.patch && git status --short --branch | 0 | Product patch restored; worktree clean before report update. |
| GREEN build | cmake --build build --target zene AudioResamplerRatioTest PluginPortsMigrationTest decimator_1202 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/build-10.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" | 0 | All requested targets and the optional migration fixture built. |
| GREEN regression | ctest -R '^AudioResamplerRatioTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/test-13.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" | 0 | 1/1 CTest target passed. |
| Focused family | ctest -R '^(AudioResamplerRatioTest|PluginPortsMigrationTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/test-14.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" | 0 | 2/2 CTest targets passed. |
| Case totals | ctest -R '^(AudioResamplerRatioTest|PluginPortsMigrationTest)$' -V --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/test-15.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" | 0 | AudioResamplerRatioTest: 16 passed. PluginPortsMigrationTest: 73 passed, 0 failed, 2 skipped; migratedPluginsPreserveBehaviour passed. |

PluginPortsMigrationTest's two skips are SF2Player cases unavailable in this build. Its migration comparison reports SF2Player and PeakControllerEffect as unproven because their optional fixtures cannot be loaded/built here; these are reported gaps, not failures. The test target needs decimator_1202 built explicitly because CMake does not make it a dependency.

The train base's tests/all-sources.txt included 1,148 vendored sfizz paths excluded by its own published recipe. The manifest was regenerated from that recipe.

## Gates

Command: python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5284-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/gate-10.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"

Exit 0; all eight rows passed:

| Gate | Result |
|---|---|
| fork-sources | PASS, exit 0 |
| all-sources-reproduce | PASS, exit 0 |
| no-upstream-regression | PASS, exit 0 |
| check-namespace | PASS, exit 0 |
| no-tautology | PASS, exit 0 |
| doc-figures | PASS, exit 0 |
| complexity | PASS, no new item in changed files |
| file-length | PASS, no new item in changed files |

The completion check command was run after commit 86a0d36c1: python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5284-port-port 5284-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5284-port-port.r1/logs/submit-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc". Exit 0; RESULT: PASS - submit.

## Registration

No new product or test source file was added. The regression remains in the registered tests/src/core/AudioResamplerRatioTest.cpp. This new report is registered in tests/fork-sources.txt; inherited changes are recorded in tests/upstream-modifications.txt; Sample::play() is recorded in tests/rt-safety-scope.txt.

## Self-check

- Regression proof: with product changes reversed, build-9 exited 0 and test-12 exited 8 (15 passed, the new mode comparison failed at AudioResamplerRatioTest.cpp:415). With the patch restored, build-10 exited 0; test-13 and test-14 exited 0. Verbose test-15 exited 0: AudioResamplerRatioTest 16 passed; PluginPortsMigrationTest 73 passed, 0 failed, 2 skipped, including a passing migratedPluginsPreserveBehaviour comparison.
- Determinism: the test injects a fixed 8192-frame sine at 2000 Hz, source rate equal to half the engine output rate, and renders 2048 frames. It compares explicit Linear-state outputs under Linear and SincBest selections; a SincBest-state render is the sensitivity control. No wall clock, sleep, or race is used.
- Cause and complete caller audit: Sample::play() no longer reads SRC quality or changes converter mode (src/core/Sample.cpp:100-108); setMode()'s src_new() is at src/core/AudioResampler.cpp:136-148. Its direct playback callers are SamplePlayHandle::play (src/core/SamplePlayHandle.cpp:573), CompSlip rendering (src/core/CompSlip.cpp:133), AudioFileProcessor (plugins/AudioFileProcessor/AudioFileProcessor.cpp:210), Patman (plugins/Patman/Patman.cpp:226), and SlicerT (plugins/SlicerT/SlicerT.cpp:179). AudioEngine's retained-state walk (src/core/AudioEngine.cpp:283-303) reaches the engine pool, song instruments, live handles, and note states; each plugin/comp-slip state owner prepares, adopts, or discards with the transaction. The global setting callers are ControlCommandsExport, ProjectRenderer, and the CLI; ExportRenderSettings::reset is used only by tests. OutputSettings::setSrcQuality stores a render option and does not publish the live global selection. Each caller handles or deliberately ignores failure according to its role; no new command was added.
- No collateral damage: no project serialization, saved data, limits, or checks changed. Linear remains the default. The migration regression's sample-exact comparison passes; the legacy reference fixture initializes its converter from the selected quality to preserve the old play-path result.
- Verdict: REAL, evidenced by the port-base Sample.cpp:116-121 comparison and AudioResampler.cpp src_new() call at line 143 described above; this is not an ALREADY-FIXED or FALSE-POSITIVE case.
- Sibling commands: export.get_settings only reads; export.preset_add, export.preset_apply, export.preset_list, and export.preset_remove operate on stored presets; export.set_dither and export.set_loudness_report change separate settings. None changes live SRC quality. export.set_src_quality uses the transactional setter and records one undo step only after success.

## Files in the port

Product: include/AudioEngine.h, include/AudioResampler.h, include/CompSlip.h, include/ExportRenderSettings.h, include/Instrument.h, include/Sample.h, include/SamplePlayHandle.h, include/Song.h, plugins/AudioFileProcessor/AudioFileProcessor.cpp, plugins/AudioFileProcessor/AudioFileProcessor.h, plugins/AudioFileProcessor/SamplerVoicePool.h, plugins/Patman/Patman.cpp, plugins/Patman/Patman.h, plugins/SlicerT/SlicerT.cpp, plugins/SlicerT/SlicerT.h, src/core/AudioEngine.cpp, src/core/AudioResampler.cpp, src/core/CompSlip.cpp, src/core/ControlCommandsExport.cpp, src/core/ExportRenderSettings.cpp, src/core/ProjectRenderer.cpp, src/core/Sample.cpp, src/core/SamplePlayHandle.cpp, src/core/Song.cpp, src/core/main.cpp.

Tests and registration: tests/all-sources.txt, tests/fork-sources.txt, tests/reference/AudioFileProcessor/AudioFileProcessor.cpp, tests/rt-safety-scope.txt, tests/src/core/AudioResamplerRatioTest.cpp, tests/upstream-modifications.txt. Report: docs/qwen-fixes-2026-10/cluster-5284-port-port.md.
