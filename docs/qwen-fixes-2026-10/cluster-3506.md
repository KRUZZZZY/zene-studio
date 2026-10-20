ALREADY-FIXED

3506: ALREADY-FIXED
Evidence: `src/tracks/SampleTrack.cpp:163-169` already stores the per-pass schedule in a fixed `std::array` of 64 clips; excess starts are deferred. The added `RecordingRealtimeTest::mutedSampleClipSchedulingDoesNotAllocate` measures the pass while a muted active clip avoids handle allocation. A vector-backed mutation fails with one observed allocation; the branch's array passes with zero.

Commands and proof:
- `cmake --build build --target RecordingRealtimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/build-001-vector-red.log 2>&1` (temporary vector-backed storage) — EXIT 0.
- `QT_QPA_PLATFORM=offscreen ./RecordingRealtimeTest mutedSampleClipSchedulingDoesNotAllocate > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/test-001-vector-red.log 2>&1` (cwd: `build/tests`, temporary vector-backed storage) — EXIT 1; `Totals: 2 passed, 1 failed`.
- `cmake --build build --target RecordingRealtimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/build-002-array-green.log 2>&1` (restored existing fixed array) — EXIT 0.
- `QT_QPA_PLATFORM=offscreen ./RecordingRealtimeTest mutedSampleClipSchedulingDoesNotAllocate > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/test-002-array-green.log 2>&1` (cwd: `build/tests`) — EXIT 0; `Totals: 3 passed, 0 failed`.
- `ctest -R '^RecordingRealtimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/test-003-family-3506.log 2>&1` (cwd: `build/tests`) — EXIT 0; 1/1 CTest target passed.
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3506 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/gate-001-pre-commit-3506.log 2>&1` — EXIT 0; all gates PASS.

new-p16-206: FIXED
Evidence: `include/SampleTrack.h:91-100` now reads and publishes the playing snapshot atomically without emitting. `src/gui/tracks/SampleTrackView.cpp:281-286` polls from the existing GUI periodic update; `src/tracks/SampleTrack.cpp:134-143` emits `playingChanged` there only when the observed state changes. The regression verifies no signal during `SampleTrack::play`, then one GUI-poll signal per state transition.

Commands and proof:
- `cmake --build build --target RecordingRealtimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/build-003-playing-red.log 2>&1` — EXIT 0.
- `QT_QPA_PLATFORM=offscreen ./RecordingRealtimeTest sampleTrackPlayingChangeIsDeliveredByGuiPoll > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/test-004-playing-red.log 2>&1` (cwd: `build/tests`, original setter) — EXIT 1; the immediate signal count assertion failed.
- `cmake --build build --target RecordingRealtimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/build-005-playing-green.log 2>&1` — EXIT 0.
- `QT_QPA_PLATFORM=offscreen ./RecordingRealtimeTest sampleTrackPlayingChangeIsDeliveredByGuiPoll > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/test-005-playing-green.log 2>&1` (cwd: `build/tests`) — EXIT 0.
- `git diff -- include/SampleTrack.h src/tracks/SampleTrack.cpp src/gui/tracks/SampleTrackView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/fix.patch` — EXIT 0.
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/fix.patch` — EXIT 0.
- `cmake --build build --target RecordingRealtimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/build-006-playing-proof-red.log 2>&1` — EXIT 0.
- `QT_QPA_PLATFORM=offscreen ./RecordingRealtimeTest sampleTrackPlayingChangeIsDeliveredByGuiPoll > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/test-006-playing-proof-red.log 2>&1` (cwd: `build/tests`) — EXIT 1; the immediate signal count assertion failed.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/fix.patch` — EXIT 0.
- `cmake --build build --target RecordingRealtimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/build-007-playing-proof-green.log 2>&1` — EXIT 0.
- `QT_QPA_PLATFORM=offscreen ./RecordingRealtimeTest sampleTrackPlayingChangeIsDeliveredByGuiPoll > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/test-007-playing-proof-green.log 2>&1` (cwd: `build/tests`) — EXIT 0; `Totals: 3 passed, 0 failed`.
- `sample_track_targets=$(rg -l '#include "SampleTrack.h"' tests/src --glob '*.cpp' | sed 's#^.*/##; s/\.cpp$//' | sort -u)` followed by `cmake --build build --parallel 4 --target zene $sample_track_targets > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/build-012-header-dependents-parallel.log 2>&1` — EXIT 0; `zene` and 63 dependent test targets built.
- `ctest -R '^(RecordingRealtimeTest|WarpQuantizeTest|EngineRenderBenchmarkTest|NoteBackgroundAudioTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/test-010-affected-families.log 2>&1` (cwd: `build/tests`) — EXIT 0; 4/4 CTest targets passed.
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3506 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3506.r1/logs/gate-003-pre-commit-new-p16-206.log 2>&1` — EXIT 0; all gates PASS.
