FIXED

# Cluster new-p12-222 (port) — a short playback window can overrun into the next source frames

Verdict: **REAL** on this line, fixed at the cause in
`SamplePlayHandle::play()`. This branch is the port of the accepted fix on
`bugs/qwen-new-p12-222` (commit `f09ab48f40`, "clamp a playback period to the
window's remaining frames"), which was written against train-52 and did not
merge here. The clamp is re-applied by hand on top of train-53's tree, together
with its regression, because the sibling cluster new-p12-223 changed the same
function on this line (it added the muted-pass source advance, whose own
clamp-carrying call is one of the paths this fix bounds).

## 1. The defect, with line numbers as they are on this branch

`SamplePlayHandle::play()` bounds the HANDLE but never the PERIOD:

- `src/core/SamplePlayHandle.cpp:570` — `play( std::span<SampleFrame> buffer )`
  starts with one guard, `framesDone() >= totalFrames()` (`:573-578`), which
  zeroes the buffer and returns. After it, `f_cnt_t frames = buffer.size()`
  (`:580`) is the device period with no reference to how much of the window is
  left.
- `src/core/SamplePlayHandle.cpp:888-901` — `totalFrames()` is the snapshotted
  window (`m_window.length() * outputRate/sampleRate`, or `m_timelineFrames`).
  It was consulted only by that opening guard.
- `src/core/Sample.cpp:100` — `Sample::play(dst, state, numFrames, loop, ratio)`
  takes the buffer, the cursor and a ratio. **It is never given the window.**

A non-looping clip hides the overrun: `Sample::render` returns short at the
Sample's `m_endFrame` (which `SampleClip::setSampleWindow` mirrors from the
window) and `Sample::play` zero-fills the rest. It still shows in the handle's
position: `framesDone()` reached the period (256) for a 100-frame window — the
RED log's first failure, below.

A looping clip is audible. `SamplePlayHandle::snapshotAudioLoop`
(`src/core/ClipAudioLoop.cpp:197-210`) sets the loop copy's frame points with
`setAllPointFrames(0, sampleSize, audioLoopStart, audioLoopStart+frames)` and
`m_loopMode = Sample::Loop::On`, so `Sample::render` WRAPS the cursor instead of
returning short, and the frames past the window are source audio. That is the
last period of every looped clip.

## 2. The fix

`src/core/SamplePlayHandle.cpp:598-619` (the clamp itself at `:613-619`), immediately after the first-period
offset block (which has already moved `workingBuffer` and shrunk `frames`):

```cpp
const f_cnt_t remaining = totalFrames() > framesDone() ? totalFrames() - framesDone() : 0;
const f_cnt_t renderFrames = std::min<f_cnt_t>(frames, remaining);
if( renderFrames < frames )
{
	zeroSampleFrames(workingBuffer + renderFrames, frames - renderFrames);
}
frames = renderFrames;
```

`f_cnt_t` is an unsigned 64-bit type (`include/LmmsTypes.h`), so the remaining
count is compared before it is subtracted. `m_frame += frames` (`:693`) now
advances by what was rendered, so `framesDone()` cannot pass `totalFrames()`.
The three render branches (`renderTransientRepeat`, `renderPreservingPitch`,
`Sample::play`) and the muted branch's `advanceSourceSilently(frames)` all take
the clamped `frames`, so every path is bounded by the same clamp. `Sample::render`
is untouched: it is shared with the browser preview and the metronome.

Realtime (I8): two integer compares, a `std::min` and a `std::fill` over frames
the callback already owns. No allocation, no free, no lock, no logging, no Qt
call. No new control command, no serialisation, no undo step.

## 3. Callers and siblings checked (this line)

- `src/tracks/SampleTrack.cpp:149-333` — the live schedule. A mid-clip pass gets
  the sub-window `window` derived at `:294-295` (from
  `audioloop::window` / `prefades::window`) and handed over at `:315`, and `clip->sampleWindow()` mirrors
  it into the Sample's frame fields, so `totalFrames()` is exactly the frames
  left; the clamp can only shorten a period that should have been shortened.
- `src/core/ClipConsolidate.cpp:80-101` — the only offline caller. It already
  computes `length = std::min({span, room, handle.totalFrames()})` (`:86`) and
  copies `std::min(BlockFrames, length - done)` frames from each block (`:93`),
  so nothing it wanted is lost; `ClipConsolidateTest` passes below.
- `PreviewPlayHandle` (`src/core/ControlCommandsBrowserMedia.cpp:163`) and
  `TestTonePlayHandle` (`src/core/ControlCommandsAudioTestTone.cpp:82`) derive
  from `SamplePlayHandle` and use the `Sample*` constructor, whose window is the
  Sample's own start/end frames — a window that spans their callback. `ControlAudioTestTone`
  and `AudioTestToneTest` pass below.
- The muted branch's `advanceSourceSilently()` (new-p12-223) receives the
  clamped count, so a muted last period advances the source by the window's
  remainder, not by the period. `SampleClipMuteCursorTest` passes below.
- No header changed, so no declaration/definition pair is split; `zene` was
  relinked after the change (`logs/build-4-green.log`, EXIT=0).

## 4. Regression test and its matrix

`tests/src/tracks/SampleClipWindowRemainderTest.cpp` (target
`SampleClipWindowRemainderTest`), registered in `tests/CMakeLists.txt:1760-1762`,
`tests/all-sources.txt`, `tests/fork-sources.txt` and — for the CMake list — in
`tests/upstream-modifications.txt`, all in this commit. Its own target because
Gate 7 measures `tests/src/tracks/SampleClipWindowTest.cpp` at 494 of its 500
lines. The source is 0.5 on every frame, so frames past the window cannot pass
as silence.

| path | slot |
|---|---|
| normal path, short window, period longer than it | `aCallbackLongerThanTheWindowRendersOnlyTheWindow` (test file `:125`): the window's 100 frames are the source, every frame past them is exactly zero, `framesDone()` is 100 not 256, `isFinished()` is true |
| failure / the audible case (reconfiguration to Loop::On) | `aCallbackLongerThanALoopedWindowPlaysOnlyTheWindow` (`:175`): a clip looped over source frames [0, 50) inside a 100-frame window; frame 100 onward must be zero, not the wrapped loop |
| reconfiguration: first-period offset | `aShortWindowRendersCorrectlyAfterAFirstPeriodOffset` (`:217`): a 10-frame lead silence, then the window, then zeros — the silenced tail is the settled region, not the period's original length |
| exhaustion | inside slot 1: a further period after the window is finished renders zeros and `framesDone()` stays at 100 |
| audio-thread constraint (I8) | no test slot: the clamp is two compares, a `min` and a `fill` of frames the period already owned, so it has no allocation, lock or call to prove; the family's own allocation slot (`SampleClipMuteCursorTest::aMutedPassAdvancesWithoutAllocating`) still passes below |
| platform | no platform-specific code; the target reuses the existing QtTest infrastructure and no new dependency |

### RED — the test fails without the product change

`logs/test-1-red.log` (built by `logs/build-3.log`, EXIT=0), run from `build/tests`:

```
ctest -R "^SampleClipWindowRemainderTest$" --output-on-failure   EXIT=8
FAIL!  : aCallbackLongerThanTheWindowRendersOnlyTheWindow() framesDone() 256, expected 100
FAIL!  : aCallbackLongerThanALoopedWindowPlaysOnlyTheWindow() the loop played past the window at frame 100: 0.5
FAIL!  : aShortWindowRendersCorrectlyAfterAFirstPeriodOffset() framesDone() 246, expected 100
Totals: 2 passed, 3 failed
```

### GREEN — the test passes with it, and so does the family

`logs/build-4-green.log` EXIT=0; `logs/test-3-green-family.log` EXIT=0,
`100% tests passed out of 14` (`SampleClipWindowTest`, `SampleClipMuteCursorTest`,
`SampleClipWarpTest`, `SampleClipWarpModesTest`, `SampleClipWarpEditTest`,
`SampleClipWarpModeMenuTest`, `SampleClipStretchTest`, `SampleClipSourceSwapTest`,
`SampleClipTransientRepeatTest`, `SampleClipTransientRepeatSafetyTest`,
`SamplePatternPassWindowTest`, `ClipConsolidateTest`, `AudioClipLoopTest`,
`SampleClipWindowRemainderTest`).
`logs/test-6-family2.log` EXIT=0, 7/7 (`AudioTestToneTest`, `ClipGainEnvelopeTest`,
`ClipTransposeTest`, `SampleClipRubberBandTest`, `RecordClipTest`,
`AudioBusPlayHandleQueueTest`, `ControlAudioTestTone` — the last one drives the
rebuilt `zene` binary over the control socket).

### 4b — the test catches the bug (patch reversed and restored)

| step | command | exit |
|---|---|---|
| save the product patch | `git diff -- src/core/SamplePlayHandle.cpp > logs/fix.patch` (33 lines) | 0 |
| reverse it | `git apply -R logs/fix.patch` | 0 |
| rebuild without the fix | `cmake --build build --target SampleClipWindowRemainderTest zene -j4` (`logs/build-9-reverted-final.log`) | 0 |
| run | `ctest -R "^SampleClipWindowRemainderTest$"` (`logs/test-7-red-final.log`) | **8** (3 failed) |
| restore it | `git apply logs/fix.patch` | 0 |
| rebuild with the fix | `cmake --build build --target SampleClipWindowRemainderTest zene -j4` (`logs/build-10-restored-final.log`) | 0 |
| run | `ctest -R "^SampleClipWindowRemainderTest$"` (`logs/test-8-green-final.log`) | **0** (1/1 passed) |

The sequence was run twice: once on the first version of the product comment and
again on the final source, after the comment's line citations were corrected to
this branch's numbers (`ClipAudioLoop.cpp:204-209`, `Sample.cpp:220-226`). The
clamp statements are identical in both; the table above cites the final run,
whose binaries were built from the committed source. The first run is
`logs/build-6-reverted.log` / `logs/test-4-red-reverted.log` (EXIT=8) and
`logs/build-7-restored.log` / `logs/test-5-green-restored.log` (EXIT=0).

## 5. Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`,
unpiped, saved as `logs/gates-2.log` (the final run, with this report in the tree; the
same table on the code-only tree is `logs/gates-1.log`):

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
EXIT=0
```

## 6. Every command, with its exit code

All run from the worktree root unless the directory is stated; all output in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-222-port.r8/logs`.

| # | command | exit |
|---|---|---:|
| 1 | `bash tests/all-sources-reproduce.sh` (`all-sources-1.log`) | 0 |
| 2 | `cmake --build build --target SampleClipWindowRemainderTest -j4` (`build-1.log`) — target did not exist yet: "No rule to make target" | 2 |
| 3 | `cmake -S . -B build` (`configure-1.log`) — refused by the build guard ("configuring is the parent's job") | 97 |
| 4 | `cmake --build build --target zene -j4` (`build-2-regenerate.log`) — re-ran CMake, which created the new test target | 0 |
| 5 | `cmake --build build --target SampleClipWindowRemainderTest -j4` (`build-3.log`) | 0 |
| 6 | `cd build/tests && ctest -R "^SampleClipWindowRemainderTest$" --output-on-failure` (`test-1-red.log`) — RED, 3 failures | 8 |
| 7 | `cmake --build build --target SampleClipWindowRemainderTest zene -j4` (`build-4-green.log`) | 0 |
| 8 | `cd build/tests && ctest -R "SampleClipWindow\|SampleClipMuteCursor\|..." --output-on-failure` (`test-2-green-family.log`) — 13 "Not Run": the family binaries were not built in this tree | 8 |
| 9 | `cmake --build build --target <13 family targets> -j4` (`build-5-family.log`) | 0 |
| 10 | the same `ctest` (`test-3-green-family.log`) — 14/14 passed | 0 |
| 11 | `cmake --build build --target <6 more family targets> -j4` (`build-8-family2.log`) | 0 |
| 12 | `cd build/tests && ctest -R "^(AudioTestToneTest\|...)$"` (`test-6-family2.log`) — 7/7 passed | 0 |
| 13 | the 4b sequence in §4b | 0 / 8 / 0 |
| 14 | `python3 .../feature_check.py <worktree>` (`gates-1.log`) | 0 |

Step 2's exit 2 and step 3's exit 97 are build-system facts, not code failures:
with the Makefile generator a target that CMake has not generated yet cannot be
named, and this session may not configure. Step 4 is how the target came into
existence, and steps 5-14 are the runs that matter.

## Other findings

- `ctest -R` in this build tree finds the family by name, but 13 of the 14
  binaries had never been built, so the first family run reported them as "Not
  Run" rather than building them. Not a product defect; noted because a reader
  of `test-2-green-family.log` will see a failure that the next run clears.
