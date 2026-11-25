FIXED

# Cluster 3503: pattern playback can schedule a reversed sample window

3503: FIXED

## Verdict and evidence

REAL, but narrower than the finding states.

- **What is real.** `SampleTrack::play`, `_clip_num >= 0` branch, pattern-store case
  (`src/tracks/SampleTrack.cpp:253-264` before the fix): the pass computed
  `sampleStart = framesPerTick * _start` and scheduled a `SamplePlayHandle` with the window
  `{ sampleStart, sClip->sampleWindow().sourceOut }` even when `sampleStart >= sourceOut`, an
  empty or inverted window. It also latched `sClip->setIsPlaying(true)` and the track's
  `setPlaying(true)`, and returned true. The `setPlaying(false)` for `_start > sClip->length()`
  (line 245-248) was overwritten by line 263 in the same pass. The window contract is
  "sourceOut > sourceIn" (`docs/CLIP-CAPTURE-DESIGN.md` §2.2; `include/SampleWindow.h:75-76`
  `empty()`), and the arrangement branch already refuses such a window
  (`src/tracks/SampleTrack.cpp:302`, `windowStart < windowEnd || sClip->isRecord()`).
  Downstream, the inverted window reaches `std::clamp(frame, m_window.sourceIn, m_window.sourceOut)`
  in `SamplePlayHandle::warpRatio()` (`src/core/SamplePlayHandle.cpp:686-687`), which is undefined
  behaviour when lo > hi. For a warped clip it also reaches `SampleClip::windowTicksFor()`, which
  takes a warp-map difference with sourceOut < sourceIn (`src/core/SampleClip.cpp:659-660`).
- **What is not real.** The "unsigned `window.length()` wraps" claim:
  `SampleWindow::length()` returns 0 for `sourceOut <= sourceIn`
  (`include/SampleWindow.h:73`). The "preserving-pitch renderer forms a pointer beyond the
  buffer" claim: `renderPreservingPitch` zero-fills and returns when `windowFrames == 0`, before
  it forms `source + sourceIn` (`src/core/SamplePlayHandle.cpp:741-747`). A linear handle's
  `totalFrames()` is `m_window.length() * ratio` = 0 (`src/core/SamplePlayHandle.cpp:877`), so
  `play()` returns at line 573 before it renders.

## The fix (`src/tracks/SampleTrack.cpp:253-269`)

The pattern branch now derives the pass's window before it schedules anything. If that window
is empty (`SampleWindow::empty()`), it returns false: no handle, no clip latch, no track latch.
A record-armed clip is excepted, exactly as at line 302, because its take does not render the
window. Otherwise behaviour is unchanged: the same window, the same `pattern_track`, the same
latches, and the clip is added to the batch after them. The arrangement branch, the
non-pattern-store single-clip branch and the frozen-take path are untouched.

- **Siblings.** The arrangement branch (line 302) already applies the same rule. The
  non-pattern-store `_clip_num >= 0` case schedules the authored window, which
  `SampleWindow::clamped` keeps well formed. `SampleClip::sampleWindow()` is its only writer
  (`include/SampleWindow.h:43-45`), so it cannot be inverted.
- **Realtime.** The change only removes work on the audio thread (one fewer `new SamplePlayHandle`
  in the skipped case). It adds no allocation, lock, wait or log.
- **Collateral.** No saved data, undo step or control command is involved. Only a pass with
  nothing to play changes.

## Regression test

`tests/src/tracks/SamplePatternPassWindowTest.cpp`, slot `patternPassPastTheWindowSchedulesNothing`.
It is a new target: added to `SampleClipWindowTest.cpp`, the slot pushed that file over the
file-length gate (gates-1.log: "new file over 500 lines: tests/src/tracks/SampleClipWindowTest.cpp
(532)"), as `SampleClipMuteCursorTest` did before it. The test plays a trimmed clip on a
`SampleTrack` in the pattern store:
- A control pass at tick 0 returns true and latches both flags.
- A pass at the first tick whose start frame (computed as the product computes it) is
  >= sourceOut, and a pass 100 ticks later, must return false and leave both latches clear.

No timing is involved; the `qWait` in `drainPlayHandles` is teardown only, copied from
`SampleClipWindowTest`.

## Commands and exit codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/3503.r1/logs)

| Step | Command | EXIT | Log |
|---|---|---|---|
| RED (slot first in SampleClipWindowTest, no fix) | `cmake --build build --target zene SampleClipWindowTest -j4` | 0 | build-2.log |
| | `cd build/tests && ctest -R SampleClipWindowTest --output-on-failure` | 8 | test-red-1.log (FAIL at `!track.play(TimePos(boundary), kPeriod, 0, 0)`) |
| GREEN | `cmake --build build --target zene SampleClipWindowTest -j4` | 0 | build-3-green.log |
| | `ctest -R SampleClipWindowTest --output-on-failure` | 0 | test-green-1.log |
| Family | `ctest -R "SampleClip\|SampleTrack\|TakeLaneCompPlayback" --output-on-failure` | 0 | test-family-1.log (100% tests passed out of 11) |
| Moved to own target | `cmake --build build --target zene SamplePatternPassWindowTest SampleClipWindowTest -j4` | 0 | build-7b.log (first try EXIT 2: stale Makefile before reconfigure, build-7.log) |
| | `ctest -R "SamplePatternPassWindowTest\|SampleClipWindowTest" --output-on-failure` | 0 | test-green-2.log |
| 4b revert | `git diff -- src/tracks/SampleTrack.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 | |
| | `cmake --build build --target zene SamplePatternPassWindowTest -j4` | 0 | build-8-red.log |
| | `cd build/tests && ctest -R SamplePatternPassWindowTest --output-on-failure` | 8 | test-red-5.log (`661 - SamplePatternPassWindowTest (Failed)`, Loc SamplePatternPassWindowTest.cpp(159)) |
| 4b re-apply | `git apply logs/fix.patch` | 0 | |
| | `cmake --build build --target zene SamplePatternPassWindowTest -j4` | 0 | build-9-green.log |
| | `cd build/tests && ctest -R SamplePatternPassWindowTest --output-on-failure` | 0 | test-green-5.log (100% tests passed out of 1) |
| Gates | `python3 .../qwen/feature_check.py <worktree>` | 0 | gates-3.log (every row PASS) |

The first gates run (gates-1.log, EXIT 1) failed file-length, which is why the test moved. The
second run (gates-2.log, EXIT 1) failed fork-sources and all-sources-reproduce only because the
new file was untracked. `git add -N` fixed that.

`cmake build` (configure-1.log) returned 97: a session guard refused a manual configure. The next
`cmake --build` reconfigured on its own.

Registration: `tests/fork-sources.txt` (new test and this report), `tests/all-sources.txt`,
`tests/CMakeLists.txt`, and `tests/upstream-modifications.txt` (one line each for
`src/tracks/SampleTrack.cpp` and `tests/CMakeLists.txt`).

Delegation: Sonnet (1) wrote the RED test and ran the first build and RED run. Haiku ran the
GREEN build, the family run, the first 4b revert/re-apply proof and the first gates run. Sonnet (2)
moved the test into its own target, registered it, and reran the 4b proof and the gates. I did the
diagnosis, the product fix, the review of each diff, and the report and commit. Note: Sonnet (1)
reported using `awk`/`sed` on named files for lookups because fff was not available to it.

## Self-check

(Filled in after running submit_check.py; see below.)

## Other findings

- In the pattern branch, `sampleStart` is the absolute source frame for `_start` and does not
  add the clip's trimmed `sourceIn`. Upstream LMMS has the same semantics
  (`setSampleStartFrame(sampleStart)`), so a trimmed clip in a pattern plays from source frame 0
  at the pattern's start. That may be intended, so it is left unchanged here.
- `tests/CMakeLists.txt:1754` says Gate 7 measures `SampleClipWindowTest.cpp` at 494 lines. The
  gate measured 481 before this change (532 with the 51-line slot), so the comment is stale.
