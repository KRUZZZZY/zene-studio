# cluster new-p10-359

Verdict: **BLOCKED**

The finding is real. `SaProcessor::analyze()` writes the normalized FFT spectra while holding `m_dataAccess` (`plugins/SpectrumAnalyzer/SaProcessor.cpp:127`). `SaSpectrumView::refreshPaths()` previously held only `m_reallocationAccess` (`plugins/SpectrumAnalyzer/SaSpectrumView.cpp:235`) while reading both channels at lines 255-256. It now holds `m_dataAccess` around both `updateBuffers()` calls (`SaSpectrumView.cpp:255-259`) and releases it before path construction. The lock order remains reallocation access, then data access.

`SaSpectrumViewTest::spectrumReadsUseDataAccessLock` (`tests/src/core/SaSpectrumViewTest.cpp:22-45`) checks that both spectrum reads are inside that lock scope. It failed before the production change and failed again after applying the reverse patch; it passed after restoring the fix.

The code and regression are verified, but the required gates block a clean completion. `complexity-gate.sh --check` reports six unrelated over-CCN functions in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`. `file-length-gate.sh --check` reports fourteen unrelated files over 500 lines as new-file regressions. Neither gate reports a regression in this change. The baselines were not changed.

## Evidence

The affected ctest family contains one test (`SaSpectrumView`, ctest #470).

| Proof | Exact command | Exit |
|---|---|---:|
| Initial RED | `ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 |
| Initial GREEN | `ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| Reverse patch | `git diff -- plugins/SpectrumAnalyzer/SaSpectrumView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| Rebuilt without fix | `cmake --build build --target zene analyzer SaSpectrumViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| Reverse-patch RED | `ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| Rebuilt with fix | `cmake --build build --target zene analyzer SaSpectrumViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| Reverse-proof GREEN | `ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 1 |

Build commands before the reverse-patch proof also completed successfully:

- `cmake --build build --target SaSpectrumViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — exit 0.
- `cmake --build build --target zene SaSpectrumViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — exit 0.
- `cmake --build build --target zene analyzer SaSpectrumViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — exit 0.
- `ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-359.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — exit 0.

## Files in this change

- `plugins/SpectrumAnalyzer/SaSpectrumView.cpp`
- `tests/src/core/SaSpectrumViewTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-359.md`
