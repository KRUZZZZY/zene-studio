# Cluster new-p9-563: navigator span overflow

Verdict: **FIXED**

`src/gui/ArrangerNavigator.cpp:363-369` now calculates the song end and visible-view end in
`std::int64_t`, then clamps their maximum to the nonnegative `tick_t` range. The regression in
`tests/src/gui/ArrangerTimelineTest.cpp:368-379` creates a clip ending exactly at the maximum
`tick_t` and checks that the navigator slider still exposes a long-song range. Its assertion
failed with the original implementation and passed with the fix, including the required reverse
and restore proof. The product-new test is registered in `tests/fork-sources.txt:2064`.

## Evidence

The affected CTest family selected one test (`ArrangerTimelineTest`, CTest test #247). The RED Qt
run recorded 12 passed and the new assertion failed; GREEN CTest runs reported 1/1 passed.

| Check | Exact command | Exit |
| --- | --- | ---: |
| RED on original code | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/test-1.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 8 |
| GREEN after fix | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/test-2.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 0 |
| Proof: remove production fix | `git diff -- src/gui/ArrangerNavigator.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Proof RED rebuild | `cmake --build build --target zene ArrangerTimelineTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Proof RED test | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/test-3.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 8 |
| Proof: restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Proof GREEN rebuild | `cmake --build build --target zene ArrangerTimelineTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Proof GREEN test | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/test-4.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

Build commands for the original RED and initial GREEN runs also completed successfully:

```sh
cmake --build build --target zene ArrangerTimelineTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/build-1.log 2>&1; echo EXIT=$?
cmake --build build --target zene ArrangerTimelineTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-563.r1/logs/build-2.log 2>&1; echo EXIT=$?
```

Both commands printed `EXIT=0`.

The session did not expose the requested fff grep tool, so build, test, and gate logs were checked
with targeted `rg` searches for failure and assertion terms.

Files in this fix commit:

- `src/gui/ArrangerNavigator.cpp`
- `tests/src/gui/ArrangerTimelineTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-563.md`
