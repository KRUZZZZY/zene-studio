Verdict: FIXED
new-p17-260: FIXED
new-p17-257: FIXED
new-p9-22: FIXED

Evidence:
- src/core/CollabHost.cpp broadcastSnapshot() now counts project-holding recipients via sendToProjectPeers() (now int) and emits "no project peers to re-sync (...)" when there are none, otherwise "peers re-synced (...): N recipient(s)".
- src/core/CollabHost.cpp onMessage() no longer drops whitespace-only lines before the request handler; CollabSession::handlePeerRequest() answers them with the malformed-wire id:-1 reply.
- src/core/CollabHost.cpp snapshotFrame() uses the exact compact empty-data header size before base64 encoding; src/core/CollabSessionSync.cpp uses the matching current-sequence header size before file.readAll().
Tests: tests/src/core/CollabTransportTest.cpp adds broadcastSnapshotReportsRecipients, blankCollaborationLineReceivesMalformedReply, oversizedSnapshotIsRefusedBeforeEncoding, and snapshotFrameAcceptsPayloadThatFits; source-order checks cover both base64 framing and the temporary-file read.

Commands:
- RED: ctest -R '^CollabTransportTest$' --output-on-failure (build/tests) -> exit 8 (logs/test-red.log: 5 FAIL including broadcastSnapshotReportsRecipients, blankCollaborationLineReceivesMalformedReply, snapshotSizeCheckPrecedesEncodingInSource, blankLinesAreDispatchedInSource, peerBroadcastUsesStableIdSnapshot)
- GREEN: ctest -R '^CollabTransportTest$' --output-on-failure (build/tests) -> exit 0 (logs/test-green.log)
- GREEN family: ctest -R '^CollabPolicyTest$' -> exit 0 (logs/test-5.log)
- Gate: python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-260 -> exit 0 (logs/gate-1.log, RESULT: PASS)
- Build: cmake --build build --target CollabTransportTest zene -> exit 0 (logs/build-green.log)


## Self-check
- RED/GREEN: Reverting only product files (src/core/CollabHost.cpp, include/CollabHost.h, src/core/CollabSessionSync.cpp) made CollabTransportTest fail with exit code 8 (logs/test-red.log); restoring them made it pass with exit code 0 (logs/test-green.log).
- Deterministic: tests use injected snapshot providers, QSignalSpy, and source-order checks; no sleeps or wall-clock timing in the added assertions except existing QTRY macros.
- Cause/complete: sendToProjectPeers return value updated in header and definition; broadcastPeers/broadcastCommand callers ignore the new return value; CollabHost and CollabSessionSync size checks share the same MaxHostLineBytes limit.
- Collateral: the early size check uses the exact frame header size, so valid near-limit projects remain accepted; writeSnapshot checks file size before readAll().
- Verdicts: all three batch items are FIXED, not already-fixed/false-positive, with the code changes above.
