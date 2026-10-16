# Cluster new-p12-200

**Verdict: FIXED**

One retrospective take could label frames recorded at several engine sample
rates with a single rate. `RetroAudioCapture::push()` stored incoming frames
without their sample rate, and the take writer (`writeRetroAudioTake`, driven by
`record.retro_capture_to_take`) labelled the whole window with one rate taken
from `engineSampleRate()`. If the engine's sample rate changed while the capture
stayed armed, the retained window mixed 48 kHz frames and 96 kHz frames but the
written WAV header claimed one rate — playing the older frames back at the wrong
speed and pitch (Luna cluster new-p12-200, BUG 5).

The fix keeps the retained window rate-homogeneous: `push()` now carries the
engine input sample rate (`AudioEngine::inputSampleRate()`). It records the rate
of the frames currently retained in a relaxed atomic; when a period arrives at a
different rate, the stale (pre-change) frames are dropped via `RetroAudioRing::
reset()` so the window holds frames recorded at one sample rate only. `reset()`
touches only atomics, so the producer path stays realtime-safe (no allocation,
no lock, no syscall). `takeWindow()` reports the window's single rate in the new
`Window::sampleRate` field, and the take command labels the file with that rate
instead of the live engine rate.

## Evidence

| Check | Result |
|---|---|
| RED (buggy revert) | `ctest -R '^RetroAudioCaptureTest$'` EXIT=8; new test `aTakeLabelsOneSampleRateEvenAfterAnEngineRateChange` FAILED ("Compared values are not the same"): window kept all 900 mixed-rate frames instead of the 200 post-change frames, and reported no homogeneous rate. The other 10 tests in the family still passed. |
| GREEN (fixed) | `ctest -R '^RetroAudioCaptureTest$'` EXIT=0; 11/11 passed (the 10 pre-existing tests + the new regression test). |
| Bug-catch proof | Same test built without the reset branch: EXIT=8 (FAIL). Rebuilt with the fix: EXIT=0 (PASS). |
| Affected-family test count | 1 CTest target (`RetroAudioCaptureTest`), 11 tests, 11 passed. |
| Complexity gate | EXIT=1 — but the 6 flagged functions are in files I did NOT touch (XyPadPairTest.cpp, DawProjectReadTracks.cpp, ControlCommandsMixerTools.cpp, ControlCommandsMixerStrip.cpp, ProjectRevisions.cpp, ControlCommandsShortcuts.cpp). None of my functions appear in the over-CCN-10 table; my changes add no new complexity regression. This gate was already failing on the branch (baseline-not-written state) before my change. |
| File-length gate | EXIT=1 — the 4 flagged files (RackMacrosTest.cpp, RevisionTimelineTest.cpp, VideoTrackTest.cpp, ArrangerTimelineTest.cpp) are unrelated to my change. My largest file is RetroAudioCaptureTest.cpp at 453 lines (under 500). Pre-existing failure, not introduced by this fix. |
| Files committed | `include/RetroAudioCapture.h`, `src/core/RetroAudioCapture.cpp`, `src/core/AudioEngine.cpp`, `src/core/ControlCommandsRecordingRetro.cpp`, `tests/src/core/RetroAudioCaptureTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p12-200.md`. |

## Commands and exit codes

Build fixed sources (zene + test):

```text
cmake --build build --target zene RetroAudioCaptureTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r1/logs/build-1.log 2>&1; echo EXIT=$?
EXIT=0
```

GREEN run (fixed):

```text
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RetroAudioCaptureTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r1/logs/test-1.log 2>&1; echo EXIT=$?
EXIT=0
```

RED run (reset branch removed from `RetroAudioCapture::push`, code still
compiling at the 3-arg signature):

```text
cmake --build build --target RetroAudioCaptureTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r1/logs/build-buggy.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RetroAudioCaptureTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r1/logs/test-buggy.log 2>&1; echo EXIT=$?
EXIT=8
```

GREEN re-run after restoring the fix:

```text
cmake --build build --target RetroAudioCaptureTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r1/logs/build-2.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RetroAudioCaptureTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r1/logs/test-2.log 2>&1; echo EXIT=$?
EXIT=0
```

The fix.patch (product-only diff) was written to
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r1/logs/fix.patch`;
the bug-catch proof reverted only the behavioral `reset()` branch in
`src/core/RetroAudioCapture.cpp` (the 3-arg `push` signature and `Window::
sampleRate` field are kept so the test still compiles), rebuilt, observed the
FAIL, restored, rebuilt, observed the PASS.

Gates (recorded for completeness; both fail on pre-existing, unrelated files):

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r1/logs/gate-complexity.log 2>&1; echo EXIT=$?
EXIT=1

bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r1/logs/gate-filelength.log 2>&1; echo EXIT=$?
EXIT=1
```

## Notes

- `src/core/AudioEngine.cpp` is an inherited/upstream file; it is recorded in
  `tests/upstream-modifications.txt` with the reason "pass the engine input
  sample rate into RetroAudioCapture::push so a retrospective take is
  rate-homogeneous".
- `include/RetroAudioCapture.h`, `src/core/RetroAudioCapture.cpp` and
  `src/core/ControlCommandsRecordingRetro.cpp` are product-new files (already in
  `tests/fork-sources.txt`), so they are not listed in upstream-modifications.txt.
- The `/tmp/opencode/RetroAudioCapture.fixed.cpp` scratch copy was used only for
  the bug-catch proof and is not part of the commit.
