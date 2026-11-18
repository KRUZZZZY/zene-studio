# Cluster new-p10-6: zero-port collaboration invites

**Verdict: FIXED**

`Invite::decode` accepted a valid invite whose encoded port was zero. `readBody` now rejects that port and reports an invalid-port error before assigning the decoded address and port. This keeps a malformed invite from reaching `collab.join` as a usable invite.

## File and test evidence

- `src/core/CollabInvite.cpp:103-112` parses the port, rejects zero, then fills the remaining invite fields.
- `tests/src/core/CollabPolicyTest.cpp:258-268` encodes a deterministic zero-port invite and asserts decoding fails.
- Both changed source files were already registered in `tests/fork-sources.txt` (`src/core/CollabInvite.cpp` and `tests/src/core/CollabPolicyTest.cpp`); that manifest did not need a change.

## Regression evidence

Commands ran from the worktree root unless the command is a CTest invocation; CTest ran from `build/tests`. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED on original code | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/test-1.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 8 | Expected failure: `inviteRejectsZeroPort()` showed that decode returned true. QtTest totals: 12 passed, 1 failed. |
| GREEN with fix | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/test-2.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 | 1/1 CTest target passed. |
| Revert-proof RED | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/test-3.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 8 | Failed again after removing only the production fix and rebuilding. |
| Reapplied-fix GREEN | `ctest -R '^CollabPolicyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/test-4.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 | Passed again after restoring the patch and rebuilding. |
| Affected family | `ctest -R '^(CollabPolicyTest|CollabTransportTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/test-5.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 | 2/2 CTest targets passed. |

The revert-proof commands both exited 0:

```sh
git diff -- src/core/CollabInvite.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/fix.patch; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/fix.patch; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"
```

The required rebuilds of `zene` and the affected test targets all exited 0:

```sh
cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/build-1.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"
cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/build-2.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"
cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/build-3.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"
cmake --build build --target zene CollabPolicyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/build-4.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"
cmake --build build --target zene CollabPolicyTest CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/build-5.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"
```

Each rebuild command above returned `EXIT=0`.

## Ratchet gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/gate-3.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-6.r1/logs/gate-4.log 2>&1; status=$?; printf 'EXIT=%d\n' "$status"; exit "$status"` | 0 |

`fff` grep was not available in this session, so the numbered logs were searched with `rg` for the requested error, FAILED, and Assertion terms.

## Files committed

- `src/core/CollabInvite.cpp`
- `tests/src/core/CollabPolicyTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-6.md`
