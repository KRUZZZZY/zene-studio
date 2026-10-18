# Cluster new-p10-143-port

Verdict: **FIXED**

Re-applied the DrumSynth duration-arithmetic guard on the newest line. `DrumSynth::LongestEnv()` now bounds the rounded envelope step count before the `int` conversion/multiplication, and `checkedWaveSampleCount()` validates the channel-expanded sample count against the `long` write cursor and `size_t` allocation byte limit before allocating with `std::nothrow`.

## File evidence

- `src/core/DrumSynth.cpp`: anonymous `checkedWaveSampleCount()` helper; `LongestEnv()` max-steps guard; allocation uses `std::nothrow` after validation.
- `tests/src/core/DrumSynthTest.cpp`: `refusesAnEnvelopeWhoseRoundedLengthOverflows()` uses a parser-safe `3e9` envelope and asserts refusal before allocation.
- `tests/upstream-modifications.txt`: registered the inherited `src/core/DrumSynth.cpp` change.

## Evidence table

| Phase | Exact command | Exit |
|---|---|---:|
| Build with fix (`build-1.log`) | `cmake --build build --target zene DrumSynthTest > .../build-1.log 2>&1` | 0 |
| GREEN (`test-1.log`, 1/1) | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure > .../test-1.log 2>&1` | 0 |
| Reverse production diff | `git diff -- src/core/DrumSynth.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 |
| Rebuild without fix (`build-2.log`) | `cmake --build build --target zene DrumSynthTest > .../build-2.log 2>&1` | 0 |
| RED (`test-2.log`) | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure > .../test-2.log 2>&1` | 8 |
| Restore production diff | `git apply .../fix.patch` | 0 |
| Rebuild with fix (`build-3.log`) | `cmake --build build --target zene DrumSynthTest > .../build-3.log 2>&1` | 0 |
| GREEN (`test-3.log`, 1/1) | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure > .../test-3.log 2>&1` | 0 |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check > .../gate-1.log 2>&1` | 1 (pre-existing regressions; none introduced by this change) |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check > .../gate-2.log 2>&1` | 1 (pre-existing regressions; none introduced by this change) |

Affected-family count: **1 CTest test executed** (`DrumSynthTest`), 2/2 test methods pass.

## Files committed

- `src/core/DrumSynth.cpp`
- `tests/src/core/DrumSynthTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-143-port.md`
