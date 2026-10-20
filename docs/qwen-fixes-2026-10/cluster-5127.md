5127: ALREADY-FIXED

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
