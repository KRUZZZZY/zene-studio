# Cluster new-p8-234: Slip arithmetic can overflow signed ticks

**Verdict: FIXED**

## Evidence

`src/core/TakeLane.cpp:69` adds the tick and slip in `std::int64_t` and checks the
result against the `tick_t` range. `canSelect()` checks the last active tick of
the proposed segment (`:203-206`); `resolveSource()` and `takeAt()` reject an
unrepresentable slipped tick (`:338-358`). A project-loaded segment with an
overflowing slip remains readable as model data but neither lookup resolves it.

The regression cases in `tests/src/core/TakeLaneTest.cpp:176`, `:188`, and `:213`
cover selection validation and both lookup paths with a loaded `INT_MAX` slip.
They all failed on the original code, passed with the fix, and failed again
after reversing the production patch. The test source is registered at
`tests/fork-sources.txt:2048`.

| Evidence | Result |
| --- | --- |
| Original RED, `TakeLaneTest` | 1/1 CTest test failed; all three regression cases failed |
| Reverse-patch RED | Same test failed again after removing only the production fix |
| GREEN affected family | 3/3 CTest tests passed: `TakeLaneTest`, `TakeLaneCompTest`, `TakeLaneCompPlaybackTest` |
| Complexity gate | Exit 0 |
| File-length gate | Exit 0 |

## Commands and exit codes

Commands ran from the worktree root unless the command is a `ctest` invocation,
which ran from `build/tests`. Every build and test log is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs`.

```text
cmake --build build --target zene TakeLaneTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/build-1.log 2>&1; echo EXIT=$?  EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^TakeLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/test-1.log 2>&1; echo EXIT=$?  EXIT=8
cmake --build build --target zene TakeLaneTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/build-2.log 2>&1; echo EXIT=$?  EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^TakeLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/test-2.log 2>&1; echo EXIT=$?  EXIT=0
git diff -- src/core/TakeLane.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/fix.patch; echo EXIT=$?  EXIT=0
cmake --build build --target zene TakeLaneTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/build-3.log 2>&1; echo EXIT=$?  EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^TakeLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/test-3.log 2>&1; echo EXIT=$?  EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/fix.patch; echo EXIT=$?  EXIT=0
cmake --build build --target zene TakeLaneTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/build-4.log 2>&1; echo EXIT=$?  EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^TakeLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/test-4.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target zene TakeLaneTest TakeLaneCompTest TakeLaneCompPlaybackTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/build-5.log 2>&1; echo EXIT=$?  EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^TakeLane(CompTest|Test|CompPlaybackTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/test-5.log 2>&1; echo EXIT=$?  EXIT=0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/gate-1.log 2>&1; echo EXIT=$?  EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-234.r1/logs/gate-2.log 2>&1; echo EXIT=$?  EXIT=0
```

The requested `fff grep` tool was not exposed in this environment; I searched
the logs with one-term `rg` queries for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/TakeLane.cpp`
- `tests/src/core/TakeLaneTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-234.md`
