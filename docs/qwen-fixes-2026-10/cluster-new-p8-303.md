# Cluster new-p8-303

Verdict: **FIXED**

The finding was real. `src/core/CollabHost.cpp:180` now reserves capacity across admitted peers and pending handshakes at accept time. `src/core/CollabHost.cpp:229` rechecks capacity as TLS completes and turns away an excess peer before creating its snapshot. The deterministic regression at `tests/src/core/CollabTransportTest.cpp:138` holds two TLS handshakes, fills six peer slots, tries one more join, then completes both handshakes.

## Evidence

| Phase | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Initial test build | `cmake --build build --target CollabTransportTest` | 0 | `build-1.log` |
| RED before fix | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure` | 8 | Regression observed 9 peers; `test-1.log` |
| Fixed build | `cmake --build build --target zene CollabTransportTest` | 0 | `build-2.log` |
| GREEN after fix | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure` | 0 | 1/1 CTest tests passed; `test-2.log` |
| Remove-fix proof | `git diff -- src/core/CollabHost.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-303.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-303.r1/logs/fix.patch` | 0 | `proof-revert-1.log` |
| Rebuild without fix | `cmake --build build --target zene CollabTransportTest` | 0 | `build-3.log` |
| RED with fix removed | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure` | 8 | Same assertion observed 9 peers; `test-3.log` |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-303.r1/logs/fix.patch` | 0 | `proof-reapply-1.log` |
| Rebuild with fix | `cmake --build build --target zene CollabTransportTest` | 0 | `build-4.log` |
| GREEN after restore | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure` | 0 | 1/1 CTest tests passed; `test-4.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

Affected-family count: 1 registered CTest test (`CollabTransportTest`), executed and passing after the fix.

Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-303.r1/logs`. The fff grep tool was unavailable in this session, so the logs were searched with separate `rg` queries for `error`, `FAILED`, and `Assertion`.
