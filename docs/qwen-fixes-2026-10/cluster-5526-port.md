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
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5526-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/gate-7.log 2>&1; echo EXIT=$?` | 0; all eight checks passed |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5526-port 5526-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5526-port.r1/logs/gate-6.log 2>&1; echo EXIT=$?` | 0 |

## Self-check

1. **Regression fails without the fix and passes with it:** Yes. `test-1.log` exits 8 on the original fields. After `git apply -R`, `build-3.log` exits 0 and `test-4.log` exits 8 on the same atomic-storage assertion. After reapplying the patch, `build-4.log` exits 0 and `test-5.log` exits 0. The affected family in `test-3.log` exits 0 with 3/3 tests passed.
2. **Deterministic test:** Yes. `AutomationClipRampSettingsTestAccess::usesLockFreeAtomicStorage()` checks the private field types and lock-free properties, while the test exercises mode resolution sequentially (`tests/src/core/AutomationThinTest.cpp:27-39,76-94`). It uses no wall-clock timing, sleeps, or concurrent accesses.
3. **Cause and sibling paths checked:** Yes. The audio caller is `src/core/Song.cpp:1482`. All direct field uses were checked: getters and `sampleAccurate()` (`include/AutomationClip.h:91-94,181,211-218`), fields (`:344,350`), copy, setter stores, curve evaluation, and serialization (`src/core/AutomationClip.cpp:83-85,173,670,755,940-943`). Both setters retain `m_clipMutex` (`src/core/AutomationClip.cpp:167,754`). Other product consumers `midiimport` and `ladspaeffect`, `zene`, and all 20 direct-include test targets were rebuilt successfully.
4. **No collateral damage:** Yes. Enum values and defaults are unchanged. `saveSettings()` still writes only explicit settings as the same `sample_accurate` values (`src/core/AutomationClip.cpp:940-944`), and `loadSettings()` still maps the same attribute or absence to On/Off/Default (`:1001-1002`). No limit, command, or undo path was added.
5. **ALREADY-FIXED/FALSE-POSITIVE evidence:** Not applicable; the verdict is REAL, supported by the unlocked audio reader at `include/AutomationClip.h:211-218` and mutex-guarded setters at `src/core/AutomationClip.cpp:167,754`.
6. `AutomationThinTest.cpp` was already registered in `tests/fork-sources.txt:2278`. The inherited header and source changes are recorded in `tests/upstream-modifications.txt:2765-2766` with cluster 5526-port reasons.
7. The explicit commit files are `include/AutomationClip.h`, `src/core/AutomationClip.cpp`, `tests/src/core/AutomationThinTest.cpp`, `tests/upstream-modifications.txt`, and `docs/qwen-fixes-2026-10/cluster-5526-port.md`.
