# Cluster new-p8-304: stopping leaves pending handshakes alive

**Verdict: FIXED**

`CollabHost::stop()` now marks and disconnects each pending TLS socket, aborts it, and resets the pending count (`src/core/CollabHost.cpp:143-168`). Accepted sockets are tracked in `include/CollabHost.h:162` and `src/core/CollabHost.cpp:185-205`. Handshake completion/failure removes only a currently pending socket, so a callback from a stopped session cannot admit a peer (`src/core/CollabHost.cpp:220-249`).

The regression fills the pending-handshake limit with raw TCP clients, confirms the next connection is refused, stops the host and checks every pending client disconnects, then restarts and joins a valid client (`tests/src/core/CollabTransportTest.cpp:174-199`). It fails on the original code and when the production patch is reversed.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline regression build | `cmake --build build --target CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 0 | Built test with original production code |
| RED on original code | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 8 | Pending clients remained connected after stop |
| Fixed build | `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 0 | Rebuilt app and affected test target |
| GREEN | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 0 | Affected family: 1/1 CTest target passed |
| Save and reverse production patch | `git diff -- include/CollabHost.h src/core/CollabHost.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit 0` | 0 | Prepared negative control |
| Negative-control rebuild | `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 0 | Rebuilt with production fix reversed |
| RED with fix reversed | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 8 | The same pending-client assertion failed |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit 0` | 0 | Restored fix |
| Final build | `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 0 | Rebuilt app and affected test target |
| Final GREEN | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 0 | Affected family: 1/1 CTest target passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/complexity-gate-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 0 | Passed |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-304.r1/logs/file-length-gate-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | 0 | Passed |

## Files committed

- `include/CollabHost.h`
- `src/core/CollabHost.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-304.md`
