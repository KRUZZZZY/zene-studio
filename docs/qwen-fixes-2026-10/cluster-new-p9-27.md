# Cluster new-p9-27

**Verdict: FIXED**

`CollabClient::onClosed()` left the last peer array in memory after a transport close, and `onBye()` did the same after a host goodbye. Both terminal paths now clear the peer array and emit `peersChanged()`.

Evidence:

- [src/core/CollabClient.cpp](../../src/core/CollabClient.cpp):176 clears peers and emits `peersChanged()` on transport close; line 270 does the same on a host goodbye.
- [tests/src/core/CollabTransportTest.cpp](../../tests/src/core/CollabTransportTest.cpp):133 and :153 cover transport close and host goodbye. Each test confirms the cached peer was initially `connected: true`, then checks the list is empty and exactly one `peersChanged()` signal was emitted.
- Before the fix, both new assertions failed at test lines 149 and 167 with `client.peers().isEmpty()` returning false.

## Verification evidence

The focused family contains one registered CTest test (`CollabTransportTest`, 1/1 executed). The pre-fix QtTest output reported 11 passed and 2 failed entries (13 total); the two failures were the new close and goodbye regressions. CTest suppresses QtTest's passing detail, and reported 1/1 passed on both green runs.

| Check | Exact command | Exit |
| --- | --- | ---: |
| Original regression RED | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Initial GREEN | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Reversal proof RED, after removing the production patch and rebuilding | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Reapplied patch GREEN | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

Build commands, each completed with exit 0:

- `cmake --build build --target CollabTransportTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/build-1.log 2>&1; echo EXIT=$?`
- `cmake --build build --target CollabTransportTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/build-2.log 2>&1; echo EXIT=$?`
- `cmake --build build --target CollabTransportTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/build-3.log 2>&1; echo EXIT=$?`
- `cmake --build build --target CollabTransportTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/build-4.log 2>&1; echo EXIT=$?`
- `cmake --build build --target zene -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/build-5.log 2>&1; echo EXIT=$?`

Reversal commands both completed with exit 0:

- `git diff -- src/core/CollabClient.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/fix.patch; echo EXIT=$?`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-27.r1/logs/fix.patch; echo EXIT=$?`

## Files committed

- `src/core/CollabClient.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-27.md`
