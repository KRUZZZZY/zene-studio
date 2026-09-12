# Warp engine — what landed, and the numbers that prove it (#597)

Lane: `post-alpha/warp` (worktree `zene-pa-warp`), based on `post-alpha/clip-slice0`
(`903d70916`), which carries the frozen clip model from Slice 0 + Slice 1 of the
clip-and-capture wave (#611).

**Verdict.** The clip model's mapping seam (§2.4) has its second implementation, and the
clip model did not change to get it: `WarpMarkers` is a child element of `<sampleclip>`,
`SampleClip::sourceFrameAt`/`timelinePosAt` gained a branch, and the clip has a tempo
mode. Markers are pinned to **source frames**, so a trim moves `sourceIn`/`sourceOut` and
the markers stay on the audio; the map is **monotonic** and **exact at every marker**;
with no markers it is the pre-warp arithmetic bit for bit. A headless render proves the
rate is real: a source whose transients are at 0/1/2/3 s comes out at **0/0.5/1.0/1.5 s**
under a marker pair that declares 2×, while the base binary renders that same project as
if the `<warp>` element were not there at all.

**The stretch mode is resampling, so it changes pitch.** A 2× warp is an octave up.
True pitch-preserving stretch is a DSP project of its own and is **not** built — see §3.

**The default is tempo *follower*** (the project sets the rate). See §4.

---

## 1. The seam this attaches to, with evidence

`docs/CLIP-CAPTURE-DESIGN.md` §2.4 is the specification, quoted verbatim:

> The mapping the model exposes is *one* function, and it is the seam #597 attaches to:
> ```cpp
> virtual f_cnt_t sourceFrameAt(TimePos timelinePos) const;   // linear today
> virtual TimePos timelinePosAt(f_cnt_t sourceFrame) const;   // inverse, for trimming
> ```
> Today's linear implementation is exactly the existing code relocated. **#597 adds
> `WarpMarkers` as a child element and a second implementation of the same two virtuals,
> keyed off the clip's source tempo. Nothing else in the clip model changes when warp
> lands.**

| What | Where (post-change line numbers) |
|---|---|
| The two virtuals | `include/Clip.h:156`, `include/Clip.h:160` |
| The base implementation (tick identity, for MIDI/automation) | `include/Clip.h:209`, `include/Clip.h:216` |
| `SampleClip`'s declarations | `include/SampleClip.h:115-116` (line numbers moved; the signatures are unchanged — the freeze held) |
| Today's linear implementation, now branched | `src/core/SampleClip.cpp:413`, `src/core/SampleClip.cpp:437` |
| The authored window | `include/SampleWindow.h:47`, `include/SampleClip.h:185` (`m_window`) |
| The two playback call sites | `src/tracks/SampleTrack.cpp:130-131` (unchanged by this lane) |
| The handle that renders what the pass derived | `src/core/SamplePlayHandle.cpp:154` |

**No new virtual, no new argument, no change to `Clip.h`.** The whole attachment is two
new declarations on `SampleClip` plus a branch inside the two existing overrides.

---

## 2. The marker model, and the mapping it defines

### 2.1 `WarpMarker` — pinned to a source frame

`include/WarpMarkers.h:51`:

```cpp
struct WarpMarker
{
    f_cnt_t sourceFrame = 0;    // a position on the audio
    tick_t  offsetTicks = 0;    // measured from the clip's own origin
};
```

`sourceFrame` is what the design asks for and what makes trim and warp compose: *"warp
markers are stored against the source frame they pin, so they are independent of trim —
trimming moves `sourceIn`/`sourceOut` and the markers stay where they are on the audio"*
(§2.4). `offsetTicks` is measured from `startPosition() + startTimeOffset()` rather than
from the project start so that **moving the clip moves its warp with it**; the mapping
functions still speak absolute `TimePos` (the clip adds its origin back), so nothing above
`SampleClip` sees the difference. `tests/src/core/ClipWarpPersistenceTest.cpp`
`warpRoundTripsThroughTheProjectFile` pins that: with the clip at tick 96, its marker at
`offsetTicks = 96` reports `timelinePosAt(176400) == 192`.

### 2.2 `WarpMarkers` — a value type with a fixed capacity

`include/WarpMarkers.h:90`, capacity `MaxMarkers = 128` (`:94`). Everything the audio
thread can reach is a plain array walk:

* `set(span)` (`:113`) sorts by source frame and **rejects the whole set** — returning
  false and leaving the previous one intact — unless it is strictly increasing in *both*
  coordinates. A non-monotonic set is not a mapping, so it is refused rather than
  repaired; the same rule is applied through the clip
  (`SampleClip::setWarpMarkers`, `src/core/SampleClip.cpp:308`) and reported as a project
  error rather than silently dropped.
* `append` (`:137`) keeps the order. Both authoring entry points use a stack array, so
  neither allocates.
* `sourceFrameAt` (`:154`), `timelineOffsetAt` (`:185`), `framesPerTickAt` (`:219`) are
  `const`, allocation-free and lock-free — the audio thread calls them (I8).

### 2.3 The mapping, stated as rules

With **no markers there is no warp**: `SampleClip::sourceFrameAt`
(`src/core/SampleClip.cpp:413`) runs the pre-warp expression
`sourceIn + framesPerTick * (pos - startPosition - startTimeOffset)`, clamped to
`[sourceIn, sourceOut)`, verbatim.

With markers, the markers **are** the mapping:

1. between two markers, interpolate **linearly in both coordinates** — a constant
   source-frames-per-tick rate for that segment, which is what makes a marker pair a
   tempo change rather than a curve;
2. outside the outermost markers, continue at the clip's own base rate
   (`clipFramesPerTick()`), extrapolated **from the nearest marker**. The intercept comes
   from the marker, not from the clip origin, because anchoring to the origin instead
   would put a **step** at the first marker;
3. clamp the result into the clip's current window `[sourceIn, sourceOut)`.

Two properties follow, and they are the two the task's acceptance names:

* **Exact at every marker.** The interpolation numerator is zero at a marker, so its own
  source frame comes back with no interpolation error — in both directions.
* **Monotonic.** Every segment and both tails increase in both coordinates, and clamping
  a monotone function preserves monotonicity.

`tests/src/core/WarpMarkersTest.cpp` measures both across the fixture
`A = (10000, 20)`, `B = (30000, 60)`, `C = (50000, 120)` on a window `[0, 100000)` at a
base rate of 100 frames/tick — so the segment slope (500) and the base rate (100) are
different numbers everywhere but the tails and a mapping that used the wrong one cannot
pass. `tests/src/tracks/SampleClipWarpTest.cpp` repeats it through the clip, with literal
expected values (offset 36 → source 66150, offset 60 → 110250, offset 84 → 154350).

One convention is worth writing down because a render depends on it: **segments are
half-open to the right** (`[marker_i, marker_{i+1})`), so `framesPerTickAt` at a marker
returns the *next* segment's slope — the rate that governs from that source frame onwards.
The two mapping functions are closed at the markers; only the rate is half-open.

### 2.4 Persistence — additive, and only when there is something to say

`src/core/SampleClip.cpp:491` writes the element, `:565` reads it:

```xml
<sampleclip pos="96" len="768" muted="0" src="take.wav" off="0" autoresize="0"
            srcin="44100" srcout="264600">
  <warp mode="source" tempo="128.5">
    <marker src="44100" pos="24"/>
    <marker src="176400" pos="96"/>
  </warp>
</sampleclip>
```

* The `<warp>` element is written **only** when the clip has markers *or* does not follow
  the project tempo, so a project without warp serialises exactly as task #611 left it —
  no new attribute, no new element, no `UPGRADE_METHODS` entry, and no
  `ELEMENTS_WITH_RESOURCES` entry (the markers carry no `src`).
* `loadSettings` reads it **after** `len`/`off`/`srcin`/`srcout`/`autoresize`, i.e. after
  the window it clamps into and after `setSampleFile()` — which is the call that replaces
  the source the markers are anchored to. A clip that names a *different* source clears
  its markers (`:171`); the copy constructor's `setSampleFile("")` deliberately does not,
  so `clone()` and `Clip::copyStateTo` carry the warp.
* A hand-edited file with a non-monotonic marker list is refused with a project error, not
  loaded half-way.

---

## 3. The stretch mode, and what a user would hear

**The stretch is resampling. It changes pitch.** `SamplePlayHandle::warpRatio()`
(`src/core/SamplePlayHandle.cpp:166`) hands `Sample::play` a resampler ratio derived from
the marker segment's source-frames-per-tick, and `Sample::play` passes it to
`AudioResampler` (`src/core/AudioResampler.cpp:78`, libsamplerate). There is no phase
vocoder, no WSOLA, no transient preservation and no formant handling anywhere in this
change.

What a user hears, stated plainly:

* a clip warped to **2× plays twice as fast and an octave higher**;
* a clip warped to **0.5× plays half as fast and an octave lower**;
* the pitch change is *exactly* the rate change — the two are the same number.

That is a defensible design (it is what a sampler does, and it is what "varispeed" means),
but it is **not** pitch-independent time-stretching and this report does not claim it is.
A pitch-preserving stretch would need its own DSP module and its own tests; it is not in
this change.

Two further honest limits on the rendering:

* **One rate per audio period.** `Sample::play` takes a single ratio per call, so the rate
  is chosen from the source frame each period starts on and held for that period. The
  *mapping* is exact at every marker; the *rendered* rate changes at the next period
  boundary after one, i.e. up to one period late (≤ 1024 frames ≈ 23 ms at 44.1 kHz with
  the default period). A two-marker set that covers the whole clip — the case the render
  proof measures — is exact everywhere, because the rate never changes.
* **The length follows the mapping, not the frame count.**
  `SamplePlayHandle::totalFrames()` (`:218`) returns the window's **timeline span** under
  the mapping when the clip is warped or leads, so a 2× segment consumes its source frames
  in half the output frames and stops there, instead of playing silence for the second
  half of the clip.

### 3.1 A defect found while building this, and what it forced

`Sample::play`'s `ratio` is documented as *"output sample rate divided by input sample
rate"* (`include/AudioResampler.h:96-97`), but the converter it drives —
`AudioResampler::Mode::Linear` → libsamplerate `SRC_LINEAR`
(`src/core/AudioResampler.cpp:40-41`) — treats it as **input/output**, that converter's
long-standing inversion. **Measured**, not assumed: a ratio of 2.0 makes the source
advance at half a frame per output frame. The first render proof run showed it exactly —
the warped clip's first 0.2 s burst came out as 0.4 s of tone at half the pitch, and the
whole window was consumed in twice the frames.

`warpRatio()` therefore passes the **reciprocal** of the speed it wants, with the reason
written at the call site (`src/core/SamplePlayHandle.cpp:181-196`).
`SampleClipWarpTest::theResamplerRatioConventionIsPinned` asserts the inversion as a
measurement (ratio 2.0 consumes half the source frames of ratio 1.0, within ±10 %), so the
day somebody fixes the converter that test goes red and the reciprocal gets removed
deliberately instead of silently cancelling out.

**The same inversion is a live defect on the pre-existing path and is NOT fixed here**: a
48 kHz source in a 44.1 kHz project passes `sampleRateRatio = 44100/48000`, which the
converter reads as 0.919 input frames per output frame — the clip would play ~8.8 % fast
and sharp. It is out of this task's scope (changing it changes the sound of every project
with a mismatched source rate, which is exactly the behaviour change AGENTS.md rule 5
forbids without a decision). Reported, not touched.

---

## 4. Tempo leader / follower, and which is the default

`include/SampleClip.h:57`:

```cpp
enum class WarpTempoMode { FollowProject = 0, SourceTempo = 1 };
```

`SampleClip::clipFramesPerTick()` (`src/core/SampleClip.cpp:292`) is the single place the
tempo enters the mapping:

| mode | rate | meaning |
|---|---|---|
| `FollowProject` (**default**) | `Engine::framesPerTick(sampleRate)` | the project tempo decides how many ticks the clip's audio spans; the audio plays at its natural rate |
| `SourceTempo` | the same × `projectTempo / sourceTempo` | the clip **leads**: it declares the tempo it was recorded at, and one bar of its music occupies one bar of the project whatever the project tempo is |

**Why follower is the default**, in one line: it is what every project written before this
task does. `FollowProject` makes `clipFramesPerTick()` return exactly
`Engine::framesPerTick(m_sample.sampleRate())`, i.e. the very expression the linear map
used before, so the mapping is unchanged bit for bit and no existing project's sound moves.
Making `SourceTempo` the default would re-time every clip in every existing project — the
behaviour change AGENTS.md rule 5 (and the task's own behaviour-preservation rule) forbids.

Two properties are asserted in `sourceTempoLeadsAndTheDefaultFollows`:

* a leader whose declared tempo **equals** the project's is identical to a follower
  (same rate, same tick length) — the mode is a scaling, not a second engine;
* a leader at half the project tempo plays at exactly twice the rate and its tick length
  halves.

The scope of `SourceTempo` here is the **clip's own rate**. It does not write anything
back to the project tempo, it does not drive a tempo map, and it does not re-fit the
clip's stored `len` when the mode changes (a later lane with a UI must do that as one
journalled edit).

---

## 5. The proofs

Everything below is from this worktree's build (`build/`), headless, on the CI linux-x86_64
flag set plus the documented `-DWANT_QT6=ON` deviation.

### 5.1 The unit suites

```
$ cmake --build build -j2 --target WarpMarkersTest SampleClipWarpTest ClipWarpPersistenceTest   # EXIT=0
$ cd build/tests
$ QT_QPA_PLATFORM=offscreen ./WarpMarkersTest          ; echo EXIT=$?   # EXIT=0  12 passed, 0 failed
$ QT_QPA_PLATFORM=offscreen ./SampleClipWarpTest       ; echo EXIT=$?   # EXIT=0  13 passed, 0 failed
$ QT_QPA_PLATFORM=offscreen ./ClipWarpPersistenceTest  ; echo EXIT=$?   # EXIT=0   5 passed, 0 failed
$ QT_QPA_PLATFORM=offscreen ./SampleClipWindowTest     ; echo EXIT=$?   # EXIT=0  12 passed, 0 failed (Slice 0 unregressed)
$ QT_QPA_PLATFORM=offscreen ./ClipSerialisationTest    ; echo EXIT=$?   # EXIT=0   5 passed, 0 failed (Slice 1 unregressed)
```

The four acceptance items, and the slot that holds each:

| Acceptance | Test | What it asserts |
|---|---|---|
| monotonic | `WarpMarkersTest::monotonicAcrossTheWholeRange`, `SampleClipWarpTest::interpolatesBetweenMarkersWithRealValues` | a sweep across both tails and every segment never decreases, in both directions |
| exact at every marker | `WarpMarkersTest::exactAtEveryMarker`, `SampleClipWarpTest::markersAreExactThroughTheClip` | `sourceFrameAt(marker.offsetTicks) == marker.sourceFrame` and the inverse, for every marker, through the clip as well as the map |
| interpolation with real expected values | `interpolatesLinearlyBetweenMarkers` / `interpolatesBetweenMarkersWithRealValues` | hand-computed literals (offset 40 → 20000; 36 → 66150; 60 → 110250; 84 → 154350) |
| clamping to `[sourceIn, sourceOut)` | `resultIsClampedToTheWindow` | the clamp bites at both ends, and clamping preserves monotonicity |
| persistence round trip | `ClipWarpPersistenceTest::warpRoundTripsThroughTheProjectFile` | markers, mode and tempo come back; the element re-saves identically once the per-object journalling id is neutralised |
| old project loads | `ClipWarpPersistenceTest::anOldProjectWithoutAWarpElementLoads` | an element written exactly as #611 wrote it (no `<warp>`) loads with every #597 default **and the linear map** |
| no markers = the linear map | `WarpMarkersTest` (empty set) + `SampleClipWarpTest::noMarkersIsThePreWarpArithmetic` | the pre-warp expression recomputed verbatim at clip positions {0, 96} × transport {start, +1, +48, +len−1} compares equal, 8/8 |
| marker independence from trim | `markersSurviveATrim` | after a trim the marker frames and offsets are unchanged, `sourceFrameAt` still returns the same source frames, and a trim that cuts a marker out of the window clamps instead of reading past it |
| warp changes the rate | `playHandleLengthFollowsTheWarp`, `warpShortensTheClipsTimelineLength` | a warped clip's handle is shorter than the plain one and equals the mapping's own timeline span |
| leader / follower | `sourceTempoLeadsAndTheDefaultFollows` | leader == follower at equal tempo; leader at half tempo = 2× rate and half the ticks |
| I1 under warp | `aPlaybackPassLeavesTheWarpAlone` | a real pass through `Song → SampleTrack::play → AudioEngine` schedules a handle and writes neither the window nor the markers |

I9 (additive serialisation) is `ClipWarpPersistenceTest::aClipWithNoWarpSerialisesExactlyAsBefore`:
with no warp the clip's attribute set is exactly
`{autoresize, data, len, muted, off, pos, sample_rate, src}`, there is no `<warp>` child, and
a load/re-save is byte-identical after the journalling id is neutralised.

### 5.2 Realtime (I8)

`AllocationProbe.h`, dynamic, on the mapping path:

* `WarpMarkersTest::lookupDoesNotAllocate` — 10 000 iterations of
  `sourceFrameAt` + `timelineOffsetAt` + `framesPerTickAt` → **0 allocations**;
* `SampleClipWarpTest::mappingDoesNotAllocate` — 2 000 × (`clip->sourceFrameAt` +
  `clip->timelinePosAt` + `handle.totalFrames()`) on a clip that is *both* warped **and** a
  tempo leader, then 16 periods of `SamplePlayHandle::play` through it → **0 allocations**.

Nothing on the path locks: the map is a fixed array, the handle's snapshot is a POD copy
made at construction, and the only shared state read is `Engine::framesPerTick`.

### 5.3 Behaviour preservation — measured, not hashed

This tree's renders are **not bit-reproducible run to run** (established independently by
the stem-export lane, the Slice-0 lane and the alpha baseline, and present in stock LMMS
too), so byte identity is not the proof. `tests/data/warp/render-proof.sh` renders the same
project twice per binary, takes the spread as the floor, and compares across the change.

```
$ bash tests/data/warp/render-proof.sh --base "$PWD/build/lmms-base" --warp "$PWD/build/lmms"
render-proof verdict exit=0
```

Run three times; every run held all nine verdicts. Two of the runs, side by side:

| comparison (best-aligned samples, floor measured in the same run) | run A: differing / max │Δ│ | run B: differing / max │Δ│ |
|---|---|---|
| base binary vs itself, run 1 vs run 2 | 17 640 / 0.07 LSB | 17 638 / 0.50 LSB |
| warp binary vs itself, run 1 vs run 2 | 15 510 / 0.50 LSB | 1 834 / 0.50 LSB |
| **base vs warp, project with no warp** | **1 768 / 0.50 LSB** | **184 / 0.50 LSB** |
| **base vs warp, project with no warp (run 2)** | **1 834 / 0.50 LSB** | **16 616 / 0.50 LSB** |
| warp binary, warped project vs plain (**sensitivity control**) | **87 334 / 0.98 FS** | **87 996 / 0.99 FS** |
| base binary, warped project vs plain | 16 616 / 0.50 LSB | 6 612 / 0.50 LSB |

Tolerance for both runs: `max(2 × floor, floor + 512)` = 35 280 and 35 276 of 528 896 samples.

Readings:

* **The change is at or below the noise floor and the sensitivity control is 2.5× outside
  it.** A project with no warp differs from the base binary by **0.5 LSB peak on 0.03–3.1 %
  of samples** — at worst the same as the 3.3 % the base binary differs from *itself*, and
  at best 96× smaller — while the warped project differs from the unwarped one by
  **~1 FS on 16.6 % of samples**. The comparator is not blind, and the code path this lane
  added is the only thing between those two columns.
* **Byte-identity is reported when it happens and never asserted.** In neither of these two
  runs did the cross-binary pair share a sha256; in the run before them,
  `base-plain-run1` and `warp-plain-run1` had the *same* sha256
  (`20577555b866ecc4…`, 0 differing samples). All three outcomes are the renderer's
  run-to-run jitter, which is exactly why the floor — not a hash — is the test.
* **The base binary cannot see `<warp>` at all** — it renders the warped project as the
  plain one (0.5 LSB peak, inside the floor; identical onset table). That is what "markers
  were invisible before this task" means.

### 5.4 The render's real measurement (the acceptance item)

The fixture source is 4 s of 1 kHz bursts, 0.2 s long, at the start of each second, in a
120 bpm project whose clip is 384 ticks (4 s) long. The warped project adds
`<warp mode="follow"><marker src="0" pos="0"/><marker src="176400" pos="192"/></warp>` —
the whole source pinned to tick 192 instead of the 384 the project's own rate gives it:
**2×**, with the clip's first frame pinned exactly where the unwarped map already puts it,
so the two projects differ *only* in the rate.

| render | measured burst onsets (s) |
|---|---|
| base, plain project | 0.000, 1.000, 2.000, 3.000 |
| warp, plain project | 0.000, 1.000, 2.000, 3.000 |
| **warp, warped project** | **0.000, 0.500, 1.000, 1.500** |
| base, warped project | 0.000, 1.000, 2.000, 3.000 |

and the warped render is **silent from 2 s on** (`-inf dB` over 2.05–5.9 s) where the
unwarped render still has its 3 s burst (`-19.4 dB`), because the mapping says the window's
4 s of audio spans 2 s of timeline. Measured against the prediction with a ±20 ms
tolerance, the onsets land **exactly** — this is a real rate change, not "it changed".

### 5.5 Build and test, unpiped

```
$ JOBS=2 CTEST_JOBS=1 bash tools/local-ci.sh --build-dir build --jobs 2
configure EXIT=0            (build/configure.log)
build EXIT=0                (build/build.log)
ctest EXIT=0                (build/ctest.log), run from build/tests
ctest totals: 100% tests passed, 0 tests failed out of 32
local-ci: overall exit=0
  deviation: Qt5 development files not found: added -DWANT_QT6=ON
```

32 = the 29 test programs of `post-alpha/clip-slice0` plus this lane's three new
executables (`WarpMarkersTest`, `SampleClipWarpTest`, `ClipWarpPersistenceTest`). ctest
counts programs, not slots; the slot totals are in §5.1.

`--jobs 2` and `CTEST_JOBS=1`, not the 4 the brief suggests: the first attempt at `-j 4`
was **terminated** by the box (sibling lanes building at the same time;
`gmake: *** Deleting file … Terminated` on four targets at once, with 4 GB of a 7 GB swap
in use on a 30 GB machine), and a parallel ctest run made `PdcMixerTest` — a test this
change does not touch — abort in `cleanupTestCase` while the serial run passes 32/32.
Both are machine limits, not code limits, and they are recorded rather than retried away.

### 5.6 Gates

All three, unpiped, on the committed tree (`217e78885`):

| gate | command | exit | what it said |
|---|---|---|---|
| 9 fork-sources | `bash tests/fork-sources-gate.sh` | **1** | **all four of this lane's new sources are `fork-sources (registered)`**; the violation is 3 files from another lane: `tests/src/core/LufsMeterTest.cpp`, `MidiLearnTest.cpp`, `SessionModelTest.cpp` |
| 6 upstream regression | `bash tests/no-upstream-regression-gate.sh` | **1** | **all four upstream files this lane changed print `declared divergence -> …#597 warp: …`**; the violations are 4 files from the MIDI-learn merge: `include/MainWindow.h`, `include/MidiController.h`, `src/core/midi/MidiAlsaSeq.cpp`, `src/core/midi/MidiClient.cpp` |
| all gates | `bash tests/run-all-gates.sh` | **1** | `RESULT: FAIL` — see the table below |

```
1      ctest                    PASS
2      coverage                 SKIP
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 PASS        kill score 88.5% >= 80%
6      upstream-regression      FAIL        pre-existing
7      file-length              FAIL        pre-existing
8      duplication              PASS        0.88% duplicated lines (budget 5%)
9      fork-sources             FAIL        pre-existing
```

**The brief expected 0 / 0 / 3. The measured answer is 1 / 1 / 1**, and the reason is
three pre-existing reds, not this change:

* **6 and 9** are the violations the Slice 0 lane reported as inherited from the MIDI-learn
  merge (`e2689e62b`). Every file *this* lane added is registered and every file it changed
  is declared; the seven offending paths are all present at the base commit `903d70916`
  and untouched here (`git cat-file -e 903d70916:<path>` for each). Registering another
  lane's test files is not a no-op: `fork-sources.txt` enrolment is what puts a file under
  the coverage, complexity, file-length and duplication ratchets, and that is that lane's
  decision — the same call the Slice 0 and stem-export lanes made.
* **7 is new information.** The file-length ratchet is red **at the base commit**:
  `tests/src/tracks/SampleClipWindowTest.cpp` is **511 lines**, it is 511 lines at
  `903d70916`, this lane never touches it (`git diff 903d70916 HEAD --` on that path is
  empty), and it is in neither `file-length-baseline.tsv` nor `file-length-baseline-all.tsv`.
  **The Slice 0 report's "gate 7 PASS" does not reproduce** — that lane's own new test file
  is the regression. It is reported here rather than silently absorbed: trimming another
  lane's file would not change this run's verdict, because 6 and 9 stay red regardless.
  This lane's own four new files are 467, 258, 268 and 381 lines, all under the 500 limit.

Exit 3 (PASS-WITH-SKIPS) is unreachable on this base for that reason: only gate 2 is a
skip, and a skip plus a failure is a failure.

---

## 6. What is NOT done

* **No UI, and no gesture.** Nothing in `src/gui/` was touched: there is no warp-marker
  handle, no drag, no context-menu entry, no keyboard action, and no way to place the first
  marker from the application. `include/ClipView.h`'s action set is unchanged. A user can
  only author a warp by editing the project file or through the API. **What a user lacks,
  concretely: every gesture.** The model, the mapping, the persistence and the render are in
  place for the lane that adds them.
* **No grid quantise and no groove templates.** A warp marker takes the position it is
  given; nothing snaps it to the project grid and nothing reads a groove map. Those ride
  warp later, as the task says.
* **No take lanes and no comping** (#600, and the #611 wave's slices D/F). Untouched.
* **No pitch-preserving stretch.** See §3 — this is resampling and the pitch moves with the
  rate.
* **One rate per audio period**, i.e. up to one period of latency on a rate *change* (exact
  at the markers themselves). A sample-accurate rate curve needs the resampler to accept a
  rate ramp, which it does not.
* **`MaxMarkers = 128`** is a hard cap: a set past it is refused, not truncated. Fixed
  capacity is what keeps the audio-thread lookup allocation-free; a larger or unbounded set
  needs a pre-allocated pool and a decision about what happens at the cap.
* **Changing the tempo mode does not re-fit the clip's stored `len`.** A `SourceTempo` clip
  reports the new timeline length through `sampleLength()`, but the clip keeps the tick
  length it had until something calls `updateLength()`; with no UI there is no gesture to
  make that atomic, and the design assigns the atomic edit to the gesture lane (I5).
* **`SourceTempo` is a per-clip rate, not a tempo map.** It does not write the project
  tempo, it is not persisted as part of any song-level tempo state, and it does not feed
  `Engine::framesPerTick`.
* **The resampler's ratio inversion is reported and not fixed** (§3.1) — it is a live
  defect on the pre-existing sample-rate-conversion path (a 48 kHz source in a 44.1 kHz
  project plays ~8.8 % fast), and fixing it is a behaviour change to every such project.
* **Moving a clip moves its markers**, by construction (`offsetTicks` is clip-relative),
  and that is asserted; **deleting the source file and reloading clears the markers**, which
  is asserted only indirectly (through `setSampleFile`) and is the behaviour a user should
  expect — the frames no longer exist.

---

## 7. Reproduction

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-warp
JOBS=2 CTEST_JOBS=1 bash tools/local-ci.sh --build-dir build --jobs 2   # configure + build + ctest
cd build/tests && ctest -R 'Warp|SampleClip' --output-on-failure
QT_QPA_PLATFORM=offscreen ./SampleClipWarpTest -v1

# the render proof needs a binary from BEFORE this change (the parent commit):
BASE_COMMIT=903d70916     # post-alpha/clip-slice0
cp build/lmms build/lmms-warp
git checkout "$BASE_COMMIT" -- include/SampleClip.h include/SamplePlayHandle.h \
                               src/core/SampleClip.cpp src/core/SamplePlayHandle.cpp
cmake --build build -j2 --target lmms && cp build/lmms build/lmms-base
git checkout HEAD -- include/SampleClip.h include/SamplePlayHandle.h \
                     src/core/SampleClip.cpp src/core/SamplePlayHandle.cpp
cmake --build build -j2 --target lmms
bash tests/data/warp/render-proof.sh --base "$PWD/build/lmms-base" --warp "$PWD/build/lmms"
```
