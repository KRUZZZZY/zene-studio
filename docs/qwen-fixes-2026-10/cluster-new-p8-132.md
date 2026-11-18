# Cluster new-p8-132: output bus slot publication

**Verdict: FIXED**

`src/core/InstrumentOutputBuses.cpp:102-116` changed `storeRoute()` so it prepares a detached `Slot` when a route has no slot yet or needs a differently sized block. The slot pointer is published, and its route atomics are updated, only after `ChangeGuard` is acquired. This covers both first routing and buffer resizing while keeping allocation off the render path.

The regression is `tests/src/core/InstrumentRoutingTest.cpp:77-95,174-186`. It holds the audio model-change guard, routes a bus from another thread, and checks that the slot is not published during the held render period. Before the fix, that assertion failed because `m_slots[1]` became non-null before the worker blocked on the guard.

## Evidence

| Check | Command | Exit / result |
|---|---|---|
| Original behavior, RED | `ctest -R '^InstrumentRoutingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/test-1.log` | 8; `outputBusSlotPublicationWaitsForRenderPeriod` failed, 8 passed and 1 failed. |
| Fixed behavior, GREEN | `ctest -R '^InstrumentRoutingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/test-2.log` | 0; 1/1 CTest executable passed, 9 passed and 0 failed. |
| Inverse-patch proof, RED | `ctest -R '^InstrumentRoutingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/test-3.log` | 8; same regression failed after removing the production fix, 8 passed and 1 failed. |
| Restored fix, final GREEN | `ctest -R '^InstrumentRoutingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/test-4.log` | 0; 1/1 CTest executable passed, 9 passed and 0 failed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/gate-1.log` | 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/gate-2.log` | 0. |

## Build and inverse-patch commands

Build commands and exits:

```sh
cmake --build build --target InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-1.log
```

Exit: 2. The initial test draft accessed the incomplete `Slot` definition and did not compile.

```sh
cmake --build build --target InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-2.log
```

Exit: 0 after reducing the probe to slot-pointer publication.

```sh
cmake --build build --target InstrumentRoutingTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-3.log
```

Exit: 0. The test target and `zene` were rebuilt together before the first GREEN run.

The inverse-patch check used these commands, each with exit 0 except for the test result recorded above:

```sh
git diff -- src/core/InstrumentOutputBuses.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/fix.patch
cmake --build build --target InstrumentRoutingTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-4.log
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/fix.patch
cmake --build build --target InstrumentRoutingTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-132.r1/logs/build-5.log
```

Each build command in the inverse sequence exited 0. The inverse ctest exited 8, and the final restored ctest exited 0, as shown in Evidence.

## Files committed

- `src/core/InstrumentOutputBuses.cpp`
- `tests/src/core/InstrumentRoutingTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-132.md`
