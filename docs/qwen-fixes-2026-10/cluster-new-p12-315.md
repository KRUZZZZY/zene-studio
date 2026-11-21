# Cluster new-p12-315

## Verdict
FIXED

## Summary
Audio playback (`AudioFileProcessor::playNoteImpl`) previously emitted `isPlaying` directly from the render thread on every period. A signal delivered to a GUI slot from the render thread is queued by Qt, which allocates and accumulates when the GUI falls behind. The render thread now publishes the playhead to an atomic (`m_playheadFrame`), and the GUI poll timer reads/coalesces it before emitting `isPlaying` (only when the value changed, so each period is a bounded, lock-free store).

This round addresses Luna's rework verdict:
- Removed the three redundant alias `QTimer`s in the view; only the single
  `audioFileProcessorPlayheadPoll` timer (20 ms) remains.
- Added a deterministic, registered regression test that exercises the render
  path: it fails on the original synchronous `isPlaying` emission and passes
  when the GUI poll delivers the coalesced value.

## Evidence
- `plugins/AudioFileProcessor/AudioFileProcessor.cpp:161-175` calls `publishPlayhead(...)` instead of `emit isPlaying(...)` on the render path.
- `plugins/AudioFileProcessor/AudioFileProcessor.cpp:186-198` implements `publishPlayhead` / `pollPlayhead`: the render thread stores an atomic; `pollPlayhead` (now `Q_INVOKABLE`) emits `isPlaying` only when the published frame differs from the last GUI-emitted value (coalescing).
- `plugins/AudioFileProcessor/AudioFileProcessor.h` adds `std::atomic<f_cnt_t> m_playheadFrame`, `f_cnt_t m_playheadShown`, `publishPlayhead`, `pollPlayhead`, and `friend class gui::AudioFileProcessorView`.
- `plugins/AudioFileProcessor/AudioFileProcessorView.cpp:145-150` creates exactly one GUI-thread `QTimer` (`audioFileProcessorPlayheadPoll`, 20 ms) invoking `pollPlayhead`. The three alias timers from the prior attempt are gone.
- `tests/src/core/AudioFileProcessorPlayheadTest.cpp` (new, registered in `tests/CMakeLists.txt` and `tests/fork-sources.txt`) plays a real note through the `audiofileprocessor` plugin and asserts: (a) the render path does NOT emit `isPlaying` synchronously, (b) the GUI poll delivers the coalesced value exactly once per change, (c) a repeat poll with no new frame emits nothing.

## Proof (regression test catches the bug)
The committed test `AudioFileProcessorPlayheadTest` was built and run both ways.

RED (buggy code — product files reverted to pre-fix baseline `a7e1b4881~1`):
- Command: `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^AudioFileProcessorPlayheadTest$' --output-on-failure`
- Exit code: `8`
- Failure: `FAIL! ... Compared values are not the same` at the `QCOMPARE(spy.count(), 0)` assertion — the original render path emitted `isPlaying` synchronously during `playNote`, so the spy already saw one signal (count 1, expected 0).

GREEN (fixed code — product files restored):
- Command: `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^AudioFileProcessorPlayheadTest$' --output-on-failure`
- Exit code: `0` (`1/1 Test ... Passed`; `100% tests passed`)

Reproduction of the fix catching the bug (step 4b):
- Save patch of product files: `git diff a7e1b4881~1 -- plugins/AudioFileProcessor/AudioFileProcessor.cpp plugins/AudioFileProcessor/AudioFileProcessor.h plugins/AudioFileProcessor/AudioFileProcessorView.cpp > .../fix.patch`
- Revert product files: `git apply -R .../fix.patch`, rebuild `audiofileprocessor` + test, run ctest → exit code `8`
- Restore: `git apply .../fix.patch`, rebuild, run ctest → exit code `0`

## Gates
- `bash tests/complexity-gate.sh --check`: exit code `1`. No AudioFileProcessor function is newly flagged; the failures are pre-existing violations in unrelated files (e.g. `RackMacrosTest.cpp`, `RevisionTimelineTest.cpp`, `VideoTrackTest.cpp`, `ArrangerTimelineTest.cpp`) not touched by this cluster.
- `bash tests/file-length-gate.sh --check`: exit code `1`. The new test file is 173 lines (under the 500-line limit); failures are pre-existing over-length files unrelated to this cluster.

## Affected-family tests
`ctest -R 'AudioFileProcessor'` (run from `build/tests`) executes 2 tests, both PASS:
- `AudioFileProcessorWaveViewTest` (unchanged behavior, still passes)
- `AudioFileProcessorPlayheadTest` (new regression test)

## Files committed
- `plugins/AudioFileProcessor/AudioFileProcessor.h`
- `plugins/AudioFileProcessor/AudioFileProcessorView.cpp`
- `tests/src/core/AudioFileProcessorPlayheadTest.cpp` (new)
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-315.md`
