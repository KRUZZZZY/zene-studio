# Cluster new-p8-7: unlink transaction before-state

Verdict: **FIXED**

`clip.link_remove` read `cliplink::membershipState(ref.clip)` when creating its transaction payload, after it had set the removed clip's link ID to zero. The snapshot therefore described the post-unlink state. The handler now captures that membership object before changing link IDs and uses it as the transaction's `before` state ([src/core/ControlCommandsClipLink.cpp:285](../../src/core/ControlCommandsClipLink.cpp#L285), [src/core/ControlCommandsClipLink.cpp:318](../../src/core/ControlCommandsClipLink.cpp#L318)).

The regression in [ClipLinkTest.cpp:531](../../tests/src/core/ClipLinkTest.cpp#L531) checks the recorded transaction through `lastTransaction()`. Before the fix it failed with actual group `0`, expected group `85`; after the fix it passed. The modified inherited test is recorded in `tests/upstream-modifications.txt`.

## Verification evidence

Affected family: `ClipLinkTest` and `ClipLinkPersistenceTest` (2 tests). Exit code 8 is the expected ctest failure for RED runs.

| Check | Exact command | EXIT | Evidence |
|---|---|---:|---|
| Initial RED build | `cmake --build build --target zene ClipLinkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/build-1.log 2>&1` | 0 | Rebuilt `zene` and `ClipLinkTest` before running the regression. |
| Initial RED | `ctest -R '^ClipLinkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/test-1.log 2>&1` | 8 | 1 test; 13 passed, 1 failed. Recorded group was 0, expected 85. |
| GREEN build | `cmake --build build --target zene ClipLinkTest ClipLinkPersistenceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/build-2.log 2>&1` | 0 | Rebuilt `zene` and both affected test targets. |
| GREEN | `ctest -R 'ClipLink(Test|PersistenceTest)' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/test-2.log 2>&1` | 0 | 2/2 passed. |
| Save and reverse production patch | `git diff -- src/core/ControlCommandsClipLink.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/fix.patch` | 0 | Reversed only the production fix for the proof check. |
| Reversed-patch build | `cmake --build build --target zene ClipLinkTest ClipLinkPersistenceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/build-3.log 2>&1` | 0 | Rebuilt `zene` and both affected test targets without the fix. |
| Reversed-patch RED | `ctest -R 'ClipLink(Test|PersistenceTest)' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/test-3.log 2>&1` | 8 | 1/2 failed on group `0` versus `85`; `ClipLinkPersistenceTest` passed. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/fix.patch` | 0 | Restored the exact saved production patch. |
| Restored-patch build | `cmake --build build --target zene ClipLinkTest ClipLinkPersistenceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/build-4.log 2>&1` | 0 | Rebuilt `zene` and both affected test targets. |
| Restored-patch GREEN | `ctest -R 'ClipLink(Test|PersistenceTest)' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/test-4.log 2>&1` | 0 | 2/2 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/gate-1.log 2>&1` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-7.r1/logs/gate-2.log 2>&1` | 0 | Passed. |

The `fff` grep tool was unavailable in this environment; I searched each build, test, and gate log with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/ControlCommandsClipLink.cpp`
- `tests/src/core/ClipLinkTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-7.md`
