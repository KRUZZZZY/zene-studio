# Cluster new-p10-143-port

Verdict: **FIXED**

Re-applied and refined the DrumSynth duration-arithmetic guard. `DrumSynth::LongestEnv()` now bounds the rounded envelope step count before the `int` conversion/multiplication, accepting a fractional step above `maxSteps` that truncates to `maxSteps` instead of rejecting it, and `checkedWaveSampleCount()` validates the channel-expanded sample count against the `long` write cursor and `size_t` allocation byte limit before allocating with `std::nothrow`. `LongestEnv()` is exposed for boundary regression coverage.

## File evidence

- `src/core/DrumSynth.cpp`: anonymous `checkedWaveSampleCount()` helper; `LongestEnv()` boundary condition `steps >= maxSteps + 1.0f`; allocation uses `std::nothrow` after validation.
- `include/DrumSynth.h`: `LongestEnv()` declared public for test seam.
- `tests/src/core/DrumSynthTest.cpp`: `refusesAnEnvelopeWhoseRoundedLengthOverflows()` uses a parser-safe `3e9` envelope and asserts refusal before allocation; `mapsAFractionalStepAboveMaxToTheSafeMaximumLength()` checks the fractional boundary maps to the safe maximum length without allocating.
- `tests/upstream-modifications.txt`: registered the inherited `src/core/DrumSynth.cpp` and `include/DrumSynth.h` changes.

## Evidence table

| Phase | Exact command | Exit |
|---|---|---:|
| Build with fix (`build-8.log`) | `cmake --build build --target zene DrumSynthTest > .../build-8.log 2>&1` | 0 |
| GREEN (`test-8.log`, 5/5) | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure > .../test-8.log 2>&1` | 0 |
| Reverse production diff (`fix.patch`) | `git diff -- src/core/DrumSynth.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 |
| Rebuild without fix (`build-9.log`) | `cmake --build build --target zene DrumSynthTest > .../build-9.log 2>&1` | 0 |
| RED (`test-9.log`) | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure > .../test-9.log 2>&1` | 8 |
| Restore production diff | `git apply .../fix.patch` | 0 |
| Rebuild with fix (`build-10.log`) | `cmake --build build --target zene DrumSynthTest > .../build-10.log 2>&1` | 0 |
| GREEN (`test-10.log`) | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure > .../test-10.log 2>&1` | 0 |
| Affected family (`test-11.log`) | `cd build/tests && ctest -R 'DrumSynthLevelTest\|DrumSynthTest\|SampleBufferResampleTest' --output-on-failure > .../test-11.log 2>&1` | 0 |
| Complexity/file-length gate (`gate-2.log`) | `python3 .../feature_check.py <worktree> > .../gate-2.log 2>&1` | 0 |

Affected-family count: **3 CTest tests executed** (`DrumSynthLevelTest`, `DrumSynthTest`, `SampleBufferResampleTest`), all pass.

## Files committed

- `src/core/DrumSynth.cpp`
- `include/DrumSynth.h`
- `tests/src/core/DrumSynthTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-143-port.md`
