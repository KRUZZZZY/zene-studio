# Cluster new-p11-87: sample-track pan is applied on import

**Verdict: FIXED**

`src/core/DawProjectSession.cpp:166-176` handled only `InstrumentTrack` in
`panningModelForTrack()`, although `SampleTrack` exposes `panningModel()`. The
import at `src/core/DawProjectSession.cpp:556-562` therefore left a sample
track centred when the model carried a non-centre pan. The helper now returns
the sample track's model too. The regression at
`tests/src/core/DawProjectInterchangeRoundTripTest.cpp:397-415` applies a
sample-track pan of `0.75` and checks for the engine value `50`.

## Evidence

The affected CTest family ran 1 test executable. Before the fix and with the
fix temporarily removed, the Qt test reported 34 passed and the regression
failed (`actual 0`, `expected 50`). With the fix restored, CTest reported 1/1
passed.

Commands below ran from the worktree root unless marked `build/tests`. Every
build and test command redirected its output to the named new log in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs`.

| Command | Exit | Result |
| --- | ---: | --- |
| `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/build-1.log 2>&1` | 0 | Original production code rebuilt with the regression. |
| *(from `build/tests`)* `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/test-1.log 2>&1` | 8 | RED: actual pan `0`, expected `50`; 34 passed, 1 failed. |
| `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/build-2.log 2>&1` | 0 | Rebuilt after the fix. |
| *(from `build/tests`)* `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/test-2.log 2>&1` | 0 | GREEN: 1/1 passed. |
| `git diff -- src/core/DawProjectSession.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/fix.patch` | 0 | Removed only the production fix for the proof run. |
| `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/build-3.log 2>&1` | 0 | Rebuilt without the production fix. |
| *(from `build/tests`)* `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/test-3.log 2>&1` | 8 | RED proof: same actual `0`, expected `50`; 34 passed, 1 failed. |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/fix.patch` | 0 | Restored the production fix. |
| `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/build-4.log 2>&1` | 0 | Final rebuild with the fix. |
| *(from `build/tests`)* `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/test-4.log 2>&1` | 0 | Final GREEN: 1/1 passed. |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/gate-1.log 2>&1` | 0 | PASS; no regressions. |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-87.r1/logs/gate-2.log 2>&1` | 0 | PASS; no regressions. |

The requested fff grep tool was unavailable in the provided tools, so build,
test, and gate logs were searched with `rg` one term at a time.

## Files committed

- `src/core/DawProjectSession.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-87.md`
