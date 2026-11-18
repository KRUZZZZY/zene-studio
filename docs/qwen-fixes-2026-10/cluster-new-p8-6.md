# Cluster new-p8-6 — FIXED

## Finding and evidence

`clip.link_create` built the transaction's `before` state after setting the anchor's link ID. The handler now snapshots `cliplink::membershipState(anchor.clip)` before assigning any IDs and passes that snapshot to `transactionPayload` in [ControlCommandsClipLink.cpp](../../src/core/ControlCommandsClipLink.cpp#L209).

The regression in [ClipLinkTest.cpp](../../tests/src/core/ClipLinkTest.cpp#L173) creates two unlinked clips, invokes `clip.link_create`, and checks the stored transaction has the anchor's ID and `group: 0`.

## Evidence table

All CMake build commands ran from the worktree root. All `ctest` commands ran from `build/tests`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs`.

| Check | Exact command | EXIT |
|---|---|---:|
| Initial regression build | `cmake --build build --target zene ClipLinkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| RED on original code | `ctest -R ClipLinkTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | 8 |
| GREEN build | `cmake --build build --target zene ClipLinkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| GREEN test | `ctest -R ClipLinkTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Save production patch for reversal proof | `git diff -- src/core/ControlCommandsClipLink.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Remove fix for reversal proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Reversal build | `cmake --build build --target zene ClipLinkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| RED with fix removed | `ctest -R ClipLinkTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` | 8 |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Final build | `cmake --build build --target zene ClipLinkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Final GREEN test | `ctest -R ClipLinkTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-6.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |

The affected family contains one registered CTest target, `ClipLinkTest`; the final run passed 1/1. The original RED run reported 14 QtTest cases passed and the new regression failed. With the production patch removed, the same regression failed with actual `before.group` 7 and expected 0.

## Files committed

- `src/core/ControlCommandsClipLink.cpp`
- `tests/src/core/ClipLinkTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-6.md`

The `fff grep` tool/executable was not available in this session, so required log terms were searched with `rg` without reading whole logs.
