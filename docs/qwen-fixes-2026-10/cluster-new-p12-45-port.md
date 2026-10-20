FIXED

Cluster: new-p12-45-port
Finding: Out-of-grid recording pairs pass preflight and are still landed
File: src/core/ControlCommandsSessionRecordLand.cpp

Evidence:
- landPairState checked song-track bounds but not session-grid bounds before calling model.slot(pair.track, pair.scene). An out-of-range session cell resolves to the model's shared empty slot, so a blank/incorrect arrangement clip was created.
- Fixed by extending the bounds check in landPairState to also require pair.track < model.trackCount() and 0 <= pair.scene < model.sceneCount(), returning pairNotLanded(..., "column_has_no_song_track") for out-of-grid pairs (consistent with the no-song-track path).

Commands:
- RED: git apply -R .../logs/fix.patch (EXIT=0); cmake --build build --target ControlSessionRecordLandTest (EXIT=0); ctest -R ControlSessionRecordLandTest (EXIT=8): 12 passed, 1 failed (anOutOfGridCellIsReportedNotLanded).
- GREEN: git apply .../logs/fix.patch (EXIT=0); cmake --build build --target ControlSessionRecordLandTest (EXIT=0); ctest -R ControlSessionRecordLandTest (EXIT=0): 13 passed, 0 failed.
- Gate: python3 .../feature_check.py <worktree> (EXIT=0): RESULT: PASS.

Files committed:
- src/core/ControlCommandsSessionRecordLand.cpp
- tests/src/core/ControlSessionRecordLandTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p12-45-port.md
