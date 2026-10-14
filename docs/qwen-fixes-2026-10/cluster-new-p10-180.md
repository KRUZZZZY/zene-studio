# Cluster new-p10-180: clip controller MIDI output channel

**Verdict: FIXED**

The finding is real. `controllerEvent` in `src/tracks/InstrumentTrackNotes.cpp:89-98` now builds every lane event on the track's zero-based MIDI output channel. `InstrumentTrack::playClipControllers` passes `midiPort()->realOutputChannel()` at line 130. The default output selection (`--`) still resolves to channel 0.

The regression at `tests/src/core/NotePlaybackTest.cpp:268-279` creates a CC lane, selects output channel 6, plays the lane, and checks that the MIDI port writes the event. Before the fix the assertion failed with actual 0, expected 1. Reversing the production patch and rebuilding reproduced the failure; restoring it and rebuilding passed.

The affected ctest family ran 1/1 test (`NotePlaybackTest`) on each invocation; the final run passed. `fff grep` was unavailable in the provided tools and shell, so the logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Build and test evidence

All build and test output was written to new files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs`.

| Command | Log | Exit |
| --- | --- | ---: |
| `cmake --build build --target NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/build-1.log 2>&1` | `build-1.log` | 2 (initial test setup used the const lane accessor; corrected to `setControllerPoints`) |
| `cmake --build build --target NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/build-2.log 2>&1` | `build-2.log` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/build-3.log 2>&1` | `build-3.log` | 0 |
| `ctest -R NotePlaybackTest --output-on-failure` (from `build/tests`) | `test-1.log` (RED) | 8 |
| `cmake --build build --target NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/build-4.log 2>&1` | `build-4.log` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/build-5.log 2>&1` | `build-5.log` | 0 |
| `ctest -R NotePlaybackTest --output-on-failure` (from `build/tests`) | `test-2.log` (GREEN) | 0 |
| `git diff -- src/tracks/InstrumentTrackNotes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/fix.patch` | reverse-patch proof | 0 |
| `cmake --build build --target NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/build-6.log 2>&1` | `build-6.log` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/build-7.log 2>&1` | `build-7.log` | 0 |
| `ctest -R NotePlaybackTest --output-on-failure` (from `build/tests`, production patch reversed) | `test-3.log` (RED) | 8 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/fix.patch` | restore patch | 0 |
| `cmake --build build --target NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/build-8.log 2>&1` | `build-8.log` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/build-9.log 2>&1` | `build-9.log` | 0 |
| `ctest -R NotePlaybackTest --output-on-failure` (from `build/tests`) | `test-4.log` (GREEN) | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/gate-1.log 2>&1` | `gate-1.log` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-180.r1/logs/gate-2.log 2>&1` | `gate-2.log` | 0 |

## Files in the commit

- `src/tracks/InstrumentTrackNotes.cpp`
- `tests/src/core/NotePlaybackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-180.md`
