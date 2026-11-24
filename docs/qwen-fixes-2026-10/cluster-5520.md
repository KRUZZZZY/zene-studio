FIXED

# Cluster 5520 - silent WSOLA overlap biased grain starts by -searchRadius

## Verdict: REAL
- `src/core/AudioStretcher.cpp` (produceHop): `bestScore` starts at -infinity, offsets run from `-searchRadius` upward, and the replacement test is a strict `>`. A silent carry makes every score exactly 0, so the first candidate (`-searchRadius`, -128 by default) won.
- Fix: new `carryHasEnergy()` helper (anonymous namespace); alignment runs only when the carry's energy exceeds 1e-12 (the same floor the candidate-energy guard uses). Otherwise `bestOffset` stays 0, i.e. the grain starts at the cursor. No header change, no audio-thread allocation, no commands.
- Regression test: `AudioStretcherTest::aGrainAfterASilentOverlapStartsAtTheCursor` (`tests/src/core/AudioStretcherTest.cpp`). Speed 2, source silent until frame 1100: grain 1 leaves a silent carry, grain 2's cursor is 1024, and output frame 512+i must equal w[i]*source[1024+i] (turns on at frame 588; biased it would turn on at 716).

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5520.r1/logs)
- RED (test, no fix): `cmake --build build --target zene AudioStretcherTest -j4` > build-1.log EXIT=0; `ctest -R AudioStretcher --output-on-failure` (build/tests) > test-1.log EXIT=8: `output frame 588 is 0, expected 0.0533878 (grain not at the cursor)`.
- GREEN: build-2.log EXIT=0; test-2.log EXIT=0 (AudioStretcherTest passed).
- 4b: `git diff -- src/core/AudioStretcher.cpp > fix.patch && git apply -R fix.patch` EXIT=0; build-3.log EXIT=0; test-3.log EXIT=8 (same failure at frame 588). `git apply fix.patch` EXIT=0; build-4.log EXIT=0; test-4.log EXIT=0.
- Gates: `feature_check.py <worktree>` > gates-1.log EXIT=0, RESULT: PASS (all 8 gates).

## Other findings
- Sibling, not fixed: if the carry has energy but every candidate in the search window is exactly silent, all scores still tie at 0 and `-searchRadius` still wins. It needs a tie-break toward offset 0 and is outside this finding's described fix.

Delegation: Haiku ran every build, ctest and the feature_check gate run (four runs) and reported exit codes and failure lines; I checked test-2.log and the patch/diff myself and wrote the test, fix, patch reversal and this report.
