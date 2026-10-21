FIXED

# Cluster 5526-port — ramp-setting changes race with audio evaluation

## Decision and evidence

Verdict: REAL.

`Song::buildAutomationRamps()` calls `AutomationClip::sampleAccurate()` on the audio block path (`src/core/Song.cpp:1426-1435,1482`). Before the fix, `sampleAccurate()` read `m_rampSetting` and `m_progressionType` without synchronization, while `setProgressionType()` and `setRampSetting()` wrote them under `m_clipMutex` (`src/core/AutomationClip.cpp:164-175,752-756`). Those plain reads and writes were a C++ data race.

Both settings are now `std::atomic` and statically required to be always lock-free (`include/AutomationClip.h:69-70,207-208,344,350`). `sampleAccurate()` and the other getters use relaxed atomic loads; both setters preserve their existing clip-mutex guard and use relaxed stores (`include/AutomationClip.h:91-94,210-218`; `src/core/AutomationClip.cpp:167,173,754-755`). Copy construction, curve evaluation, and serialization also use explicit atomic operations. The explicit `sample_accurate` save attribute, default mode, and progression behavior are unchanged.

`AutomationThinTest::rampSettingsUseLockFreeAtomicStorage()` checks the actual member types and the lock-free guarantee, then verifies Default/On/Off resolution. It uses no concurrent test threads, sleeps, or wall-clock timing. It fails on the original plain fields and after reversing the production patch, then passes with the fix.

## Verification evidence

| Check | Exact command | Exit and result |
|---|---|---|
| RED build before the production fix | `cmake --build build --target zene AutomationThinTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED regression before the production fix | `ctest -R '^AutomationThinTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8; 1/1 failed at the atomic-storage assertion |
| Rebuild `zene` and every test target with a direct `AutomationClip.h` include | `cmake --build build --parallel 2 --target zene AutomationTrackTest W3EngineTest AutomationWriteToEndTest AutomationThinTest AutomationLanesTest AutomationRenderAllocationTest AutomationLaneChildrenTest UndoHistoryPersistenceTest AutomationModesTest ControlAutomationScriptTest ControlAutomationModesTest EngineRenderBenchmarkTest MixingRoutingTest SampleAccurateAutomationTest NoteCurvesAcrossTest AutomationEditorEditingTest AutomationClipDisconnectTest AutomationModeMenuTest AutomationLaneChildGuiTest ClapEffectIntegrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Rebuild the remaining product module consumers | `cmake --build build --target zene midiimport ladspaeffect > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| GREEN regression | `ctest -R '^AutomationThinTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0; 1/1 passed |
| Affected test family | `ctest -R '^(AutomationClipDisconnectTest|AutomationThinTest|AutomationWriteToEndTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 0; 3/3 passed |
| Save product patch for reversal | `git diff -- include/AutomationClip.h src/core/AutomationClip.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild without the fix | `cmake --build build --target zene AutomationThinTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Reversal regression | `ctest -R '^AutomationThinTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 8; 1/1 failed at the same atomic-storage assertion |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with the fix restored | `cmake --build build --target zene AutomationThinTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Reversal-proof GREEN regression | `ctest -R '^AutomationThinTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0; 1/1 passed |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5526-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0; all eight checks passed |
| Completion check | Pending commit and execution. | Pending |

## Self-check

- The audio path adds no mutex, allocation, wait, or logging; `sampleAccurate()` performs only always-lock-free atomic loads.
- Every direct access to both fields was checked. Progression getters, tangent checks, curve evaluation, copy construction, serialization, and setters now use explicit atomic loads or stores. Setters retain the pre-existing clip mutex.
- Saved project values are unchanged: the default remains `Default`, and serialization still writes the existing `sample_accurate` values only for an explicit setting. No command or undo behavior was added.
- `AutomationThinTest.cpp` was already registered in `tests/fork-sources.txt:2278`. The inherited header and source changes are recorded in `tests/upstream-modifications.txt:2765-2766` with cluster 5526-port reasons.
- The test uses sequential calls and compile-time type introspection; there are no timing assumptions or test races.
- The explicit commit files are `include/AutomationClip.h`, `src/core/AutomationClip.cpp`, `tests/src/core/AutomationThinTest.cpp`, `tests/upstream-modifications.txt`, and `docs/qwen-fixes-2026-10/cluster-5526-port.md`.
- The repository-wide include scan found two additional product consumers outside `src/`; their `midiimport` and `ladspaeffect` targets were rebuilt in addition to `zene` and the 20 test targets.
