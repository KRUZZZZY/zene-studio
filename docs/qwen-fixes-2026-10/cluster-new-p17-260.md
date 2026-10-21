new-p17-260: FIXED
new-p17-257: FIXED
new-p9-22: FIXED

Evidence:
- src/core/CollabHost.cpp broadcastSnapshot() now counts project-holding recipients via sendToProjectPeers() (now int) and emits "no project peers to re-sync (...)" when there are none, otherwise "peers re-synced (...): N recipient(s)".
- src/core/CollabHost.cpp onMessage() no longer drops whitespace-only lines before the request handler; CollabSession::handlePeerRequest() answers them with the malformed-wire id:-1 reply.
- src/core/CollabHost.cpp snapshotFrame() checks base64Size + 64 >= MaxHostLineBytes before frameOf()/toBase64(); src/core/CollabSessionSync.cpp writeSnapshot() checks file size before readAll().
Tests: tests/src/core/CollabTransportTest.cpp adds broadcastSnapshotReportsRecipients, blankCollaborationLineReceivesMalformedReply, oversizedSnapshotIsRefusedBeforeEncoding, plus source-order checks.

Commands:
- RED: ctest -R '^CollabTransportTest$' --output-on-failure (build/tests) -> exit 8 (logs/test-red.log: 5 FAIL including broadcastSnapshotReportsRecipients, blankCollaborationLineReceivesMalformedReply, snapshotSizeCheckPrecedesEncodingInSource, blankLinesAreDispatchedInSource, peerBroadcastUsesStableIdSnapshot)
- GREEN: ctest -R '^CollabTransportTest$' --output-on-failure (build/tests) -> exit 0 (logs/test-green.log)
- GREEN family: ctest -R '^CollabPolicyTest$' -> exit 0 (logs/test-5.log)
- Gate: python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-260 -> exit 0 (logs/gate-1.log, RESULT: PASS)
- Build: cmake --build build --target CollabTransportTest zene -> exit 0 (logs/build-green.log)
