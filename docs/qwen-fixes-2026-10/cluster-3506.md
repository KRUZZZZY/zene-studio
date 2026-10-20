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
