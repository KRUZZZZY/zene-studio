# Cluster new-p8-301

Verdict: **FIXED**

`CollabClient` now caps its pending nested-loop backlog at 1,024 frames and `MaxHostLineBytes` total raw frame bytes. If another frame would exceed either cap, it clears pending work, stops the drain timer, and closes the link with a backlog-specific reason. Closing forces a fresh join and snapshot before the client can continue applying the ordered stream.

## File evidence

- `include/CollabClient.h:102` stores each queued message with its frame size and tracks aggregate bytes at line 140.
- `src/core/CollabClient.cpp:42` defines both caps. `onMessage` enforces them and closes on overflow at lines 186–198; `drainBacklog` subtracts the dequeued frame size at lines 201–210.
- `tests/src/core/CollabTransportTest.cpp:194` starts a host flood inside a nested event loop and verifies the client fails with the backlog reason at lines 228–231.

## Verification evidence

The red run rebuilt `zene` and `CollabTransportTest` with the production patch reversed. The regression failed at `client.state()`: actual `Joined` (2), expected `Failed` (3). Reapplying the patch and rebuilding made the same ctest family pass.

| Result | Command | Exit | Log |
|---|---|---:|---|
| RED build | `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-301.r1/logs/build-10.log 2>&1; result=$?; echo EXIT=$result` | 0 | `build-10.log` |
| RED test | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-301.r1/logs/test-9.log 2>&1; result=$?; echo EXIT=$result` | 8 | `test-9.log` |
| GREEN build | `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-301.r1/logs/build-11.log 2>&1; result=$?; echo EXIT=$result` | 0 | `build-11.log` |
| GREEN test | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-301.r1/logs/test-10.log 2>&1; result=$?; echo EXIT=$result` | 0 | `test-10.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-301.r1/logs/gate-complexity-1.log 2>&1; result=$?; echo EXIT=$result` | 0 | `gate-complexity-1.log` |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-301.r1/logs/gate-file-length-1.log 2>&1; result=$?; echo EXIT=$result` | 0 | `gate-file-length-1.log` |

The affected ctest family executed 1/1 target. On RED, QtTest reported 11 passed, 1 failed, and 0 skipped; on GREEN, CTest reported 1/1 passed. The patch reversal and reapplication commands both exited 0:

```sh
git diff -- include/CollabClient.h src/core/CollabClient.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-301.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-301.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-301.r1/logs/fix.patch
```

Earlier flood-test drafts are recorded in `test-1.log` through `test-7.log` (each exited 8). Those drafts entered the nested loop from a reply handler or starved delivery with a zero-interval flood, so they did not prove that frames reached the client while its loop depth was greater than one. The final test enters the loop from a timer first, then sends at 1 ms intervals. The environment did not expose an `fff grep` tool or executable; build and test logs were searched with separate `rg` searches for `error`, `FAILED`, and `Assertion`.

## Files committed

- `include/CollabClient.h`
- `src/core/CollabClient.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-301.md`
