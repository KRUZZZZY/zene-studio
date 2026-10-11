# Cluster new-p10-417

Verdict: **FIXED**

## Evidence

ThreadableJob is an abstract polymorphic base, and its public API had no virtual destructor. Deleting the test's derived job through std::unique_ptr<ThreadableJob> skipped the derived destructor. The fix adds a public virtual default destructor.

| File and line | Evidence |
|---|---|
| include/ThreadableJob.h:52 | Public defaulted virtual destructor |
| tests/src/core/RenderJobQueueTest.cpp:140 | Derived job records destructor execution |
| tests/src/core/RenderJobQueueTest.cpp:209 | Regression deletes through ThreadableJob and checks the derived destructor |
| tests/upstream-modifications.txt:1054 | Required inherited-file reason and regression reference |

The regression was red before the fix and red again after removing only the product patch. It passed after restoring the patch. Each ctest -R '^RenderJobQueueTest$' invocation executed 1 of 1 matched CTest tests (RenderJobQueueTest). The red runs reported 10 passed and 1 failed inside QtTest; the green runs passed the CTest target.

## Commands and recorded exit codes

Each command below preserved its command status in result and printed it as EXIT=.... All build, test, and gate output is in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs.

Initial regression, before the fix:

| Exact command | Recorded result |
|---|---:|
| cmake --build build --target RenderJobQueueTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |
| From build/tests: ctest -R '^RenderJobQueueTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=8 |

Initial green run after the fix:

| Exact command | Recorded result |
|---|---:|
| cmake --build build --target zene RenderJobQueueTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |
| From build/tests: ctest -R '^RenderJobQueueTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |

Required patch-out proof:

| Exact command | Recorded result |
|---|---:|
| git diff -- include/ThreadableJob.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |
| cmake --build build --target zene RenderJobQueueTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |
| From build/tests: ctest -R '^RenderJobQueueTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=8 |
| git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |
| cmake --build build --target zene RenderJobQueueTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |
| From build/tests: ctest -R '^RenderJobQueueTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |

Ratchet gates:

| Exact command | Recorded result |
|---|---:|
| bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |
| bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-417.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0 | EXIT=0 |

The session did not expose the requested fff grep tool, and command -v fff returned no path. I searched the log folder with rg one term at a time (error, FAILED, Assertion): only expected CTest failure lines appeared in the two red logs, and no Assertion match appeared.

## Files in this commit

- include/ThreadableJob.h
- tests/src/core/RenderJobQueueTest.cpp
- tests/upstream-modifications.txt
- docs/qwen-fixes-2026-10/cluster-new-p10-417.md
