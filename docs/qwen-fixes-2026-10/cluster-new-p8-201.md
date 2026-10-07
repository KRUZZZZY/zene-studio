# Cluster new-p8-201: held notes close at capture edge

**Verdict: FIXED**

The writer previously used only note-event timestamps as its end tick. When the last event was an
unmatched note-on, it closed that note at its own timestamp and produced a one-tick note. The live
clip and session capture callers now pass the current published transport tick as the capture edge.
The writer uses that edge, or a later note-event timestamp, while continuing to ignore trailing
non-note events for note length.

## Evidence

| Location | Evidence |
| --- | --- |
| `src/core/RetroMidiClipWriter.cpp:65` | `windowEndOf()` starts from the supplied capture edge and advances only for later note events. |
| `src/core/RetroMidiClipWriter.cpp:125` | `writeWindowToClip()` uses the explicit edge when closing unmatched notes. |
| `src/core/ControlCommandsMidi.cpp:446` | `midi.retro_capture_to_clip` reads `RetroMidiCapture::publishedTick()` and passes it to the writer at line 454. |
| `src/core/ControlCommandsSessionCapture.cpp:127` | `session.capture_to_slot` stores and passes the published edge to the same writer at line 92. |
| `tests/src/core/RetroMidiCaptureCommandsTest.cpp:671` | Regression captures an unmatched note-on at tick 100 with the capture edge at 2000 and checks for a 1900-tick note. |

The RED assertion reported `notes[0].length` as 1 instead of 1900, both before the production
change and after reversing the saved production patch. The identical test passed after restoring it.

## Commands and results

All logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs`.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Initial affected-target build | `cmake --build build --target zene RetroMidiCaptureCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/build-1.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 |
| Initial RED | `ctest -R '^RetroMidiCaptureCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/test-1.log 2>&1; test_status=$?; echo EXIT=$test_status` | 8 |
| Save production patch | `git diff -- include/RetroMidiClipWriter.h src/core/RetroMidiClipWriter.cpp src/core/ControlCommandsMidi.cpp src/core/ControlCommandsSessionCapture.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/fix.patch; patch_status=$?; echo EXIT=$patch_status; wc -l /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/fix.patch` | 0 |
| Reverse production patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/fix.patch; reverse_status=$?; echo EXIT=$reverse_status` | 0 |
| Build without production fix | `cmake --build build --target zene RetroMidiCaptureCommandsTest RecordingPassTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/build-3.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 |
| Reverse-patch RED | `ctest -R '^RetroMidiCaptureCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/test-4.log 2>&1; test_status=$?; echo EXIT=$test_status` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/fix.patch; apply_status=$?; echo EXIT=$apply_status` | 0 |
| Final affected-target build | `cmake --build build --target zene RetroMidiCaptureCommandsTest RecordingPassTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/build-5.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 |
| Final GREEN, `RetroMidiCaptureCommandsTest` | `ctest -R '^RetroMidiCaptureCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/test-7.log 2>&1; test_status=$?; echo EXIT=$test_status` | 0 |
| Related session suite, `RecordingPassTest` | `ctest -R '^RecordingPassTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/test-8.log 2>&1; test_status=$?; echo EXIT=$test_status` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/gate-3.log 2>&1; gate_status=$?; echo EXIT=$gate_status` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-201.r1/logs/gate-4.log 2>&1; gate_status=$?; echo EXIT=$gate_status` | 0 |

The final CTest runs executed one test each: `RetroMidiCaptureCommandsTest` **1/1 passed** and
`RecordingPassTest` **1/1 passed**. The RED and reverse-patch runs each executed the same
`RetroMidiCaptureCommandsTest` and failed as expected.

## Files committed

- `docs/MIDI-RETRO-CAPTURE.md`
- `docs/qwen-fixes-2026-10/cluster-new-p8-201.md`
- `include/RetroMidiClipWriter.h`
- `src/core/ControlCommandsMidi.cpp`
- `src/core/ControlCommandsSessionCapture.cpp`
- `src/core/RetroMidiClipWriter.cpp`
- `tests/src/core/RetroMidiCaptureCommandsTest.cpp`
- `tests/upstream-modifications.txt`
