# Cluster new-p11-13: A throttled resync request is silently dropped

Verdict: **BLOCKED**

The finding is real. `CollabHost::resyncPeer()` returned during the cooldown without replying, and a `CollabClient` waiting for a failed streamed command's replacement snapshot had no retry path. The host now coalesces one pending request per peer and serves it when the cooldown expires.

Evidence:

- [include/CollabHost.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-13/include/CollabHost.h:140) stores the per-peer pending state.
- [src/core/CollabHost.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-13/src/core/CollabHost.cpp:288) queues one timer, rechecks the peer by ID, and sends the snapshot after the cooldown.
- [tests/src/core/CollabTransportTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-13/tests/src/core/CollabTransportTest.cpp:357) reproduces two failed streamed commands inside one throttle interval.

The affected CTest family ran 1 registered test (`CollabTransportTest`). The RED run reported 23 QtTest cases passed and the new case failed; GREEN runs passed the registered test.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| RED build | `cmake --build build --target CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Fresh test target built before RED. |
| RED test | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/test-1.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 8 | New assertion failed: no third snapshot after the second command failure. Totals: 23 passed, 1 failed. |
| GREEN build | `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and the test target. |
| GREEN test | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/test-2.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 0 | 1/1 registered tests passed. |
| Remove product fix | `git diff -- include/CollabHost.h src/core/CollabHost.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/fix.patch; echo EXIT=$?` | 0 | Reversed only the product patch for the inverse proof. |
| Inverse build | `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt both affected targets without the fix. |
| Inverse RED | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/test-3.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 8 | Same new assertion failed; totals: 23 passed, 1 failed. |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored the verified product patch. |
| Final build | `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 | Rebuilt both affected targets after restoring the fix. |
| Final GREEN | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/test-4.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 0 | 1/1 registered tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 1 | Reports six new over-target functions in untouched files. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-13.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 1 | Reports 19 sources over 500 code lines, including `CollabTransportTest.cpp` at 567. |

The fix and regression pass, including the inverse proof. The required ratchets remain red: the complexity failures are in untouched functions in `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, and `src/gui/ControlCommandsShortcuts.cpp`. The file-length gate also reports 19 over-limit files. I did not alter either gate or its baseline, so full gated completion is blocked.

An initial identical test-target build was also started with `build-1.log`; its tool session did not surface the exit status. The repeated build recorded in `build-2.log` exited 0.
