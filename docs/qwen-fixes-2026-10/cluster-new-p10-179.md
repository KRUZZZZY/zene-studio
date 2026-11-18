# Cluster new-p10-179 — FIXED

## Finding and fix

The finding was real. MIDI clip notes use separate NotePlayHandle instances, but both note events previously reached the base-instrument gate with only a key. Per-key accepted/skipped counters could therefore route a shorter skipped note's off to the base instrument while swallowing the accepted longer note's off.

Each note handle now carries its base-instrument routing decision in an atomic flag. Its note-on stores the decision, and its own note-off reads that same flag. Direct MIDI events continue to use the per-key counters. The new regression covers both accepted-longer/skipped-shorter and skipped-longer/accepted-shorter, checking the track's endNote signal to verify which offs pass the base gate.

Evidence: [InstrumentLayers.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-179/include/InstrumentLayers.h:131), [InstrumentLayers.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-179/src/core/InstrumentLayers.cpp:315), [NotePlayHandle.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-179/include/NotePlayHandle.h:363), [NotePlayHandle.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-179/src/core/NotePlayHandle.cpp:270), [InstrumentTrack.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-179/src/tracks/InstrumentTrack.cpp:589), and [InstrumentLayersCommandsTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-179/tests/src/core/InstrumentLayersCommandsTest.cpp:177). The RED assertion was skippedShortOffs: actual 1, expected 0 (test line 231).

## Verification evidence

All build and test logs are new files under /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs.

| Purpose | Exact command | Exit |
|---|---|---:|
| Build with production patch removed | cmake --build build --target zene InstrumentLayersCommandsTest -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/build-9.log' 2>&1; result=$?; echo EXIT=$result; exit 0 | 0 |
| RED with production patch removed, from build/tests | ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/red-4.log' 2>&1; result=$?; echo EXIT=$result; exit 0 | 8 |
| Restore production patch | git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/fix.patch' | 0 |
| Build restored patch | cmake --build build --target zene InstrumentLayersCommandsTest -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/build-10.log' 2>&1; result=$?; echo EXIT=$result; exit 0 | 0 |
| GREEN, from build/tests | ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/green-4.log' 2>&1; result=$?; echo EXIT=$result; exit 0 | 0 |
| Verbose family count, from build/tests | ctest -V -R '^InstrumentLayersCommandsTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/test-verbose-1.log' 2>&1; result=$?; echo EXIT=$result; exit 0 | 0 |
| Complexity gate | bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/gate-complexity-1.log' 2>&1; result=$?; echo EXIT=$result; exit 0 | 0 |
| File-length gate | bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/gate-file-length-1.log' 2>&1; result=$?; echo EXIT=$result; exit 0 | 0 |

The required reversible-patch proof also used git diff -- include/InstrumentLayers.h src/core/InstrumentLayers.cpp include/InstrumentTrack.h include/NotePlayHandle.h src/core/NotePlayHandle.cpp src/tracks/InstrumentTrack.cpp src/tracks/InstrumentTrackNotes.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r2/logs/fix.patch' (EXIT 0). Rebuilding and rerunning the same targeted CTest without those product changes failed at the stated assertion; reapplying the patch, rebuilding, and rerunning passed.

The affected family executed 1/1 CTest target. Its verbose QtTest run reported 8 passed, 0 failed, and 1 skipped (multiOutputBusesRouteCreateUndo, because this build has no CLAP instrument module). Both required gates passed.

## Files committed

- include/InstrumentLayers.h
- include/InstrumentTrack.h
- include/NotePlayHandle.h
- src/core/InstrumentLayers.cpp
- src/core/NotePlayHandle.cpp
- src/tracks/InstrumentTrack.cpp
- src/tracks/InstrumentTrackNotes.cpp
- tests/src/core/InstrumentLayersCommandsTest.cpp
- tests/upstream-modifications.txt
- docs/qwen-fixes-2026-10/cluster-new-p10-179.md
