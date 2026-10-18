# Cluster new-p12-316 — NEEDS-CONTEXT

## Finding

"Editing sample controls races with playback" — `plugins/AudioFileProcessor/AudioFileProcessor.cpp:82-90, 117-119, 330-344, 417-426`. UI model `dataChanged()` signals are connected with `Qt::DirectConnection` to slots that mutate sample state and plain playback-position fields, while `playNoteImpl()` reads/writes the same state on the audio thread.

## Verdict: REAL bug, but cannot be deterministically verified here (concurrency / tooling)

The data race is genuine and the minimal targeted fix is clear, but a *regression test* for a data race cannot be written as a normal `ctest` (it needs ThreadSanitizer), and the task rules forbid configuring a sanitizer build ("Do not configure"). I therefore do not ship an unverified change. Details and the recommended fix below.

## Evidence

| Check | Command and result |
|---|---|
| Plain (non-atomic) playback fields | `rg -n "f_cnt_t m_nextPlayStartPoint\|bool m_nextPlayBackwards" plugins/AudioFileProcessor/AudioFileProcessor.h` — exit 0: lines 112-113 declare `f_cnt_t m_nextPlayStartPoint;` and `bool m_nextPlayBackwards;`, both plain (not `std::atomic`). |
| Sample's own fields already atomic | `rg -n "std::atomic" include/Sample.h` — exit 0: `m_startFrame`, `m_endFrame`, `m_loopStartFrame`, `m_loopEndFrame`, `m_amplification`, `m_frequency`, `m_reversed` are all `std::atomic<…>` (lines 131-137). So individual sample-field loads/stores are already synchronized at field granularity. |
| GUI-thread writers of the plain fields | `rg -n "m_nextPlayStartPoint\|m_nextPlayBackwards" plugins/AudioFileProcessor/AudioFileProcessor.cpp` — exit 0: written by `reverseModelChanged` (333-334), `stutterModelChanged` (350-351), `pointChanged` (423-424), and by `setAudioFile` which also does a wholesale `m_sample = Sample(...)` (line 320). These slots run on the GUI thread because the `connect(..., Qt::DirectConnection)` at lines 82-93 makes the slot execute in the signal emitter's (GUI) thread. |
| Audio-thread readers/writers | `playNoteImpl` (audio thread) reads/writes `m_nextPlayStartPoint`/`m_nextPlayBackwards` at 117-118, 124, 128-129, 147-148, 178-179; `beatLen` reads them at 291-293. |
| Whole-sample assignment race | `setAudioFile` does `m_sample = Sample(SampleBuffer::fromFile(_audio_file));` (line 320) on the GUI thread while the audio thread reads `m_sample` (e.g. `m_sample.startFrame()`, `m_sample.endFrame()`, `m_sample.play(...)` in `playNoteImpl`). Copying the `Sample` struct / swapping its `std::shared_ptr<const SampleBuffer> m_buffer` concurrently with a read is a second, larger race the finding's "sample updates" clause names. |
| Existing test family still green | `cmake --build build --target zene AudioFileProcessorWaveViewTest` — exit 0; `( cd build/tests && ctest -R '^AudioFileProcessorWaveViewTest$' --output-on-failure … )` — exit 0, `1/1 Test #259: AudioFileProcessorWaveViewTest … Passed`. The existing family does not exercise the cross-thread race. |
| Complexity gate (clean tree) | `bash tests/complexity-gate.sh --check` — exit 1. **Pre-existing failure, unrelated to this finding** (e.g. `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp` CCN 16; several other functions already over CCN 10 in the baseline). |
| File-length gate (clean tree) | `bash tests/file-length-gate.sh --check` — exit 1. **Pre-existing failure, unrelated to this finding** (multiple files already >500 lines, e.g. `src/core/DeviceDspTest.cpp` 752, `tests/src/gui/ArrangerTimelineTest.cpp` 689). |
| RED regression for the bug | Not runnable: a data race has no deterministic assertion under a normal (non-TSan) build. |
| GREEN regression for the bug | Not runnable for the same reason; requires a ThreadSanitizer build, which the task rules forbid configuring. |

## Why this is NEEDS-CONTEXT (not FIXED)

1. **Verification tooling is unavailable.** The required proof-of-regression (test fails without the fix, passes with it) is impossible for a data race without ThreadSanitizer. There is no TSan ctest in this build tree, and the rules say "Do not configure" a new build. Per the procedure's step 7 ("If you are stuck … concurrency question … report NEEDS-CONTEXT or BLOCKED — never guess a fix or fake a result"), I do not ship an unverified change.
2. **The "sample updates" half needs a design decision.** The minimal, behaviour-preserving, realtime-safe fix for the *plain playback-position fields* is to make them `std::atomic<f_cnt_t>` / `std::atomic<bool>` — matching `Sample`'s established pattern (lock-free, no allocation, no mutex, no logging; safe on the audio path). But the finding also names "sample updates": the wholesale `m_sample = Sample(...)` in `setAudioFile` is a concurrent struct copy that a field-level atomic does not cover. Fully closing that race requires a thread-safe handoff for the `Sample` itself (e.g. an atomic `std::shared_ptr` / pointer indirection swapped at a render-safe boundary), which is a design-bearing change the finding leaves open ("Apply control changes at a render-safe boundary or use a thread-safe handoff").

## Recommended fix (for the next agent, to verify under TSan)

- In `AudioFileProcessor.h:112-113`: change `f_cnt_t m_nextPlayStartPoint;` → `std::atomic<f_cnt_t> m_nextPlayStartPoint;` and `bool m_nextPlayBackwards;` → `std::atomic<bool> m_nextPlayBackwards;`. Adjust the reads/writes in `AudioFileProcessor.cpp` (lines 117-118, 124, 128-129, 147-148, 178-179, 291-293, 333-334, 350-351, 423-424) to use `.load()`/`.store(..., std::memory_order_relaxed)` — matching `Sample`'s relaxed ordering. This is behaviour-preserving and realtime-safe.
- For the `m_sample` assignment race: introduce an atomic handle for the `Sample` (e.g. `std::atomic<std::shared_ptr<Sample>>` or a pointer swapped under a render-safe boundary) and have `playNoteImpl`/`beatLen` read through it; `setAudioFile` publishes the new `Sample` via the atomic store. Confirm under ThreadSanitizer that no race remains on the audio thread.
- Register any production change in `tests/upstream-modifications.txt` with a reason; no new file, no new control command.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-316.md`

No production code was changed: the bug is real but its verification needs ThreadSanitizer tooling (forbidden to configure here) and the sample-handoff portion needs a design decision, so shipping an unverified change would violate the "never guess a fix or fake a result" rule.
