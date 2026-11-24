FIXED

# Cluster new-p12-218: clips deferred by the 64-start cap lose their opening audio

## Verdict and evidence

REAL. `SampleTrack::play` collected the clips that start in a pass into fixed storage of 64
(`std::array<ScheduledClip, 64>`) and, when that storage was full, skipped the clip for this pass
(`s_deferredClipStarts.fetch_add(1)` in the `else` of `if (clipCount < clips.size())`). It left the
clip's playing latch clear, so the clip was "retried" on a later pass, and that later pass derived its
window from the later transport position: `prefades::window(clip, _start)` / `audioloop::window(clip,
_start)` use `_start` for `window.sourceIn`, so every frame between the pass that entered the clip's
span and the pass that finally scheduled it was never rendered. A clip whose span ended before that
next pass was never scheduled at all.

- Base code: `src/tracks/SampleTrack.cpp:180` `constexpr std::size_t MaxScheduledClipsPerPass = 64;`,
  the `else { s_deferredClipStarts.fetch_add( 1, ... ); }` under the cap check, and the final loop that
  built the handles from the collected descriptors.
- The deferral was deliberate bookkeeping (it had a counter and a test) but the audio loss was not: it
  was counted, never repaired. This is not one of the "by design" entries in the job brief, and the
  branch's own record (`docs/m0-blocker-fixes-2026-10-08/A-recording.md`, "The per-pass start cap
  defers the extra starts and counts them") states the deferral without claiming the loss is intended.
- The cap is per track and per pass (`Song::process` calls `track->play` once per tick,
  `src/core/Song.cpp:687`), so the loss is one scheduling step per deferred clip - about a tick of the
  clip's opening, and total loss for a clip shorter than the gap.

## Fix

`src/tracks/SampleTrack.cpp`:

- The fixed array is a BATCH, not a cap (`ScheduledClipBatchSize`, `:184`). The handle-creation loop is
  now `flushScheduledClips` (`:191`): it schedules the collected descriptors and empties the batch.
- When the batch fills inside the arrangement loop, the pass flushes it and keeps collecting
  (`:308`), so the clip that would have been deferred starts in this pass with the window this pass
  derived for it. The final flush is at `:325`.
- Storage stays fixed (no allocation, no growth, same stack footprint); the number of starts per pass is
  no longer bounded, which is the point.
- `s_deferredClipStarts` (`:114`) stays for `record.get_state`'s `deferred_clip_starts` field and now
  always reports 0, with the comments updated in `src/tracks/SampleTrack.cpp:112`,
  `include/SampleTrack.h:58` and `src/core/ControlCommandsRecordingRoutes.cpp:161`. No command was added
  or removed; `record.get_state`'s field set and the commands snapshot are unchanged.

## Test matrix

| Case | Where it is covered |
|---|---|
| Normal path (clips that fit in one batch) | Existing cases in `RecordingRealtimeTest` (63 cases, all pass); `mutedSampleClipSchedulingDoesNotAllocate` still measures 0 allocations through `RealtimeScope` with the flush in the code |
| Exhaustion (more starts than one batch) | `clipStartsBeyondOneBatchAllStartInTheSamePass`: 65 clips at tick 0 -> `deferredClipStarts` +0, all 65 playing, one handle per clip after one rendered period, all handles equal length (no truncated window) |
| Failure path with the bug | Same test with the product change reverted: `deferredClipStarts` +1 and the last clip's latch clear (log 018) |
| Clip shorter than one scheduling step | `shortClipBeyondOneBatchIsNotMissed`: the 65th clip is 1 tick long; it must start in the pass that enters its span, because the next pass is already past its span |
| Retry path (a later pass must not restart a started clip) | `clipStartsBeyondOneBatchAllStartInTheSamePass`, second pass at tick 1: deferred stays 0 and the handle count stays 65 (no clip started twice) |
| Reconfiguration path | None: the fix changes no model, no saved attribute and no command argument. Nothing in save/load, undo or the control surface is touched |
| Audio-thread constraints | The flush runs on the audio thread with the same code it had before (local `std::array`, no container growth, no lock, no log, no Qt call). The allocation probe case above still reports 0 allocations |
| Supported platforms | Plain C++20 (`std::array`, a reference-capturing lambda); no platform API, no Qt-6-only call, nothing new in the header's ABI |

Determinism: time is the transport tick handed to `track->play` (injected `TimePos(0)` / `TimePos(1)`);
no sleeps, no wall clock, no threads. Handle enumeration is `AudioEngine::playHandles()` after one
explicit `renderNextPeriod()`, and the track is owned by a `std::unique_ptr`, so a failed check still
deletes it and cannot leak handles into later cases (an earlier draft leaked them and perturbed three
audio-ratio tests in the same file; the leakage is gone in logs 011 onward, where only my two cases
fail).

## Commands and exit codes

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-218.r1/logs/`. Each EXIT is the value
printed by the shell right after the command.

| Command | Log | EXIT |
|---|---|---|
| `cmake --build build --target RecordingRealtimeTest -j4` (baseline, before any edit) | 001-build-baseline.log | 0 |
| `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure` (baseline) | 002-test-baseline.log | 0 |
| `cmake --build build --target RecordingRealtimeTest -j4` (tests added, no fix) | 010-build-red3.log | 0 |
| `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure` (RED) | 011-test-red3.log | 8 |
| `cmake --build build --target zene RecordingRealtimeTest -j4` (fix) | 012-build-green.log | 0 |
| `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure` (GREEN) | 013-test-green.log | 0 |
| `cmake --build build --target SampleClipWindowTest ClipFadesRenderTest AudioClipLoopTest TakeLaneCompPlaybackTest SessionPlaybackRenderTest ControlRecordingRoutesTest -j4` | 015-build-family.log | 0 |
| `cd build/tests && ctest -R "^(SampleClipWindowTest\|ClipFadesRenderTest\|AudioClipLoopTest\|TakeLaneCompPlaybackTest\|SessionPlaybackRenderTest\|ControlRecordingRoutesTest)$" --output-on-failure` | 016-test-family.log | 0 |
| `git diff -- src/tracks/SampleTrack.cpp include/SampleTrack.h src/core/ControlCommandsRecordingRoutes.cpp > .../logs/fix.patch` | fix.patch (187 lines) | 0 |
| `git apply -R .../logs/fix.patch` | shell EXIT | 0 |
| `cmake --build build --target zene RecordingRealtimeTest -j4` (reverted) | 017-build-reverted.log | 0 |
| `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure` (reverted: must FAIL) | 018-test-reverted.log | 8 |
| `git apply .../logs/fix.patch` | shell EXIT | 0 |
| `cmake --build build --target zene RecordingRealtimeTest -j4` (restored) | 019-build-reapplied.log | 0 |
| `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure` (restored: must PASS) | 020-test-reapplied.log | 0 |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 021-feature-check.log | 0 |

RED (011): `Totals: 61 passed, 2 failed` - `clipStartsBeyondOneBatchAllStartInTheSamePass`
(`deferredClipStarts` Actual 1, Expected 0; line 961) and `shortClipBeyondOneBatchIsNotMissed`
(`shortClip->isPlaying()` returned FALSE; line 1008). The same two fail in 018 with the patch reverted,
so the test is red on the unfixed product code both before the fix and through the reverse-patch proof.

GREEN (013, 020): `ctest` `1/1 Test #237: RecordingRealtimeTest ... Passed`, `100% tests passed`,
exit 0 - 63 cases: the 61 that the baseline ran green (log 002) plus my two.

Family (016): `100% tests passed out of 6` - SampleClipWindowTest, ClipFadesRenderTest,
AudioClipLoopTest, TakeLaneCompPlaybackTest, SessionPlaybackRenderTest, ControlRecordingRoutesTest.

Gates (021):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted:
  `git apply -R .../fix.patch` EXIT 0, build 017 EXIT 0, `ctest -R RecordingRealtimeTest` 018 EXIT 8
  (`61 passed, 2 failed`, both new cases). Restored: `git apply .../fix.patch` EXIT 0, build 019 EXIT 0,
  `ctest` 020 EXIT 0. RED before the fix was ever written (011) shows the same two failures.
- **Deterministic.** No sleeps, no wall clock, no threads, no races. The seam is the transport position
  passed to `track->play` (`TimePos(0)` for the first pass, `TimePos(1)` for the second) plus one
  explicit `AudioEngine::renderNextPeriod()` to move staged handles into `playHandles()` where
  `SamplePlayHandle::totalFrames()` can be read. Clip count (65 = 64 + 1), clip lengths
  (`song->ticksPerBar()` and 1 tick) and the audio buffers are fixed values.
- **Fix at the cause, callers and siblings checked.** `SampleTrack::play` is the only site with the
  cap; `rg -n "PerPass|per-pass"` finds no other scheduler (`src/tracks/InstrumentTrack.cpp` and
  `PatternTrack` take different paths and were not touched). Inside the function: the single-clip path
  (`_clip_num >= 0`, including the pattern-store window override) pushes at most one descriptor and is
  flushed by the same final call at `:325`; the frozen-take path returns before any collection; every
  early return happens with zero collected clips. The header declaration and definition of
  `deferredClipStarts` still agree (`include/SampleTrack.h:60`, `src/tracks/SampleTrack.cpp:135`) and the
  value is unchanged in type and meaning for `record.get_state`.
- **No collateral damage.** No save/load attribute, no model default, no undo step, no command
  argument changes, so old projects load as before. The value the fix removes is a silent drop, not a
  limit other callers rely on: nothing else reads `clipCount` or the array outside this function. The
  63-case RecordingRealtimeTest (which includes the record, take-lane, comp and allocation-probe cases)
  and the six-file family above all pass.
- **Realtime.** `flushScheduledClips` is the pre-existing handle-creation loop, moved: local
  `std::array`, `PlayHandle` construction and `AudioEngine::addPlayHandle` exactly as before (both were
  already on this path), no lock, no log, no allocation of my own, no Qt call, no signal. The one
  behavioural difference on the audio thread is that a pass with more than 64 starts now creates the
  handles it used to drop, which is the fix. `mutedSampleClipSchedulingDoesNotAllocate` still measures
  `tlAllocationCount == 0` under `RealtimeScope`, and `sampleTrackPlayingChangeIsDeliveredByGuiPoll`
  (the `playingChanged` delivery) still passes.
- **Undo and commands.** No control command added, none removed, no reversibility class changed; the
  commands snapshot is untouched, which is why `ControlCommandsSnapshot` is not in the family run above
  (it was not rebuilt). `record.get_state` still reports `deferred_clip_starts`, now 0.
- **Sibling commands in the changed command files.** `submit_check.py` lists `app.*` and
  `arrangement.*`; those come from the branch-wide `base..HEAD` range (the train merges, 433 files), not
  from this job's own commit (the one this report arrives in), whose five paths are `src/tracks/SampleTrack.cpp`,
  `include/SampleTrack.h`, `src/core/ControlCommandsRecordingRoutes.cpp`,
  `tests/src/core/RecordingRealtimeTest.cpp` and this report. The one command file my commit touches is
  `src/core/ControlCommandsRecordingRoutes.cpp`, changed in a comment only: the `record.*` commands it
  serves are untouched - `record.get_state`'s field set, ordering and types are identical, and the
  `deferred_clip_starts` counter it names now always reports 0 because nothing is deferred. No command
  id was added or removed, so the commands snapshot does not move.
- **State sequence driven by the test** (for the recording-routes file):
  pass at tick 0 -> `deferred_clip_starts` +0, 65 of 65 clips playing, 65 handles, all equal length;
  pass at tick 1 -> still 65 playing, still +0 deferred, still 65 handles (nothing restarted).
- **Edge cases tried.** A clip shorter than the gap between passes (missed entirely before the fix); a
  second pass that must not restart a started clip; 65 clips on one track; the muted-clip scheduling
  path (allocation probe).

## Other findings

- `docs/m0-blocker-fixes-2026-10-08/A-recording.md` still describes "The per-pass start cap defers the
  extra starts and counts them". That is the historical record of that batch; left as written, noted
  here rather than rewritten.
- The base drop path for a play handle the engine cannot queue (`AudioEngine::addPlayHandle` returning
  false because of `criticalXRuns()` or a full list) is unchecked in `SampleTrack::play`: the handle is
  disposed by the engine, so nothing leaks, but the clip's latch stays set and the start is silent and
  uncounted. Pre-existing, not touched here.
