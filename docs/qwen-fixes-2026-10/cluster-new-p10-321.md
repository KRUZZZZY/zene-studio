# Cluster new-p10-321

Verdict: **FIXED**

`RackZones::loadSettings` accepted malformed and out-of-range zone bounds. A
failed `QString::toInt()` conversion became zero, and the resulting zone was
stored and could match a much wider range than the project specified.

## Evidence

- `src/core/RackZones.cpp:54-59` checks whether each integer attribute converts
  successfully while preserving the existing defaults for missing attributes.
- `src/core/RackZones.cpp:200-238` skips zones with malformed numeric values or
  invalid key/velocity ranges before loading their samples or storing them.
- `tests/src/core/RackZonesTest.cpp:125-144` loads a malformed key bound, an
  out-of-range velocity bound, and a valid zone; it requires only the valid zone
  to remain and confirms the malformed zone does not match key 0.
- Before the fix, the test failed at line 139: actual zone count 3, expected 1.
- The required reverse-patch check reproduced that failure, and restoring the
  patch made the same test pass.

## Build and test evidence

Build commands ran from the worktree root. CTest commands ran from
`build/tests`. The affected family matched and executed **1 CTest test**.

| Stage | Exact command | Exit |
|---|---|---:|
| Build regression before fix | `cmake --build build --target RackZonesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| RED before fix | `ctest -R '^RackZonesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | 8 |
| Build fix | `cmake --build build --target zene RackZonesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| GREEN after fix | `ctest -R '^RackZonesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Reverse production patch | `git diff -- src/core/RackZones.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Build without fix | `cmake --build build --target zene RackZonesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| RED with fix reversed | `ctest -R '^RackZonesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Rebuild restored fix | `cmake --build build --target zene RackZonesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| GREEN after restored fix | `ctest -R '^RackZonesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |

## Ratchet gates

Both gates passed.

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-321.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |

## Files in the commit

- `src/core/RackZones.cpp`
- `tests/src/core/RackZonesTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-321.md`
