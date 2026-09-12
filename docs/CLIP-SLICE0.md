# Clip-and-capture wave — Slice 0 and Slice 1, as built (#611)

Implementation report for **Slice 0** (the read-only playback path and the authored clip window)
and **Slice 1** (serialisation), per `docs/CLIP-CAPTURE-DESIGN.md` §4.1. Branch
`post-alpha/clip-slice0`, based on `post-alpha/integration` (`ccd07f490`).

| commit | what |
|---|---|
| `f43ac532f` | Slice 0 — `SampleWindow`, the authored window on `SampleClip`, the read-only `SampleTrack::play`, the window snapshot in `SamplePlayHandle`, `tests/src/tracks/SampleClipWindowTest.cpp` |
| `a928b35f7` | Slice 1 — `srcin`/`srcout` on `<sampleclip>`, the `autoresize="0"` rule, `tests/src/core/ClipSerialisationTest.cpp` |
| `b6c323fe4` | evidence — the pre-slice arithmetic equivalence test, and `tests/data/clip-window/render-proof.sh` |
| this file | the report |

---

## 1. What the design said, and the answers it supplied

§4.1: **"Slice 0 — the read-only playback path and the clip window. This gates everything else. …
Nothing in trim, slip, fades or comping can hold while `SampleTrack::play` rewrites the clip's window
on every play pass (`src/tracks/SampleTrack.cpp:126-127`) and `updateLength`/`tempoChanged` reset the
length and slip (`src/core/SampleClip.cpp:235-236`, `:245`). Every editing lane's first bug report
would otherwise be 'my trim reverted', and it would be true."** Slice 1: *"the moment a window exists
it must survive a save, which means `src/core/SampleClip.cpp:281-356` plus the `autoresize="0"`
dependency (§2.6)."*

The blocking question the design answers itself is **OQ-1**, and this build takes that answer: *"keep
it on `Sample` and make the play path read-only, or move the authored window onto `SampleClip` and
leave `Sample`'s fields as the render-time scratch they already are. **The second is the design
above**, because the first leaves two writers of one quantity."* So:

- the **authored window** lives on `SampleClip` (`m_window`) — the only writer is `SampleClip`;
- `Sample::m_startFrame`/`m_endFrame` stay the **render-time scratch**, pointed at the window by
  `SampleClip` and read by `Sample::render`/`SamplePlayHandle`;
- `SampleTrack::play` writes **neither**: it derives the window for the pass and hands it to the play
  handle as a **snapshot** (I1).

Two further decisions the design left to the implementer, and what was chosen:

- **OQ-2 (where a fade would go)** — no fades are built in this slice, so nothing was added to
  `Sample::render`. The metronome and browser-preview call sites
  (`src/core/SamplePlayHandle.cpp:106-108`, `FileBrowser.cpp:816`, `Metronome.cpp:36-37`) are
  **excluded by construction**: the preview constructors keep the sample's own frame range, exactly
  what they rendered before. Stated here because the design asks for the exclusion list.
- **OQ-4 (`autoresize` on a trimmed clip)** — answered as: `setSampleWindow()` clears auto-resize
  when the window is not the whole buffer (a trim is a manual edit — the same thing
  `ClipView.cpp:904-923` does for a manual resize), which makes `autoresize="0"` fall out of the
  existing serialiser rather than being forced at save time. On load the **file's** value is applied
  *after* the window, so a hand-written or future file that says `autoresize="1"` keeps it. Because
  `sampleLength()` now reads the window, a tempo change re-fits the length to the window instead of
  undoing the trim — the trim can no longer die either way.

---

## 2. What was built

**`include/SampleWindow.h`** (new, header-only) — `{sourceIn, sourceOut}`, half-open, frames;
`full(bufferFrames)`, `clamped(in, out, bufferFrames)`, `length()`, `empty()`, `==`/`!=`. The
clamping rule (I4) lives in one place.

**`include/Clip.h` / `src/core/SampleClip.cpp`** — the mapping seam §2.4 freezes, because §4.3 says
warp (#597) is unblocked by Slice 0 *"provided the two mapping virtuals (§2.4) are frozen in it"*:

```cpp
virtual f_cnt_t sourceFrameAt(TimePos timelinePos) const;   // linear today
virtual TimePos timelinePosAt(f_cnt_t sourceFrame) const;   // inverse, for trimming
```

`Clip`'s implementation is the tick identity (MIDI/automation have no frame-domain source);
`SampleClip` overrides both with the linear frame map through `Engine::framesPerTick()` and clamps
into the window. `SampleClip::sampleLength()` is now the **window's** length in ticks, which is
identical to the old `sampleSize()`-based value for a clip without a trim.

**`src/tracks/SampleTrack.cpp`** — `play()` no longer writes a clip (`sClip->setSampleStartFrame` /
`setSamplePlayLength` at `:126-127`, and the pattern-store write at `:99`, are gone). Per pass it
computes `windowStart = sClip->sourceFrameAt(_start)` and `windowEnd = sClip->sourceFrameAt(endPosition())`
and schedules `{clip, window}`; the handle gets `{windowStart, windowEnd}`. The old guard
`sampleStart < sampleBufferLength` becomes `windowStart < windowEnd`, which is the same predicate
(proved in §5).

**`include/SamplePlayHandle.h` / `src/core/SamplePlayHandle.cpp`** — the handle snapshots its window
at construction (`m_window`) and `totalFrames()` reads the snapshot, so a later pass cannot move a
live handle's length (the old code read the sample's live frame fields, which the pass was moving
underneath it). New overload `SamplePlayHandle(SampleClip*, const SampleWindow&)`; the 1-arg clip
constructor renders the clip's whole window; the preview/metronome constructors are unchanged in
behaviour (window = the sample's own range, snapshotted).

**Slice 1 (`src/core/SampleClip.cpp` serialisation)** — `srcin`/`srcout` are written **only** when
the window is not the whole buffer, so a clip with no trim writes the exact pre-slice attribute set
(I9); `loadSettings` applies them after `len`/`off` and before `autoresize` (§2.6 ordering). No
`UPGRADE_METHODS` entry and no `ELEMENTS_WITH_RESOURCES` entry are needed — both as §2.6 says.

**Test coverage added:** `tests/src/tracks/SampleClipWindowTest.cpp` (12 slots) and
`tests/src/core/ClipSerialisationTest.cpp` (5 slots). Both are registered in `tests/CMakeLists.txt`
**and** in `tests/fork-sources.txt`; every upstream-inherited file this touches is declared in
`tests/upstream-modifications.txt` with a reason (`include/Clip.h`, `include/SampleClip.h`,
`include/SamplePlayHandle.h`, `src/core/SampleClip.cpp`, `src/core/SamplePlayHandle.cpp`,
`src/tracks/SampleTrack.cpp`).

---

## 3. The RED run — the defect, on the unmodified tree

The test file was written first and built against the **unmodified** production tree (only the test
and its `tests/CMakeLists.txt` registration present). The defect test asserts the only thing the old
tree offers — the window it wrote into the clip's own `Sample` — need not change; the direct
`clip->sampleWindow()` assertions are inside `#if __has_include("SampleWindow.h")`, which is how one
file is both the RED test on the old tree and the regression guard on the new one.

```
$ cmake --build build --target SampleClipWindowTest -j4        # BUILD EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./SampleClipWindowTest > red-run.log 2>&1; echo EXIT=$?
EXIT=134
```

```
FAIL!  : SampleClipWindowTest::authoredWindowSurvivesAPlaybackPass() Compared values are not the same
   Actual   (asNumber(static_cast<f_cnt_t>(clip->sample().startFrame()))): 0
   Expected (asNumber(authoredIn))                                       : 44100
   Loc: [.../tests/src/tracks/SampleClipWindowTest.cpp(173)]
FAIL!  : SampleClipWindowTest::repeatedPassesLeaveTheWindowAlone() Compared values are not the same
   Actual   (asNumber(static_cast<f_cnt_t>(clip->sample().startFrame()))): 12600
   Expected (asNumber(authoredIn))                                       : 44100
FAIL!  : SampleClipWindowTest::malformedWindowIsRejected() Compared values are not the same
   Actual   (asNumber(static_cast<f_cnt_t>(clip->sample().endFrame()))): 44100
   Expected (asNumber(authoredOut))                                    : 88200
FAIL!  : SampleClipWindowTest::twoClipsShareOneBufferWithIndependentWindows() ...
   Actual   (asNumber(static_cast<f_cnt_t>(clipA->sample().startFrame()))): 0
   Expected (asNumber(trimIn(rate)))                                      : 44100
Totals: 3 passed, 7 failed, 0 skipped, 0 blacklisted, 1460ms
```

That is the defect exactly as §2.5 describes it: the clip was trimmed to frames `[44100, 88200)`, one
playback pass ran, and the window had been rewritten to `[0, clipFrameLength)`. (The `12600` in the
second slot is the third pass's transport position, i.e. the window tracking the playhead.)

Two caveats, stated because they matter for the evidence: the crash-then-abort at the end of that run
was **my test's own bug** (it left play handles with the engine while the stack-allocated `SampleTrack`
was destroyed — see §5, "test hygiene"), fixed before the GREEN run; and the two edits made to the
file after the RED run are an include path (`../core/AllocationProbe.h`) and the `playHandleRender…`
probe's setup — neither touches the defect slot's body or its assertions.

## 4. The GREEN run

```
$ cmake --build build --target SampleClipWindowTest ClipSerialisationTest -j4    # BUILD EXIT=0
$ cd build/tests
$ QT_QPA_PLATFORM=offscreen ./SampleClipWindowTest; echo EXIT=$?     # EXIT=0, 12 passed, 0 failed
$ QT_QPA_PLATFORM=offscreen ./ClipSerialisationTest; echo EXIT=$?    # EXIT=0, 5 passed, 0 failed
```

`SampleClipWindowTest`: `authoredWindowSurvivesAPlaybackPass` (the defect), `repeatedPasses…`,
`malformedWindowIsRejected` (I4), `twoClipsShareOneBufferWithIndependentWindows` (I2),
`playbackRendersExactlyTheAuthoredWindow`, `timelinePositionMapsOntoTheWindow`,
`windowLengthAndTempoChange`, `untrimmedPassWindowMatchesThePreSliceArithmetic`,
`readPathArithmeticDoesNotAllocate`, `playHandleRenderDoesNotAllocate`.

`ClipSerialisationTest`: `untrimmedClipSerialisesExactlyAsBefore` (attribute set + bytes unchanged,
with the per-object `<journallingObject id>` neutralised — two distinct clips can never be compared
byte for byte, so the SlideNotesTest pattern does not transfer verbatim),
`trimmedWindowRoundTrips`, `wholeProjectWithAWindowRoundTrips` (a window set → project saved →
`Song::loadProject` → window identical → re-saved window identical).

---

## 5. The render comparison — and what it can and cannot prove

`tests/data/clip-window/render-proof.sh` generates a fixture project (a 2 s source: 220 Hz for the
first second, 880 Hz for the second; one `sampletrack` with one `sampleclip` at pos 0, len 384), and
renders it headless with a **reference** binary and the **changed** binary:

```
$ bash tests/data/clip-window/render-proof.sh \
    --before .../zene-pa-integration/build/lmms \   # ccd07f490 + a docs-only commit, same CI flags
    --after  "$PWD/build/lmms"
RENDER PROOF EXIT=0        # 10 verdicts, all PASS
```

```
render                 data-sha256        frames   0-1s dB/cross   1-2s dB/cross   2-3s dB/cross
before-untrimmed-run1  a233d05b81bfac23   226560    -9.0 / 352      -9.0 / 1408     -inf / 0
before-untrimmed-run2  f1161ea6dc609820   226560    -9.1 / 350      -9.0 / 1408     -inf / 0
after-untrimmed-run1   fd47d72aea864fa5   226560    -9.1 / 352      -9.0 / 1408     -inf / 0
after-untrimmed-run2   a233d05b81bfac23   226560    -9.0 / 352      -9.0 / 1408     -inf / 0
after-trimmed          cadbf916bd367c39   226560    -9.0 / 1408     -inf / 0        -inf / 0
before-trimmed         ed3524f2f25c2d13   226560    -9.0 / 352      -9.0 / 1408     -inf / 0
```

- **Sensitivity control (the comparator is not blind).** The trimmed project renders with the source's
  *second* second at 0-1 s (1408 zero crossings vs 352) and is silent from the window's end onward
  (`-inf dB` at 1-2 s where the untrimmed render is `-9.0 dB`): the window is what played, and the
  playback stopped at `sourceOut`. With the **pre-slice binary** the same trimmed project renders
  *identically to the untrimmed one* (best-aligned difference 6852 samples ≈ the tree's own jitter,
  versus 176292 for the changed binary) — that binary does not parse `srcin`/`srcout` at all, which is
  why the trim was invisible before this slice. (The design's own test for row 1 — two clips sharing
  one buffer with independent windows — is asserted in the unit suite instead.)

- **The byte-identity claim does not hold, in either direction, and that is a property of the tree.**
  Two runs of the **same** binary differ (best-aligned differing samples: 53314 for the reference
  binary, 47014 for the changed one; raw counts 157k–175k), and one changed-binary run
  (`a233d05b81bfac23`) is byte-identical to a reference run. The pattern — identical total length
  (226560 frames), a start offset that jitters by up to a frame, and differences concentrated at
  period boundaries — is the offline renderer's own non-determinism (`ProjectRenderer` renders in its
  own thread while the dummy audio device thread also runs `renderNextPeriod`), not anything this
  slice moved. The script therefore *asserts* that non-determinism as a verdict, so that making the
  renderer deterministic turns the script red and sends the reader back here.

- **What the renders do establish.** Same song length, same per-second RMS to 0.1 dB (the exact figure
  the task allows: `-9.0` vs `-9.0` at 0-1 s and 1-2 s, `-inf` vs `-inf` where there is no audio), the
  same source content, and a cross-build difference (47014–53314 best-aligned samples) inside the
  same-binary spread (tolerance 106628 = 2× the measured spread). Below that, the decisive byte-level
  evidence is deterministic and does not involve the renderer at all:

```
untrimmedPassWindowMatchesThePreSliceArithmetic  PASS
  the pre-slice arithmetic (SampleTrack.cpp:117-122) recomputed verbatim
  vs SampleClip::sourceFrameAt() for clip positions {0, 96} x transport
  {start, start+1, start+48, start+len-1}: bit-equal (32 comparisons)
```

  This also settles the guard change: for every case where the old code created a handle
  (`sampleStart < sampleBufferLength`), `windowStart < windowEnd` is the same predicate because
  `f(_start) < f(endPosition)` strictly and the clamp only bites where the old code refused anyway.

- **A separate observation, not caused by this slice:** a *post-render* shutdown abort
  (`QThread: Destroyed while thread is still running`, exit 134 after a complete WAV) appears
  sporadically in both binaries — 1 abort in 5 renders with the reference binary, 0 in 5 with the
  changed one. It is reported, not swallowed; the script does not treat it as a render failure.

## 5.1 Allocation probes (the realtime rule)

Two dynamic probes, `AllocationProbe.h`, on the methods this slice adds to the playback path:
`readPathArithmeticDoesNotAllocate` (1000 × `sourceFrameAt` + `timelinePosAt` + `totalFrames()`
→ **0 allocations**) and `playHandleRenderDoesNotAllocate` (64 periods of
`SamplePlayHandle::play` through a windowed clip → **0 allocations**). Nothing added to
`SampleTrack::play`/`SamplePlayHandle::play` allocates or locks: the snapshot is a POD by value and
`sourceFrameAt` is arithmetic. The `new SamplePlayHandle` in `SampleTrack::play` and its
`std::vector` are upstream's existing behaviour, untouched.

## 5.2 Test hygiene (and an upstream hazard found on the way)

The first GREEN attempt aborted inside `Track::isMuted()` on the audio thread with a stack address as
`this`: a play handle added through `AudioEngine::addPlayHandle` sits in the engine's lock-free
*new-handle* list until the audio thread adopts it, and `removePlayHandlesOfTypes()` only walks the
adopted list, so a track destroyed immediately after a playback pass leaves a live handle pointing at
it. The test drains and removes its handles before the track dies (`drainPlayHandles`) and explains
why. Upstream LMMS has the same window; nothing in this slice widens it.

---

## 6. The full suite and the gates

Run on the Slice 0 + Slice 1 tree (`a928b35f7`):

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
configure EXIT=0   (log: build/configure.log)
build EXIT=0   (log: build/build.log)
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 29
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)
```

Run on the final tree (`b6c323fe4`, the two evidence commits included):

```
configure EXIT=0
build EXIT=0
ctest EXIT=8    -> 97% tests passed, 1 tests failed out of 29   (#25 TwoTrackRecordingHarness)
$ cd build/tests && ctest --output-on-failure -j1 > ctest.log 2>&1; echo $?
0          -> 100% tests passed, 0 tests failed out of 29
$ ctest --output-on-failure -R TwoTrackRecordingHarness; echo $?    # isolated re-run
0          -> 1/1 Passed
```

29 tests is the integration branch's 27 plus this slice's 2. The single failure in the parallel run
was `TwoTrackRecordingHarness` reporting a decoded-WAV digest mismatch while its counters were exact
(`pushed=96000 recorded=96000 overflow=0 writeErrors=0`): the harness writes to the **fixed** path
`/tmp/lmms-recording-harness` (`tests/src/core/TwoTrackRecordingHarness.cpp:223`), sibling lanes on
this box were compiling at the time, and it passes in isolation and in the serial `-j1` run. This
slice touches nothing on the recording path (`TrackRecorder`, `MultiTrackRecorder`, `RecordRingBuffer`
are untouched), and the failure is reported here rather than retried away.

One more thing to record rather than gloss: the script is committed **not executable**
(`tools/local-ci.sh` is `100644` in this branch), so the command that works is
`bash tools/local-ci.sh …`.

Gate results on the final tree (unpiped):

| gate | result | note |
|---|---|---|
| 3 no-tautology | PASS (EXIT=0) | `--strict` is red for essentially every test file in the repo (pre-existing: "fewer asserts than slots") |
| 4 complexity | PASS (EXIT=0) | no new function over CCN 10 |
| 6 upstream divergence | EXIT=1 | **all six files this slice touches are `declared divergence`**; the 4 violations are pre-existing (`include/MainWindow.h`, `include/MidiController.h`, `src/core/midi/MidiAlsaSeq.cpp`, `src/core/midi/MidiClient.cpp`, from the MIDI-learn merge `e2689e62b`) and were left to their lane |
| 7 file length | PASS (EXIT=0) | |
| 8 duplication | PASS (EXIT=0) | |
| 9 fork-sources | EXIT=1 | **both new test files are registered**; the 3 unregistered files are pre-existing (`LufsMeterTest.cpp`, `MidiLearnTest.cpp`, `SessionModelTest.cpp`) — the exact omission the integration pass reported |

---

## 7. What is still not done (explicitly out of this slice)

- **The gestures.** No trim/slip/fade handle, no drag, no context-menu entry, no keyboard action.
  `include/ClipView.h`'s action set is untouched; a user cannot trim from the GUI yet. The model, the
  mapping (`timelinePosAt()` is the inverse a trim needs) and the persistence are in place for the
  lane that adds them.
- **Clip gain** (`ClipEdits::gain`), **fades** and **crossfades** (§3 rows 3-5) — not built. Nothing
  was added to `Sample::render`, so the metronome and preview paths are untouched (OQ-2).
- **Take lanes, comping, punch in/out, arbitrary input count, input monitoring** (§3 rows 6-10) —
  not built; they are later slices/children per §5.2.
- **A `ClipEdits` object** — Slice 0/1 carry only `SampleWindow`. The design places gain/fades on
  `Clip`; that is the editing track's work.
- **I5's atomicity.** `setSampleWindow()` authors the window only; the clip's *length* follows the
  window through the existing `sampleLength()`/`updateLength()` path, but nothing yet changes both in
  one journalled operation. The design assigns "a trim changes both together, atomically" to the trim
  *operation*, and `ClipEdits`-style journalling is not in this slice — so a programmatically trimmed
  clip can carry a length longer than its window (the render fixture deliberately exercises that: the
  audio stops at `sourceOut` even though the clip's bars continue). The gesture lane must land the
  atomic change (and the undo entry) together with the drag.
- **A strict I1.** The authored window is now written only by `SampleClip`, and `SampleTrack::play`
  writes neither the clip nor its `Sample`; but `play()` still updates `Clip::m_isPlaying`, the
  play-session bookkeeping flag it always owned. A strict reading of I1 ("no function called from
  `Song::processNextBuffer` may mutate a `Clip`") would move that flag out of the clip; that is a
  larger change than this slice and is left visible here rather than quietly claimed.
- **Renders are not reproducible in this tree**, as measured in §5. Nothing in this slice caused it,
  but any lane that wants a byte-identical render oracle must first make the offline renderer
  single-threaded (or stop the dummy device thread), and the render-proof script will say so.
