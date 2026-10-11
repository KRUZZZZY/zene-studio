# Cluster new-p8-307

Verdict: **FIXED**

`base32Decode` now rejects nonzero unused trailing bits. An IPv4 invite's final
base32 symbol has one padding bit; changing that bit no longer yields the same
accepted invite.

## File evidence

- `src/core/CollabInvite.cpp:131` — `base32Decode`; line 149 checks that the
  leftover bits are zero.
- `tests/src/core/CollabPolicyTest.cpp:239` — `inviteRejectsDamage`; lines
  253–258 change only the final IPv4 symbol's unused low bit and require invite
  decoding to fail.

## Evidence table

The affected CTest family contains one test (`CollabPolicyTest`); each run below
executed 1/1 test.

| Phase | Command | Exit | Result |
| --- | --- | ---: | --- |
| Initial build | `cmake --build build --target CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Built the regression against original production code |
| RED | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` (cwd: `build/tests`) | 8 | `inviteRejectsDamage` accepted the altered padding bit |
| Fixed build | `cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Rebuilt `zene` and the affected test |
| GREEN | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` (cwd: `build/tests`) | 0 | 1/1 passed |
| Reverse fix | `git diff -- src/core/CollabInvite.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/fix.patch; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Reversed only the production fix |
| Reversal build | `cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Rebuilt with the fix reversed |
| Reversal RED | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` (cwd: `build/tests`) | 8 | 1/1 failed on the padding-bit assertion |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/fix.patch; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Restored the production fix |
| Reapplied build | `cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Rebuilt with the fix restored |
| Reapplied GREEN | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` (cwd: `build/tests`) | 0 | 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Passed; no new regressions |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-307.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%d\n' "$result"; exit "$result"` | 0 | Passed; no new regressions |

## Files committed

- `src/core/CollabInvite.cpp`
- `tests/src/core/CollabPolicyTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-307.md`
