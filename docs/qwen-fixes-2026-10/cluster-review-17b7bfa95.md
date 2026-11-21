# Cluster review-17b7bfa95 — the same-file batch on `src/core/SampleClip.cpp`

Verdict: **FIXED**

review-17b7bfa95: FIXED
new-p16-205: FIXED
new-p17-158: FIXED

Three bugs, one file, one commit each. The logs of every command quoted below are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-17b7bfa95.r1/logs`.

| cluster | commit | what it was | fix |
| --- | --- | --- | --- |
| new-p16-205 | `b8ef98d65` | a window edit never moved the clip's timeline length | `setSampleWindow()` sets the length to the edited window's mapped duration; a loaded file keeps the length it was saved with |
| new-p17-158 | `dfed7915c` | `timelinePosAt()` divided by a zero rate on the warped branch | the mapping refuses a nonpositive frames-per-tick and answers with the clip's own origin |
| review-17b7bfa95 | `b11a696f1` | a reversed clip's transients were detected in the region it never plays, and the ascending-only quantise kept the first and dropped the rest | the window contract lives in one place (`audibleWindow()`, `playFrameFor()`); the detector looks at the audible range; the quantise walks the transients in play-frame order and writes the play frame |

---

## new-p16-205 — a window edit left the clip's timeline length unchanged

### Verdict: REAL

### Evidence (file:line)

- `src/core/SampleClip.cpp:694` `SampleClip::setSampleWindow()` — the window was
  installed and, for a window that is not the whole buffer, `setAutoResize(false)`
  was called; the clip's stored length was never touched. The length is the only
  thing that says how long the clip is on the timeline, and a window that is
  trimmed in half is half the audio: the clip went on playing its old length
  (half a clip of silence) over half the audio it no longer had.
- `src/core/SampleClip.cpp:468` `SampleClip::updateLength()` — where an
  auto-resizing clip's length IS its window's duration
  (`changeLength(sampleLength())`), the rule the window edit has to keep.
- `src/core/SampleClip.cpp:751` `SampleClip::sourceFrameAt()` / the play handle —
  the clip plays exactly its window, so the length and the window are one quantity.
- `src/core/SampleClip.cpp:894` `SampleClip::loadSettings()` — the load path: the
  file's own `len` is authoritative for a file that carries a window (design
  §2.6), so the length a loaded window moved is put back.
- Regression: `tests/src/tracks/SampleClipWindowTest.cpp`
  `trimmingTheWindowMovesTheClipLength()`.

### Fix

`src/core/SampleClip.cpp` — the length is moved while auto-resize may still be on,
because the moment it goes off below nothing would move it any more:

```cpp
	installWindow(clamped);

	// The clip plays exactly its window, so an edited window is a different amount
	// of audio and the clip's timeline length has to follow it (BUG new-p16-205).
	// ... A clip in the pattern store is the one exception: there the pattern owns
	// the length, exactly as updateLength() has it.
	if (getTrack()->trackContainer() != Engine::patternStore()) { changeLength(sampleLength()); }

	if (m_window != SampleWindow::full(bufferFrames))
	{
		setAutoResize(false);
	}
```

and in `loadSettings()` the file's own length is put back after the window is
applied, so a file that carries a window loads with the length it was saved with
(`changeLength(savedLength)`).

The pattern-store exception mirrors `updateLength()`'s own pattern branch, so the
two agree. No new command, no new signal, no new member; `changeLength()` is the
control-thread path the model already uses.

### Commands and exit codes

RED (the regression on the unfixed tree):

```
$ cmake --build build --target SampleClipWindowTest -j 20 > logs/build-1-red.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^SampleClipWindowTest$' --output-on-failure > logs/test-1-red.log 2>&1
CTEST_EXIT=8
```

```
FAIL!  : SampleClipWindowTest::trimmingTheWindowMovesTheClipLength() 'std::abs(static_cast<int>(clip->length()) - static_cast<int>(clip->sampleLength())) <= 1' returned FALSE. (the length stayed at 896, the window says 448)
Totals: 15 passed, 1 failed, 0 skipped, 0 blacklisted, 2216ms
```

GREEN after the fix:

```
$ cmake --build build --target SampleClipWindowTest -j 20 > logs/build-2-green.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^SampleClipWindowTest$' --output-on-failure > logs/test-2-green.log 2>&1
CTEST_EXIT=0
100% tests passed out of 1
```

Test-proof of the bug (the product change reverted, then restored):

```
$ git diff -- src/core/SampleClip.cpp > logs/fix-p16-205.patch && git apply -R logs/fix-p16-205.patch
REVERTED=0
$ cmake --build build --target SampleClipWindowTest -j 20 > logs/build-3-red-revert.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^SampleClipWindowTest$' --output-on-failure > logs/test-3-red-revert.log 2>&1
CTEST_EXIT=8      # the same assertion: the length stayed at 896, the window says 448
$ cd ../.. && git apply logs/fix-p16-205.patch
REAPPLIED=0
$ cmake --build build --target SampleClipWindowTest -j 20 > logs/build-4-green-reapply.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^SampleClipWindowTest$' --output-on-failure > logs/test-4-green-reapply.log 2>&1
CTEST_EXIT=0
```

---

## new-p17-158 — a zero-rate source produced an invalid timeline position

### Verdict: REAL (the warped branch); the linear branch was already guarded

### Evidence (file:line)

- `src/core/SampleClip.cpp:809` `SampleClip::timelinePosAt()` — no guard for a
  nonpositive `clipFramesPerTick()`, although `windowTicksFor()` and
  `leadOutputFramesAt()` refuse the same value.
- `include/WarpMarkers.h:340` `WarpMarkers::timelineOffsetAt()` — the warped branch
  of the mapping divides by the rate and falls back to `1.0f` when the rate is not
  positive, so a zero-rate clip's frame was placed `frames / 1` ticks away from
  the clip: the regression below measures **-252 ticks for a 1024 frame source**,
  i.e. a position hundreds of ticks outside the clip that owns the frame (and, for
  a long source, past the range of `tick_t`).
- `include/TimePos.h:135` `TimePos::fromFrames()` — the LINEAR branch was already
  safe: it returns `TimePos(0)` for a nonpositive rate, which is why the clip's
  origin was the answer there. The warped branch had no such guard, which is the
  asymmetry the finding names.
- `src/core/SampleClip.cpp:345` `SampleClip::setSampleBuffer()` — the way a
  zero-rate `SampleBuffer` reaches a clip (a project load refuses a stated rate
  that is not positive — `new-p12-204` — but a buffer handed to the clip in
  process is not checked).
- Regression: `tests/src/tracks/SampleClipWindowTest.cpp`
  `aZeroRateSourceMapsToTheClipOrigin()`.

### Fix

`src/core/SampleClip.cpp` — the mapping refuses the rate and answers with the
clip's own origin, the same defined fallback the linear branch already gives:

```cpp
	// BUG new-p17-158: a source whose rate is zero has no frames per tick, so
	// there is nothing to divide by (windowTicksFor() and leadOutputFramesAt()
	// refuse the same rate). ...
	if (!(framesPerTick > 0.0f)) { return TimePos(originTicks); }
```

The linear branch's answer is unchanged (`TimePos::fromFrames()` returned
`TimePos(0)` there, and `originTicks + 0` is the origin); only the warped branch's
wild position is gone. No allocation, no lock, no I/O: the mapping is a pure
function of the clip's own state and runs on the audio thread.

### Commands and exit codes

The first GREEN attempt did not compile (an unused local in the new test), which
is worth recording: `logs/build-8-p17-158-green.log`, `BUILD_EXIT=2`,
`unused variable 'rate'`. The variable was dropped and the run repeated.

```
$ cmake --build build --target SampleClipWindowTest -j 20 > logs/build-9-p17-158-green.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^SampleClipWindowTest$' --output-on-failure > logs/test-9-p17-158-green.log 2>&1
CTEST_EXIT=0
100% tests passed out of 1
```

Test-proof of the bug (the product change reverted, then restored):

```
$ git diff -- src/core/SampleClip.cpp > logs/fix-p17-158.patch && git apply -R logs/fix-p17-158.patch
REVERTED=OK
$ cmake --build build --target SampleClipWindowTest -j 20 > logs/build-10-p17-158-red.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^SampleClipWindowTest$' --output-on-failure > logs/test-10-p17-158-red.log 2>&1
CTEST_EXIT=8
```

```
FAIL!  : SampleClipWindowTest::aZeroRateSourceMapsToTheClipOrigin() Compared values are not the same
   Actual   (clip->timelinePosAt(0).getTicks()): -252
   Expected (origin)                           : 0
   Loc: [../../tests/src/tracks/SampleClipWindowTest.cpp(600)]
Totals: 16 passed, 1 failed, 0 skipped, 0 blacklisted, 1725ms
```

```
$ cd ../.. && git apply logs/fix-p17-158.patch
REAPPLIED=OK
$ cmake --build build --target SampleClipWindowTest -j 20 > logs/build-11-p17-158-green2.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^SampleClipWindowTest$' --output-on-failure > logs/test-11-p17-158-green2.log 2>&1
CTEST_EXIT=0
100% tests passed out of 1
```

---

## review-17b7bfa95 — a reversed clip's transients underflowed, reversed and were dropped

### Verdict: REAL (the transient half); the underflow and the asymmetric-trim mirror were already fixed in this tree

The finding names three things. Two of them were already fixed in this branch by
commit `09674ea8a` ("fix(rework 17b7bfa95): reversed transients mapped by the window
mirror, which is not what plays"), which is an ancestor of this branch's HEAD:

- *"timelinePosAt reverse math has unsigned underflow outside window"* — fixed: the
  mirror is computed in signed arithmetic (`std::int64_t played`) and clamped to
  the window's edge, so a frame past the end of the source cannot wrap. The guard
  for it is now a regression of its own
  (`SampleClipWindowTest::aFrameOutsideTheWindowMapsToItsNearestEdge()`).
- *"Actual Sample::play reverses whole buffer-size index, unlike windowmirror on
  asymmetric trim"* — fixed: the mapping mirrors over the whole buffer
  (`size - 1 - f`), which is what `Sample::render` does, and the asymmetric trim is
  proven audible by `SampleClipWindowTest::aReversedImpulseSoundsWhereItsTransientPlaces()`.

The third one was still open, and it is what this commit fixes:

- *"reversed descending ticks then feed ascending-only quantisedMarkers loop
  dropping all but first"* — **REAL**. A reversed clip's transients' ticks DESCEND
  with their source frames, and the quantise's marker set has to be strictly
  increasing in both coordinates, so the ascending-only rule kept the first
  transient and dropped every other one. The regression below measures **1 marker
  instead of 4** (and 1 instead of 2 for a trimmed reversed clip).

### Evidence (file:line)

- `src/core/ControlCommandsWarpQuantize.cpp:54` `quantisedMarkers()` — the loop
  took the transients in source-frame order, took each one's tick from
  `timelinePosAt()` (which descends for a reversed clip) and dropped every marker
  whose tick did not exceed the previous one: only the first survived. It also
  wrote the marker coordinate in SOURCE frames while the warp map is written in
  PLAY frames.
- `src/core/ClipTransients.cpp:33` `clipTransientFrames()` — the detector scanned
  `clip.sampleWindow()`, which for a reversed clip is the region the clip never
  plays (it sounds the mirror of the window), so a trimmed reversed clip had no
  transients at all and the ones it had sat outside its own audio.
- `src/core/Sample.cpp` `Sample::render()` — the whole-buffer reversal the
  contract is derived from: the frame read at play frame `p` is the buffer frame
  `size - 1 - p`.
- `src/core/SamplePlayHandle.cpp` `warpRatio()` — the render path asks the warp
  map about PLAY frames, so the play frame is the coordinate the map is written
  in.
- `include/Clip.h:260` — the base class's mapping contract ("the two functions
  are inverses up to the frame-to-tick truncation"), which the header now states
  for the reversed case as well.
- Regressions: `tests/src/core/WarpQuantizeTest.cpp`
  `aReversedClipKeepsEveryTransientInOrder()` and
  `aTrimmedReversedClipQuantisesTheAudioItPlays()`;
  `tests/src/tracks/SampleClipWindowTest.cpp`
  `aFrameOutsideTheWindowMapsToItsNearestEdge()`.

### The contract, in one place

`include/SampleClip.h` and `src/core/SampleClip.cpp` now say it:

- the **window is a PLAY range** — the frames the clip plays, in play order;
- `audibleWindow()` — the source range the clip actually sounds: the window for a
  forward source, the window's MIRROR for a reversed one (because `Sample::render`
  reverses the whole buffer);
- `playFrameFor(sourceFrame)` — the play frame of a source frame (the mirror,
  clamped to the window's edge): the coordinate the warp map is written in;
- `timelinePosAt(sourceFrame)` — where a SOURCE frame sounds, routed through
  `playFrameFor()`, so the mirror and the clamp live in one place. Its answers are
  bit for bit the ones it gave before this change (the same signed mirror, the
  same clamp);
- `sourceFrameAt(timelinePos)` — the play frame at a timeline position, which for
  a forward source is the source frame. It is left as it was: the play handles,
  the consolidation and the audio loop unfold the clip's PLAY range, and the
  source-frame tools slice the BUFFER — `playFrameFor()` is the bridge between the
  two, and the header now says which coordinate each mapping answers in.

### The fix

- `src/core/ClipTransients.cpp` — the detector looks at `clip.audibleWindow()`:
  the transients are the clip's AUDIO's, so they are detected where the audio is.
- `src/core/ControlCommandsWarpQuantize.cpp` — the transients are walked in
  play-frame order and the marker coordinate is the play frame:

```cpp
	std::vector<std::pair<f_cnt_t, f_cnt_t>> ordered;
	ordered.reserve(transients.size());
	for (const f_cnt_t frame : transients) { ordered.emplace_back(clip.playFrameFor(frame), frame); }
	std::sort(ordered.begin(), ordered.end());
	for (const auto& [playFrame, frame] : ordered)
	{
		...
		(*markers)[static_cast<std::size_t>(count++)] = WarpMarker{playFrame, moved};
	}
```

A forward clip's play frame IS its source frame, so a forward clip's marker set is
exactly the one it was — the only forward-visible change is the order of the
walk, which is the same order.

### Commands and exit codes

```
$ cmake --build build --target WarpQuantizeTest SampleClipWindowTest -j 20 > logs/build-12-review-green.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^(WarpQuantizeTest|SampleClipWindowTest)$' --output-on-failure > logs/test-12-review-green.log 2>&1
CTEST_EXIT=0
100% tests passed out of 2
```

Test-proof of the bug (the product change reverted, then restored):

```
$ git diff -- include/SampleClip.h src/core/SampleClip.cpp src/core/ClipTransients.cpp src/core/ControlCommandsWarpQuantize.cpp > logs/fix-review-17b7bfa95.patch
$ git apply -R logs/fix-review-17b7bfa95.patch
REVERTED=OK
$ cmake --build build --target WarpQuantizeTest SampleClipWindowTest -j 20 > logs/build-13-review-red.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^(WarpQuantizeTest|SampleClipWindowTest)$' --output-on-failure > logs/test-13-review-red.log 2>&1
CTEST_EXIT=8
```

```
FAIL!  : WarpQuantizeTest::aReversedClipKeepsEveryTransientInOrder() Compared values are not the same
   Actual   (warp.size()): 1
   Expected (4)          : 4
   Loc: [../../tests/src/core/WarpQuantizeTest.cpp(173)]
FAIL!  : WarpQuantizeTest::aTrimmedReversedClipQuantisesTheAudioItPlays() Compared values are not the same
   Actual   (warp.size()): 1
   Expected (2)          : 2
   Loc: [../../tests/src/core/WarpQuantizeTest.cpp(210)]
Totals: 7 passed, 2 failed, 0 skipped, 0 blacklisted, 1354ms
```

```
$ cd ../.. && git apply logs/fix-review-17b7bfa95.patch
REAPPLIED=OK
$ cmake --build build --target WarpQuantizeTest SampleClipWindowTest -j 20 > logs/build-14-review-green2.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^(WarpQuantizeTest|SampleClipWindowTest)$' --output-on-failure > logs/test-14-review-green2.log 2>&1
CTEST_EXIT=0
100% tests passed out of 2
```

The out-of-window guard
(`SampleClipWindowTest::aFrameOutsideTheWindowMapsToItsNearestEdge()`) passes
before and after the change: it is the hardening the finding asked for on the
underflow, which `09674ea8a` had already fixed. It is not a RED/GREEN proof and is
not claimed as one.

### The affected family, after the change

```
$ cmake --build build --target zene SampleClipWindowTest SampleClipWarpTest SampleClipStretchTest SampleClipRubberBandTest SampleClipWarpModeMenuTest WarpQuantizeTest ControlWarpCommandsTest ClipSerialisationTest ClipWarpPersistenceTest ClipConsolidateTest AudioEditingTest AudioAnalysisTest AudioAutomationToolsTest ControlSampleOperatorTest W3EngineTest WriteRefusalGateTest ArrangeToolsTest AudioClipLoopTest ControlTempoMapCommandsTest TapTempoTest -j 20 > logs/build-16-family.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^(SampleClipWindowTest|SampleClipWarpTest|SampleClipStretchTest|SampleClipRubberBandTest|SampleClipWarpModeMenuTest|WarpQuantizeTest|ControlWarpCommandsTest|ClipSerialisationTest|ClipWarpPersistenceTest|ClipConsolidateTest|AudioEditingTest|AudioAnalysisTest|AudioAutomationToolsTest|ControlSampleOperatorTest|W3EngineTest|WriteRefusalGateTest|ArrangeToolsTest|AudioClipLoopTest|ControlTempoMapCommandsTest|TapTempoTest)$' --output-on-failure > logs/test-17-family.log 2>&1
CTEST_EXIT=0
100% tests passed out of 20
```

---

---

## Round 2 (reviewer r1) — the GUI path: the overlay's analysis cache

### The blocking finding

> Reversing a trimmed clip repaints its transient overlay using cached frames from the
> prior direction. `SampleClipView`'s analysis cache key includes the buffer and authored
> window but omits playback direction; the later placement key includes direction and only
> remaps those stale frames. `reverseSample()` toggles direction and repaints without
> invalidating the analysis cache.

### Verdict: REAL — fixed

The quantise fix of round 1 made the CORE detector look at the clip's audible range, but
the GUI's overlay keeps its own cache of the analysed frames, keyed by the AUTHORED
window. Reversing a trimmed clip therefore left the overlay with the frames of the range
the clip played before the reversal, and the placement (which does know the direction)
mapped those stale frames onto the reversed clip, where they all collapse onto the play
window's edge: the overlay showed one marker at the window's end instead of the
transients of the audio the clip plays.

### Evidence (file:line)

- `src/gui/clips/SampleClipView.cpp:205` `SampleClipView::transientTicks()` — the
  analysis (the slow part) is cached per audio and per range
  (`m_transientAudioKey`), and the range was keyed by `m_clip->sampleWindow()`, the
  AUTHORED window; the direction entered only the later placement key
  (`m_transientKey`), which re-maps the frames the cache has.
- `src/gui/clips/SampleClipView.cpp:514` `SampleClipView::reverseSample()` — toggles the
  direction and repaints, without invalidating the analysis cache.
- `src/core/SampleClip.cpp:778` `SampleClip::audibleWindow()` — the range the clip
  actually sounds: the window for a forward source, the window's MIRROR for a reversed
  one.
- Regression: `tests/src/gui/ArrangerTimelineTest.cpp`
  `aReversedTrimmedClipDrawsTheTransientsOfTheAudioItPlays()`.

### Fix

- `src/gui/clips/SampleClipView.cpp` — the analysed range is the clip's AUDIBLE window,
  so the cache key follows it (a direction change moves the range, so the frames are
  analysed again):

```cpp
	const SampleWindow window = m_clip->audibleWindow();
	const QString audio = QStringLiteral("%1:%2:%3").arg(reinterpret_cast<quintptr>(m_clip->sample().data()))
		.arg(window.sourceIn).arg(window.sourceOut);
```

- `src/gui/clips/SampleClipView.cpp` — and the direction toggle drops the cache
  outright, because the direction decides which audio the clip plays:

```cpp
void SampleClipView::reverseSample()
{
	m_clip->m_sample.setReversed(!m_clip->m_sample.reversed());
	// The direction decides WHICH audio the clip plays, so the analysed range moves with it: ...
	m_transientAudioKey.clear();
	m_transientKey.clear();
	m_clip->setStartTimeOffset(m_clip->length() - m_clip->startTimeOffset() - m_clip->sampleLength());
	...
```

- `include/SampleClipView.h` — `transientTicks()` moves from the private section to the
  public one, so the overlay's cache invalidation can be regression-tested. It is the
  same method the paint path calls; no behaviour change.

The regression lives in `tests/src/gui/ArrangerTimelineTest.cpp` — the arranger's
timeline is where the clip view and its overlay are drawn, and that test file already
carries the GUI application. (A dedicated new test file was written first and dropped:
the lane's cmake shim refuses to configure, so a newly registered test target cannot be
built or run in this lane — the regression therefore went into an already registered
GUI test.)

### Commands and exit codes

```
$ cmake --build build --target ArrangerTimelineTest WarpQuantizeTest SampleClipWindowTest -j 20 > logs/build-24-round2.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^(ArrangerTimelineTest|WarpQuantizeTest|SampleClipWindowTest)$' --output-on-failure > logs/test-24-round2-green.log 2>&1
CTEST_EXIT=0
100% tests passed out of 3
```

Test-proof of the bug (the product change reverted, then restored):

```
$ git diff -- src/gui/clips/SampleClipView.cpp > logs/fix-review-17b7bfa95-gui.patch && git apply -R logs/fix-review-17b7bfa95-gui.patch
REVERTED=OK
$ cmake --build build --target ArrangerTimelineTest -j 20 > logs/build-25-round2-red.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure > logs/test-25-round2-red.log 2>&1
CTEST_EXIT=8
```

```
FAIL!  : ArrangerTimelineTest::aReversedTrimmedClipDrawsTheTransientsOfTheAudioItPlays() 'first || swapped' returned FALSE. (the transients sit at 99 and 99, expected 45 and 93)
   Loc: [../../tests/src/gui/ArrangerTimelineTest.cpp(958)]
Totals: 32 passed, 1 failed, 0 skipped, 0 blacklisted, 6643ms
```

```
$ cd ../.. && git apply logs/fix-review-17b7bfa95-gui.patch
REAPPLIED=OK
$ cmake --build build --target ArrangerTimelineTest -j 20 > logs/build-26-round2-green2.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure > logs/test-26-round2-green2.log 2>&1
CTEST_EXIT=0
100% tests passed out of 1
```

The non-blocking note of the review is acted on as well: the trimmed reversed quantise
test's comment said the authored first-half window "contains no bursts", which is not
true of the fixture (`clicks(true)` puts bursts in BOTH halves). The comment now says
what actually happens — the detector that looked at the window found the two bursts of
the half the reversed clip never plays, and their ticks collapse onto the play window's
edge, so the quantise kept one of them instead of the two the clip plays.

### The affected family, after the round-2 change

```
$ cmake --build build --target zene SampleClipWindowTest WarpQuantizeTest SampleClipWarpTest ClipSerialisationTest ArrangeToolsTest AudioEditingTest SampleClipWarpModeMenuTest ArrangerTimelineTest SampleClipStretchTest SampleClipRubberBandTest ClipWarpPersistenceTest ClipConsolidateTest AudioAnalysisTest AudioAutomationToolsTest ControlWarpCommandsTest WriteRefusalGateTest AudioClipLoopTest ControlTempoMapCommandsTest TapTempoTest -j 20 > logs/build-27-round2-family.log 2>&1
BUILD_EXIT=0
$ cd build/tests && ctest -R '^(ArrangerTimelineTest|SampleClipWindowTest|WarpQuantizeTest|SampleClipWarpTest|ClipSerialisationTest|ArrangeToolsTest|AudioEditingTest|SampleClipWarpModeMenuTest|SampleClipStretchTest|SampleClipRubberBandTest|ClipWarpPersistenceTest|ClipConsolidateTest|AudioAnalysisTest|AudioAutomationToolsTest|ControlWarpCommandsTest|WriteRefusalGateTest|AudioClipLoopTest|ControlTempoMapCommandsTest|TapTempoTest)$' --output-on-failure > logs/test-28-round2-family.log 2>&1
CTEST_EXIT=0
100% tests passed out of 19
```

The reviewer's focused list — `SampleClipWindowTest`, `WarpQuantizeTest`,
`SampleClipWarpTest`, `ClipSerialisationTest`, `ArrangeToolsTest`, `AudioEditingTest`,
`SampleClipWarpModeMenuTest` — is inside that run, together with the GUI regression.

---

## Gates

```
$ python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-feature-check-1.log 2>&1
FEATURE_CHECK_EXIT=0
```

and again after the round-2 change:

```
$ python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-feature-check-2.log 2>&1
FEATURE_CHECK_EXIT=0
```

```
feature-check zene-qwen-review-17b7bfa95 (base b61e5f965, 7 changed files; logs in .../zene-qwen-review-17b7bfa95-feature-check)
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

The two ratchet gates are red on the train as they are everywhere else, and none
of their items is in a file this change touched:

```
$ bash tests/complexity-gate.sh --check > logs/gate-complexity.log 2>&1
COMPLEXITY_EXIT=1        # 5 pre-existing items, none in SampleClip.cpp / ClipTransients.cpp / ControlCommandsWarpQuantize.cpp
$ bash tests/file-length-gate.sh --check > logs/gate-filelength.log 2>&1
FILELENGTH_EXIT=1        # 15 pre-existing items, none in a file this change touched
```

No gate limit and no baseline was re-anchored.

## Files committed

- `b8ef98d65` — `src/core/SampleClip.cpp`, `tests/src/tracks/SampleClipWindowTest.cpp`,
  `tests/upstream-modifications.txt` (cluster new-p16-205)
- `dfed7915c` — `src/core/SampleClip.cpp`, `tests/src/tracks/SampleClipWindowTest.cpp`,
  `tests/upstream-modifications.txt` (cluster new-p17-158)
- `b11a696f1` — `include/SampleClip.h`, `src/core/SampleClip.cpp`,
  `src/core/ClipTransients.cpp`, `src/core/ControlCommandsWarpQuantize.cpp`,
  `tests/src/core/WarpQuantizeTest.cpp`,
  `tests/src/tracks/SampleClipWindowTest.cpp`, `tests/upstream-modifications.txt`
  (cluster review-17b7bfa95)
- `beb00b6a4` — `include/SampleClipView.h`, `src/gui/clips/SampleClipView.cpp`,
  `tests/src/gui/ArrangerTimelineTest.cpp`, `tests/src/core/WarpQuantizeTest.cpp`,
  `tests/upstream-modifications.txt` (cluster review-17b7bfa95, round 2: the GUI
  overlay's analysis cache)
- this report

`git status` is clean.

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it
(exit codes).**

| cluster | RED (reverted) | GREEN (restored) |
| --- | --- | --- |
| new-p16-205 | `ctest -R '^SampleClipWindowTest$'` → EXIT 8 (`the length stayed at 896, the window says 448`) | EXIT 0 |
| new-p17-158 | `ctest -R '^SampleClipWindowTest$'` → EXIT 8 (`Actual -252, Expected 0`) | EXIT 0 |
| review-17b7bfa95 | `ctest -R '^(WarpQuantizeTest\|SampleClipWindowTest)$'` → EXIT 8 (1 marker instead of 4, and of 2) | EXIT 0 |

The exact patches are `logs/fix-p16-205.patch`, `logs/fix-p17-158.patch` and
`logs/fix-review-17b7bfa95.patch`; each was applied with `git apply -R`, rebuilt,
run (RED), then applied again, rebuilt, run (GREEN). The commands and their exit
codes are quoted verbatim above.

**2. The test is deterministic: no wall-clock timing, sleeps or races.**

- `trimmingTheWindowMovesTheClipLength` reads the clip's own window and length
  after the edit — no clock, no thread, no waiting.
- `aZeroRateSourceMapsToTheClipOrigin` builds a 1024 frame buffer of silence at
  rate 0 and sets two warp markers by hand; the frames, the rate and the markers
  are all injected values.
- `aReversedClipKeepsEveryTransientInOrder` and
  `aTrimmedReversedClipQuantisesTheAudioItPlays` drive the real
  `warp.quantize` command over a deterministic burst signal
  (`test::burstAudio`, the same fixture the file's other tests use) and read the
  resulting marker set; the onset detector itself is a pure function of the
  samples.
- `aFrameOutsideTheWindowMapsToItsNearestEdge` asks the mapping about two
  out-of-window frames and checks the answer is inside the clip's span.
- The only waiting in the whole set is the existing `drainPlayHandles()` helper of
  the file, which belongs to the playback tests that were already there; none of
  the new tests plays audio.

**3. The fix is at the cause and complete: every caller and sibling of what you
changed was checked and stays consistent.**

- `clipTransientFrames()` — every caller was checked:
  `ControlCommandsWarpQuantize.cpp:88` (the quantise), `ControlCommandsAudioSlice.cpp:138`
  (the slice command and its `transients` report),
  `ControlCommandsTransportMap.cpp:526` (the beat map),
  `src/gui/clips/SampleClipView.cpp:215` (the transient overlay). All of them ask
  the clip for the frames of its AUDIO; for a forward clip the audible range IS
  the window, so nothing changes for them. The tick lists they derive
  (`clipTransientTicks`) are sorted by their consumers
  (`ControlCommandsAudioSlice.cpp:125`, `ControlCommandsArrangeTools.cpp:117`).
- `quantisedMarkers()` — the only caller is `quantizeAudio()`
  (`src/core/ControlCommandsWarpQuantize.cpp:88`), and the marker set it builds
  still goes through `applyMarkers()` → `SampleClip::setWarpMarkers()`, whose
  `WarpMarkers::set()` rejects a set that is not strictly increasing in both
  coordinates: the set the fix builds is, by construction.
- The `warp` group's sibling commands were checked one by one and are consistent
  with the fix: `warp.add`, `warp.set`, `warp.move`, `warp.remove` and `warp.list`
  write or read the marker set the render path reads, and they are written in
  PLAY frames — which is the coordinate the quantise now writes too, so the two
  agree (before the fix the quantise wrote SOURCE frames while the render path
  read PLAY frames: that is the disagreement the fix closes). `warp.stretch` does
  not touch the markers. `warp.quantize` is the command this fix changes. The
  whole group is `warp.add`, `warp.list`, `warp.move`, `warp.quantize`,
  `warp.remove`, `warp.set`, `warp.stretch`. No command id, argument or schema
  changed, so `tools/mcp-zene-control/zene_control/commands_snapshot.json` is
  unchanged and the ControlCommandsSnapshot canary does not move.
- `SampleClip::timelinePosAt()` — its only callers are the transient overlay, the
  slice/analysis/arrange/transport-map commands and the quantise; the refactor
  routes the same mirror through `playFrameFor()` and returns the same values
  (the pre-fix and post-fix answers are the same function of the same inputs).
- `SampleClip::setSampleWindow()` — its callers are the two legacy frame setters
  and `loadSettings()`. The frame-exact trim does NOT go through it: it authors
  the window through `replaceSample()` (`src/core/SampleFrameEdits.cpp:82`),
  which sets the length itself and turns auto-resize off first, so the trim's own
  length is untouched. `loadSettings()` puts the file's length back.
- `SampleClip::sourceFrameAt()` — deliberately unchanged: the play handles, the
  consolidation (`src/core/ClipConsolidate.cpp:63`) and the audio loop unfold the
  clip's PLAY range and need the play frame, while the audio tools slice the
  BUFFER; changing it would have made the consolidation of a reversed clip build
  an inverted window. The header now states which coordinate each mapping answers
  in, and `playFrameFor()` is the bridge.
- One sibling was found and deliberately NOT changed: the beat map
  (`ControlCommandsTransportMap.cpp:526`) builds its tempo events from the
  transient frames, so for a reversed clip the events' ticks descend. That is a
  property of the tempo map's own direction handling, it is pre-existing (the
  ticks of a reversed clip have descended since the direction was honoured) and
  it is not the window contract; the tempo map refuses a set that is not
  ascending, so a reversed clip's beat map is refused rather than written
  wrongly. Changing that is a separate fix and is out of this batch's scope.

**4. No collateral damage: old projects and saved data still load as before, and
the new checks do not hit other callers that legitimately exceed them.**

- The `new-p16-205` change is the only one that touches the load path, and it
  keeps the file's `len` authoritative: `loadSettings()` restores the length it
  read from the file after the window is applied, so a project that carries a
  window loads with the length it was saved with. `ClipSerialisationTest` and
  `ClipWarpPersistenceTest` (the round-trip tests) pass unchanged.
- The `new-p17-158` check refuses a nonpositive frames-per-tick. No clip that
  legitimately exceeds it exists: a rate of zero is not a rate, and the same
  refusal is already the rule in `windowTicksFor()` and
  `leadOutputFramesAt()`. A clip with a positive rate is unaffected.
- The `review-17b7bfa95` change is confined to a REVERSED clip: for a forward
  clip the audible range is the window, the play frame is the source frame, and
  the marker set is the one it was. The serialisation is untouched by this
  commit (no `<warp>` or window attribute changed), so a saved project loads
  exactly as before.

**5. Realtime, undo and commands.**

- Nothing new runs on the audio thread. `SampleClip::timelinePosAt()` and
  `playFrameFor()` are pure arithmetic over the clip's own state — no
  allocation, no lock, no logging, no I/O, no signal (the mapping is called from
  `SamplePlayHandle` and the existing
  `SampleClipWindowTest::readPathArithmeticDoesNotAllocate()` still passes: it
  calls `sourceFrameAt()`, `timelinePosAt()` and `handle.totalFrames()` 1000
  times and counts ZERO allocations).
- `audibleWindow()` and `playFrameFor()` are called from the control thread
  (the transient detector, the quantise) and from the mapping; neither
  allocates.
- The quantise still takes ONE journal checkpoint (`clip->addJournalCheckPoint()`)
  and the markers still live in the clip's `<warp>` element, so one undo takes
  the quantise back: `WarpQuantizeTest::theTransientsLandOnTheGrid` asserts
  `control.undo` empties the marker set and it passes.
- No new control command and no changed command id, argument or schema.

**6. Qt 5.15 / MSVC / macOS portability.**

No Qt-6-only API, no `windows.h` macro name and no `std::filesystem` was used.
The new code uses `<algorithm>`, `<utility>`, `<vector>`, `<array>` and
`std::sort`/`std::pair`/`std::span`, all of which the project already uses
(`std::span` is already in the test file), and the project is C++20.


**7. Round 2 — the GUI path (the reviewer's blocking finding).**

- The regression test FAILS with the product change reverted and PASSES with it:
  `ctest -R '^ArrangerTimelineTest$'` → EXIT 8 with
  `the transients sit at 99 and 99, expected 45 and 93` (the stale forward frames
  clamped onto the play window's edge) and EXIT 0 with the fix restored.
- The test is deterministic: the bursts are the fixture's (`test::burstAudio`, fixed
  samples), the clip's window is authored by the test, the reversal is the GUI's own
  `reverseSample()` slot, and the assertion reads the overlay's own tick list. No
  clock, no sleep, no rendering, no thread.
- The fix is at the cause: the overlay's analysis cache is keyed by the ANALYSED
  RANGE, and the analysed range is the clip's audible window, so the direction is part
  of the key; `reverseSample()` drops the cache as well. The overlay's other caller is
  the paint path (`src/gui/clips/SampleClipView.cpp:456`, `paintTransients(p,
  transientTicks(), ...)`), which draws one mark per tick — the order of the ticks does
  not matter to it, which is why the regression asserts the SET of the two ticks.
- No collateral damage: for a forward clip the audible range IS the window, so the key
  and the analysed frames are exactly the ones of round 1; an untrimmed reversed clip
  analyses the whole source either way, so nothing moves.
- Realtime: the overlay is painted on the GUI thread, not the audio thread; the change
  adds no allocation to the paint path (the analysis is the slow part that was already
  cached) and no new signal, lock or command. The GUI's reverse journals no undo step
  of its own, before and after the change.

**8. "If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
the intended contract that shows it."**

Not applicable: all three verdicts in this batch are FIXED, and each one carries
its own `file:line` evidence above. The two parts of the review-17b7bfa95
finding that were ALREADY FIXED in this tree (the unsigned underflow and the
asymmetric-trim mirror) are named as such in that section, with the commit that
fixed them (`09674ea8a`) and the code that carries the fix
(`src/core/SampleClip.cpp:809` `SampleClip::timelinePosAt()` and its regression
`SampleClipWindowTest::aReversedImpulseSoundsWhereItsTransientPlaces()`).
---

new-p16-205: FIXED
new-p17-158: FIXED
review-17b7bfa95: FIXED
