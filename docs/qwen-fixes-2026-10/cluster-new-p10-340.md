# Cluster new-p10-340

**Verdict: FIXED**

`RubberBandVoice::render()` used a 1× time ratio for NaN and other nonpositive speeds, but advanced `m_sourcePos` with the original input. A NaN therefore poisoned the position after rendering audio. The render now normalizes any nonfinite or nonpositive speed to `1.0`, and uses that value for both the time ratio and source position.

## File and line evidence

- `src/core/RubberBandStretch.cpp:206-207,231` validates speed before setting the time ratio and updating source position.
- `tests/src/tracks/SampleClipRubberBandTest.cpp:250-266` renders at NaN speed, verifies output was produced, and asserts the position is finite and advances at 1×.
- `tests/fork-sources.txt:2064` registers the product-new test source.

## Regression evidence

The affected family is one registered ctest (`SampleClipRubberBandTest`, CTest #295) containing nine QtTest case slots. The initial red run executed eight passing slots and failed the new assertion; the fixed run and the patch-restored proof each passed the ctest.

| Run | Exact command | Exit | Result |
|---|---|---:|---|
| Initial red | `ctest -R '^SampleClipRubberBandTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | 8 passed, 1 failed; `std::isfinite(voice.sourcePosition())` returned false. |
| Initial green | `ctest -R '^SampleClipRubberBandTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | 1/1 ctest passed. |
| Patch-reversal red | `ctest -R '^SampleClipRubberBandTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | After removing the production fix and rebuilding, the same assertion failed. |
| Patch-restored green | `ctest -R '^SampleClipRubberBandTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | After restoring the production fix and rebuilding, 1/1 ctest passed. |

## Build, proof, and gate commands

| Exact command | Exit |
|---|---:|
| `cmake --build build --target SampleClipRubberBandTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target zene SampleClipRubberBandTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| `git diff -- src/core/RubberBandStretch.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| `cmake --build build --target zene SampleClipRubberBandTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| `cmake --build build --target zene SampleClipRubberBandTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-340.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

## Files in the commit

- `src/core/RubberBandStretch.cpp`
- `tests/src/tracks/SampleClipRubberBandTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-340.md`
