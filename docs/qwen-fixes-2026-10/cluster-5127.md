ALREADY-FIXED
5127: ALREADY-FIXED

All referenced logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5127.r1/logs/`.

Evidence: `src/core/MetronomeOptions.cpp:169-170` schedules the prepared click buffer through `AudioEngine::acquirePooledSampleVoice`; it does not allocate a `Sample` or `SamplePlayHandle` per click. This was introduced by existing commit `e88364fcc` (cluster 5127/5414). The new deterministic regression in `tests/src/core/RecordToolsTest.cpp:122-131` checks that `click()` returns a pooled `SamplePlayHandle`; the existing `tests/src/core/FrozenTakeHandoffTest.cpp:280-322` also compares audio and asserts zero allocations during click scheduling and rendering.

RED proof: Temporarily restored the old per-click `new Sample` / `new SamplePlayHandle` return path. `metronomeClickUsesPreparedVoice()` failed because `isPooledVoice()` was false. Separately, temporarily injected `new Sample(buffer)` into the pooled path; the existing allocation probe failed with `test::tlAllocationCount == 1`.

| Stage | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene RecordToolsTest` | 0 | `logs/25-click-pool-red-build.log` |
| RED test | `ctest -R '^RecordToolsTest$' --output-on-failure` (cwd `build/tests`) | 8 | `logs/26-click-pool-red-test.log`; `isPooledVoice()` assertion failed, 20 passed and 1 failed |
| Allocation probe RED | `ctest -R '^FrozenTakeHandoffTest$' --output-on-failure` (cwd `build/tests`) | 8 | `logs/07-click-red-injection-test.log`; allocation count was 1 |
| GREEN build | `cmake --build build --target zene RecordToolsTest FrozenTakeHandoffTest` | 0 | `logs/27-final-build.log` |
| GREEN test | `ctest -R '^RecordToolsTest$' --output-on-failure` (cwd `build/tests`) | 0 | `logs/28-recordtools-final-test.log`; 1/1 CTest test passed |
| Allocation probe GREEN | `ctest -R '^FrozenTakeHandoffTest$' --output-on-failure` (cwd `build/tests`) | 0 | `logs/29-frozen-final-test.log`; 1/1 CTest test passed |

The temporary old path and allocation injection were removed. No product change was needed for this cluster. Final feature check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5127` — exit 0 (`logs/32-final-feature-check.log`).

Files committed for this status: `tests/src/core/RecordToolsTest.cpp` and `docs/qwen-fixes-2026-10/cluster-5127.md`; the product fix was already present in `e88364fcc`.

new-p16-111: FIXED

Evidence: `src/core/MetronomeOptions.cpp:92-94` now checks `QString::toFloat(&validLevel)` and passes `1.f` to the existing `boundedLevel` when parsing fails. Before the fix, an invalid string converted to 0 and `boundedLevel(0)` accepted the muted level. The regression in `tests/src/core/RecordToolsTest.cpp:100-120` writes `level=invalid` before metronome registration loads options, then checks that the loaded level is 1; it restores the original stored setting afterward.

| Stage | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene RecordToolsTest` | 0 | `logs/01-recordtools-red-build.log` |
| RED test | `ctest -R '^RecordToolsTest$' --output-on-failure` (cwd `build/tests`) | 8 | `logs/02-recordtools-red-test.log`; invalid-level assertion failed with the original code |
| 4b reverse-patch build | `git diff -- src/core/MetronomeOptions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5127.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5127.r1/logs/fix.patch && cmake --build build --target zene RecordToolsTest` | 0 | `logs/08-config-red-proof-build.log` |
| 4b reverse-patch test | `ctest -R '^RecordToolsTest$' --output-on-failure` (cwd `build/tests`) | 8 | `logs/09-config-red-proof-test.log`; invalid-level assertion failed again |
| GREEN build after `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5127.r1/logs/fix.patch` | `cmake --build build --target zene RecordToolsTest FrozenTakeHandoffTest` | 0 | `logs/27-final-build.log` |
| GREEN test | `ctest -R '^RecordToolsTest$' --output-on-failure` (cwd `build/tests`) | 0 | `logs/28-recordtools-final-test.log`; 1/1 CTest test passed |

Affected family count: 1/1 CTest executable (`RecordToolsTest`). The existing metronome options and undo assertions pass in the same run. Files committed for this fix: `src/core/MetronomeOptions.cpp`, `tests/src/core/RecordToolsTest.cpp`, and this report.

## Self-check

- Regression proof: `metronomeClickUsesPreparedVoice()` failed under the temporary old allocating path (`ctest -R '^RecordToolsTest$' --output-on-failure`, exit 8; `logs/26-click-pool-red-test.log`) and passes with the pooled path (`logs/28-recordtools-final-test.log`, exit 0). The separate allocation probe also observed one allocation under injection and passed after restoration (`logs/07-click-red-injection-test.log`, exit 8; `logs/29-frozen-final-test.log`, exit 0). For the config bug, the initial invalid-value test failed (`logs/02-recordtools-red-test.log`, exit 8), failed again with the production patch reversed (`logs/09-config-red-proof-test.log`, exit 8), and passed after reapplying the fix (`logs/28-recordtools-final-test.log`, exit 0).
- Determinism: `metronomeClickUsesPreparedVoice()` synchronously checks the returned handle's pool marker; `invalidStoredLevelUsesDefault()` uses the injected literal `invalid` before the one-time options load and compares the captured level. These regression assertions use no sleeps, timestamps, or races.
- Cause and sibling paths: `Metronome.cpp:39-42` is the scheduler caller of `click()`; the click path now already acquires from the prepared pool at `MetronomeOptions.cpp:169-170`. Stored config is parsed in `MetronomeOptions.cpp:92-94`; live command and undo changes continue through `setOptions()` and the unchanged `boundedLevel()` path at `MetronomeOptions.cpp:127-136`. Options are loaded during control-thread registration at `ProjectSectionRegistry.cpp:70` and `ControlCommandsRecordTools.cpp:260`.
- Collateral behavior: valid numeric config values still flow through `boundedLevel()` with the existing 0..2 clamp; NaN still defaults to 1. `setOptions()` and undo behavior are unchanged. The regression restores the raw config setting it observed before injecting `invalid`.
- Already-fixed evidence: `MetronomeOptions.cpp:169-170` returns the preallocated pooled handle, and existing commit `e88364fcc` introduced that path. The new pooled-handle assertion and the allocation-probe test both fail under the old allocation behavior.
- Gates: `feature_check.py` exit 0 (`logs/33-final-feature-check.log`); `submit_check.py` exit 0 (`logs/34-submit-check.log`).
- Files: this batch changes `src/core/MetronomeOptions.cpp`, `tests/src/core/RecordToolsTest.cpp`, and `docs/qwen-fixes-2026-10/cluster-5127.md`; no new product file was added.

| Gate | Command | Exit | Log |
| --- | --- | ---: | --- |
| Feature check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5127` | 0 | `logs/33-final-feature-check.log` |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5127 5127` | 0 | `logs/34-submit-check.log` |
