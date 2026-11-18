# Cluster new-p10-142: DrumSynth level squaring

**Verdict: FIXED**

`DrumSynth::GetDSFileSamples()` squared each section's parsed `int` level before converting the product to `float`. A `.ds` tone with `Level=50000` therefore overflowed the signed multiplication and inverted the rendered sample. Each of the five section-level products now multiplies in `std::int64_t` before conversion. This preserves the prior exact integer-square result wherever the old multiplication was defined. No level clamp was added: existing DrumSynth presets use values above 128.

## File and line evidence

| File | Evidence |
| --- | --- |
| `src/core/DrumSynth.cpp:386,406,435,480,493` | Noise, tone, overtones, and both noise bands widen the level before squaring. |
| `tests/src/core/DrumSynthLevelTest.cpp:21-47` | Writes a real `.ds` fixture with `Tone/Level=50000`, renders it through `GetDSFileSamples()`, and requires the first sample to be +32700. |
| `tests/CMakeLists.txt:325` | Registers the regression test as `DrumSynthLevelTest`. |

The original test failed with actual −32700 versus expected +32700. Reversing only the production patch reproduced the same failure; reapplying it passed.

## Verification evidence

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs`.

| Check | Exact command | Exit |
| --- | --- | ---: |
| Initial build attempt (target absent from the old generated makefiles) | `cmake --build build --target zene DrumSynthLevelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | 2 |
| Build test target before original-code RED | `cmake --build build --target zene DrumSynthLevelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Original-code RED | `cd build/tests && ctest -R DrumSynthLevelTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | 8 |
| Build fixed code | `cmake --build build --target zene DrumSynthLevelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Fixed-code GREEN | `cd build/tests && ctest -R DrumSynthLevelTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Save and reverse only the product patch | `git diff -- src/core/DrumSynth.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Build with production fix reversed | `cmake --build build --target zene DrumSynthLevelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Reversed-patch RED proof | `cd build/tests && ctest -R DrumSynthLevelTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Rebuild after reapplying | `cmake --build build --target zene DrumSynthLevelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Reapplied-patch GREEN proof | `cd build/tests && ctest -R DrumSynthLevelTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-142.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |

The affected CTest family contains one test; every invocation ran **1/1**. The regression failed both before the fix and with the product patch reversed, then passed with the fix applied.

## Files committed

- `src/core/DrumSynth.cpp`
- `tests/src/core/DrumSynthLevelTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-142.md`
