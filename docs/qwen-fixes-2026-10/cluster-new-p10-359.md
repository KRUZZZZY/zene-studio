# cluster new-p10-359

Verdict: FIXED

The finding was real. SaSpectrumView::drawSpectrum() calls SaProcessor::spectrumNotEmpty() at plugins/SpectrumAnalyzer/SaSpectrumView.cpp:182. analyze() writes the normalized spectra under m_dataAccess at plugins/SpectrumAnalyzer/SaProcessor.cpp:127 and :181. spectrumNotEmpty() previously held only m_reallocationAccess while scanning those arrays. It now takes m_dataAccess second (SaProcessor.cpp:481-485), preserving the required reallocation-before-data lock order. The existing refreshPaths() reads remain protected by m_dataAccess (SaSpectrumView.cpp:256-258).

Added SaSpectrumViewTest::spectrumNotEmptyUsesDataAccessLock at tests/src/core/SaSpectrumViewTest.cpp:47-64. It verifies the reallocation lock is taken before the data lock and both spectrum scans occur under both locks. The test fails without the SaProcessor.cpp change and passes with it.

The affected CTest family ran 1 test: SaSpectrumView (#470). The focused RED run failed on the new assertion; the final GREEN run passed 1/1.

The fff grep tool was not available in this session, so I searched the logs with one-term rg -n matches.

## Evidence

ctest commands below ran from /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-359/build/tests. Build, patch, and gate commands ran from the worktree root.

| Proof | Exact command | Exit |
|---|---|---:|
| Build test before product change | cmake --build build --target SaSpectrumViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 0 |
| Initial RED | ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 8 |
| Build with fix | cmake --build build --target zene analyzer SaSpectrumViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 0 |
| Initial GREEN | ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 0 |
| Reverse product patch | git diff -- plugins/SpectrumAnalyzer/SaProcessor.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 0 |
| Rebuild without fix | cmake --build build --target zene analyzer SaSpectrumViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 0 |
| Reverse-patch RED | ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 8 |
| Restore product patch | git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 0 |
| Rebuild with fix restored | cmake --build build --target zene analyzer SaSpectrumViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 0 |
| Final GREEN | ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 0 |
| Complexity gate | bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 1 |
| File-length gate | bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r2/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0 | 1 |

The gates remain red on unrelated existing findings: the complexity gate reports six over-CCN functions in XyPadPairTest.cpp, DawProjectReadTracks.cpp, ControlCommandsMixerTools.cpp, ControlCommandsMixerStrip.cpp, ProjectRevisions.cpp, and ControlCommandsShortcuts.cpp. The file-length gate reports fourteen unrelated files over 500 lines. Neither gate identified a regression in this change; no baselines were changed.

## Files committed

- plugins/SpectrumAnalyzer/SaProcessor.cpp
- tests/src/core/SaSpectrumViewTest.cpp
- tests/upstream-modifications.txt
- docs/qwen-fixes-2026-10/cluster-new-p10-359.md
