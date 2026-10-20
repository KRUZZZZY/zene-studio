5127: ALREADY-FIXED

All referenced logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5127.r1/logs/`.

Evidence: `src/core/MetronomeOptions.cpp:169-170` schedules the prepared click buffer through `AudioEngine::acquirePooledSampleVoice`; it does not allocate a `Sample` or `SamplePlayHandle` per click. This was introduced by existing commit `e88364fcc` (cluster 5127/5414). `tests/src/core/FrozenTakeHandoffTest.cpp:280-318` compares the click audio with a fresh handle and asserts zero allocations during scheduling and rendering.

RED proof: Temporarily injected `new Sample(buffer)` immediately before the pool acquisition. The existing regression then reported `test::tlAllocationCount == 1`.

| Stage | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene FrozenTakeHandoffTest` | 0 | `logs/06-click-red-injection-build.log` |
| RED test | `ctest -R '^FrozenTakeHandoffTest$' --output-on-failure` (cwd `build/tests`) | 8 | `logs/07-click-red-injection-test.log`; allocation count was 1, 8 passed and 1 failed |
| GREEN build | `cmake --build build --target zene RecordToolsTest FrozenTakeHandoffTest` | 0 | `logs/10-final-build.log` |
| GREEN test | `ctest -R '^FrozenTakeHandoffTest$' --output-on-failure` (cwd `build/tests`) | 0 | `logs/12-frozen-final-test.log`; 1/1 CTest test passed |

The temporary allocation was removed. No product change was needed for this cluster. Feature check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5127` — exit 0 (`logs/13-feature-check.log`).

Files committed for this status: `docs/qwen-fixes-2026-10/cluster-5127.md`.

new-p16-111: FIXED

Evidence: `src/core/MetronomeOptions.cpp:92-94` now checks `QString::toFloat(&validLevel)` and passes `1.f` to the existing `boundedLevel` when parsing fails. Before the fix, an invalid string converted to 0 and `boundedLevel(0)` accepted the muted level. The regression in `tests/src/core/RecordToolsTest.cpp:100-120` writes `level=invalid` before metronome registration loads options, then checks that the loaded level is 1; it restores the original stored setting afterward.

| Stage | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene RecordToolsTest` | 0 | `logs/01-recordtools-red-build.log` |
| RED test | `ctest -R '^RecordToolsTest$' --output-on-failure` (cwd `build/tests`) | 8 | `logs/02-recordtools-red-test.log`; invalid-level assertion failed with the original code |
| 4b reverse-patch build | `git diff -- src/core/MetronomeOptions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5127.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5127.r1/logs/fix.patch && cmake --build build --target zene RecordToolsTest` | 0 | `logs/08-config-red-proof-build.log` |
| 4b reverse-patch test | `ctest -R '^RecordToolsTest$' --output-on-failure` (cwd `build/tests`) | 8 | `logs/09-config-red-proof-test.log`; invalid-level assertion failed again |
| GREEN build after `git apply logs/fix.patch` | `cmake --build build --target zene RecordToolsTest FrozenTakeHandoffTest` | 0 | `logs/10-final-build.log` |
| GREEN test | `ctest -R '^RecordToolsTest$' --output-on-failure` (cwd `build/tests`) | 0 | `logs/11-recordtools-final-test.log`; 1/1 CTest test passed |

Affected family count: 1/1 CTest executable (`RecordToolsTest`). The existing metronome options and undo assertions pass in the same run. Files committed for this fix: `src/core/MetronomeOptions.cpp`, `tests/src/core/RecordToolsTest.cpp`, and this report.
