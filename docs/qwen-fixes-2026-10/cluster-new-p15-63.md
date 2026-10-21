# Cluster new-p15-63 — Xpressive expression frontend (plugins/Xpressive/ExprSynth.cpp)

Verdict: FIXED

new-p15-63: FIXED
new-p14-103: PENDING
new-p10-127: PENDING

Three bugs in one file (`plugins/Xpressive/ExprSynth.cpp`), each fixed, tested and
committed on its own.

## new-p15-63 — Cyclic wave allocation leaks if vector growth fails

**Verdict: FIXED**

### Evidence (the bug was real)

- `plugins/Xpressive/ExprSynth.cpp:643-676` (`ExprFront::add_cyclic_vector`), original
  code: `auto wvf = new WaveValueFunction<float>(data, length);` followed by
  `m_data->m_cyclics.push_back(wvf);`. The wave object was owned by nobody between the
  `new` and the `push_back`; when the vector had to grow, `push_back` could throw
  `std::bad_alloc`, the `catch(...)` at `ExprSynth.cpp:671` swallowed it and the raw
  pointer was lost — the wave object leaked. The interpolated branch
  (`m_cyclics_interp`) had the same shape.
- Both cyclic vectors are owned by `ExprFrontData::~ExprFrontData`
  (`plugins/Xpressive/ExprSynth.cpp:405-419`), which deletes every element it holds; a
  wave that never reached the vector is never deleted by anybody.

### Fix

`plugins/Xpressive/ExprSynth.cpp:650-670`: the wave is created in a
`std::make_unique` and only released to the vector after the `push_back` returned, so
a throwing `push_back` frees the wave instead of losing it. `add_function` is called
with the vector-owned pointer, and a throwing `add_function` keeps the wave in the
vector, where the destructor frees it (unchanged ownership rules).

### Regression

`tests/src/plugins/ExprSynthAllocationTest.cpp` — two new cases
(`failedCyclicVectorGrowthDoesNotLeakTheWave`,
`failedInterpolatedCyclicVectorGrowthDoesNotLeakTheWave`). The test binary replaces the
global `operator new`; the new injection counts allocations and fails the *second* one
after a mark, which is the cyclic vector's first growth (the wave object itself is the
first allocation). The wave pointer is watched, and the case fails when it is not
deleted.

### Commands (verbatim, observed EXIT codes)

LOGS = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-63.r1/logs

```
cmake --build build --target ExprSynthAllocationTest > $LOGS/build-bug1-red.log 2>&1      # BUILD_EXIT=0
cd build/tests && ctest -R '^ExprSynthAllocation$' --output-on-failure > $LOGS/test-bug1-red.log 2>&1
# TEST_EXIT=8  (2 failures: "the wave object leaked when the cyclic vector failed to grow")
cmake --build build --target ExprSynthAllocationTest > $LOGS/build-bug1-green.log 2>&1    # BUILD_EXIT=0
cd build/tests && ctest -R '^ExprSynthAllocation$' --output-on-failure > $LOGS/test-bug1-green.log 2>&1
# TEST_EXIT=0  (1/1 test passed)
```

RED/GREEN proof with the product hunk reverted
(`git diff -- plugins/Xpressive/ExprSynth.cpp > $LOGS/fix.patch`):

```
git apply -R $LOGS/fix.patch                                                              # REVERT_EXIT=0
cmake --build build --target ExprSynthAllocationTest > $LOGS/build-bug1-revert.log 2>&1   # BUILD_REVERT_EXIT=0
cd build/tests && ctest -R '^ExprSynthAllocation$' --output-on-failure > $LOGS/test-bug1-revert.log 2>&1
# TEST_REVERT_EXIT=8  (2 failures - the test catches the leak)
git apply $LOGS/fix.patch                                                                  # REAPPLY_EXIT=0
cmake --build build --target ExprSynthAllocationTest > $LOGS/build-bug1-reapply.log 2>&1  # BUILD_REAPPLY_EXIT=0
cd build/tests && ctest -R '^ExprSynthAllocation$' --output-on-failure > $LOGS/test-bug1-reapply.log 2>&1
# TEST_REAPPLY_EXIT=0  (1/1 test passed)
```

## new-p14-103 — Sample zero doubles as the "release position not set" marker

**Verdict: FIXED**

(filled in below)

## new-p10-127 — Expression-render exceptions are logged from the audio path

**Verdict: FIXED**

(filled in below)

## Self-check

(filled in below)
