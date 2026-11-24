FIXED

# cluster new-p9-431 - Replacing the SlicerT sample races with active playback

Line numbers below are from the tree before this change (`9913d36d53`), read with
`git show HEAD:<file> | nl -ba`; the current line numbers are in the fix section.

## Verdict: REAL

The render path read the instrument's *editing* state, and a control thread replaced that state:

* `plugins/SlicerT/SlicerT.cpp:176` `if (m_originalSample.sampleSize() <= 1) { return; }`,
  `:208` `sliceStart * m_originalSample.sampleSize()`, `:229` `sliceEnd * m_originalSample.sampleSize()`,
  `:233` `m_originalSample.play(...)`, `:254` `... / m_originalSample.sampleSize()` - all inside
  `SlicerT::playNoteImpl`, which runs per period on the render thread.
* `plugins/SlicerT/SlicerT.cpp:195-200` read `m_slicePoints[noteIndex]` / `[noteIndex + 1]` from a
  `std::vector<float>` (`plugins/SlicerT/SlicerT.h:129`) in the same per-period path.
* Writers of the same two members: `SlicerT.cpp:587` (`updateFile` assigns `m_originalSample`),
  `SlicerT.cpp:647` and `:658` (`loadSettings` assigns it from a file or from base64),
  `SlicerT.cpp:664-666` and `findSlices()` (`:437`, `:486-527`) rebuild `m_slicePoints`;
  `SlicerTView.cpp:303` (drop) and `:268` (file dialog) call `updateFile`, and
  `SlicerTView.cpp:230-235`, `SlicerTWaveform.cpp:403`, `:424` and `:507` rewrite `m_slicePoints`
  from the GUI.
* The audio data is owned by a **non-atomic** `std::shared_ptr<const SampleBuffer>`
  (`include/Sample.h:148`) that `Sample::play` dereferences per call (`src/core/Sample.cpp:102`,
  `:106`) and that `Sample::render` walks inside the render loop
  (`src/core/Sample.cpp:243-244`, `m_buffer->data()[...]`). So `m_originalSample = Sample(...)` in
  `updateFile` both writes a `shared_ptr` a render thread is reading and can drop the last reference
  to the buffer a sounding note is still reading from: a data race on the `shared_ptr` object plus a
  use-after-free of the audio data. There is no synchronisation between the two paths.

Reproduced before the fix: the new regression test rendered a constant sample under a held note,
replaced the instrument's sample with a file that opens with a fifth of a second of silence, and the
**sounding** note measured peak `0` - it had switched to the replacement's (silent) buffer:

```
FAIL!  : SlicerTPlayheadTest::replacingTheSampleKeepsASoundingNoteOnItsSource() 'afterReplacement > 0.3f' returned FALSE. (the sounding note switched samples: last-period peak 0)
   Loc: [../../tests/src/core/SlicerTPlayheadTest.cpp(586)]
```

## The fix (at the cause)

`plugins/SlicerT/SlicerT.h:96-100` adds an immutable publication:

```cpp
struct PlaybackSnapshot { Sample sample; std::vector<float> slicePoints; };
```

`SlicerT::publishPlaybackSnapshot()` (`plugins/SlicerT/SlicerT.cpp:306-322`, declared at
`SlicerT.h:122`) builds one from `m_originalSample` + `m_slicePoints` and swaps it in under
`AudioEngine::requestChangesGuard()`. `AudioEngine::renderNextPeriod()` opens with
`std::lock_guard{m_changeMutex}` (`src/core/AudioEngine.cpp:1224`) and holds it for every render
stage, and the guard takes the same recursive mutex (`include/AudioEngine.h:994`, `:661`;
`src/core/AudioEngine.cpp:1983-1995`), so a publication and a render period are mutually exclusive:
the render thread's note-start read cannot see a half-made replacement, and nothing is added to the
audio thread's locking (the period lock is the engine's own, inherited).

`SlicerT::playNoteImpl` now reads only a publication: the one the note **pinned** into its
`PlaybackSlot` on its first period (`SlicerT.cpp:188-201` reads it, `:233-246` pins it,
`SlicerT.h:116` holds it), or, on that first period, the one current now. Notes started later get the
new publication. A replaced publication is retained in `m_retiredSnapshots`
(`SlicerT.cpp:315-321`, `SlicerT.h:148-149`) while a note still pins it and is destroyed on the
control thread when `use_count() == 1` - the same retire rule and the same contract the accepted
sibling fix for AudioFileProcessor uses (`plugins/AudioFileProcessor/AudioFileProcessor.cpp:473-489`,
`tests/upstream-modifications.txt:6098`, cluster new-p9-552: "a sounding note captures its sample
source on its first period and renders it for its whole life").

Publication points cover every writer found above: `updateFile` (`SlicerT.cpp:638`), `updateSlices`
(`:651`), `loadSettings` (`:743`), the constructor (`:125`), `SlicerTView::clearSlices`
(`SlicerTView.cpp:237`) and the three waveform edits (`SlicerTWaveform.cpp:405`, `:429`, `:513`).
`SlicerT::getSampleName`, `saveSettings`, `exportMidi` and the waveform drawing keep reading the
editing state; they are GUI/control-thread readers of state only the control thread writes.
`SlicerT::deleteNotePluginData` releases the note's reference (`SlicerT.cpp:294`).

Behavioural consequence, deliberate and matching that sibling contract: a **sounding** note keeps the
sample and slice map it started on; the next note gets the new ones. Before the fix a sounding note
followed a replacement into a buffer that could be freed under it.

## Test matrix

| path | test | result |
| --- | --- | --- |
| normal path (a note sounds its sample) | `replacingTheSampleKeepsASoundingNoteOnItsSource`, first assertion (`> 0.3f`) | PASS |
| the bug (replacement while a note sounds) | same test, assertion after `updateFile` | PASS with fix, **FAIL without it** (peak `0`) |
| reconfiguration (a later note uses the replacement) | same test, fresh note after the replacement (`< 0.1f`) | PASS |
| exhaustion / overflow | `aSoundingSliceNoteEmitsItsPlayheadFromTheGuiThreadOnly` drives 130 notes past the 128-slot pool and reads `slicert_state_overflow`; `deleteNotePluginData` still runs (now also clearing the pinned publication) | PASS |
| note teardown while the instrument is removed | `deletingSlicerTrackCancelsActivePlaybackState` | PASS |
| audio-thread constraints (no allocation at note start) | `firstNoteOnFreshSlicerInstrumentAllocatesNothing` (AllocationProbe, `QCOMPARE(allocations, 0)`) | PASS |
| audio-thread constraints (static sweep) | `RtSafetySweep` + `RtSafetySelfTest` over `SlicerT::playNoteImpl`, `acquirePlaybackSlot`, `deleteNotePluginData` | PASS |
| slice-edit siblings (controls, zoom, waveform, saved settings) | `SlicerTSliceControlsTest`, `SlicerTZoomTest`, `SlicerTWaveformTest` (rebuilt after the header change) | PASS |
| sampler/sample-replacement neighbours | `SamplerVoiceReuseTest`, `SamplerTakeTest`, `SamplerTakeGuiTest` | PASS |
| supported-platform build | Qt 5/Qt 6 and MSVC: only `std::shared_ptr`/`std::vector`/`<memory>`, no Qt-6-only API, no POSIX call, `RequestChangesGuard` default-constructs when the engine is null (used by the tests that build `SlicerT instrument(nullptr)`) | builds and passes here (Qt 6); not cross-built on MSVC in this session |

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-431.r1/logs`. Files numbered 1-8
in that folder predate this session (an earlier attempt on this cluster that left no commit); none of
them is cited here.

| step | command | exit | log |
| --- | --- | --- | --- |
| RED build | `cmake --build build --target zene SlicerTPlayheadTest slicert -- -j4` | 0 | build-9-red.log |
| RED test | `cd build/tests && ctest -R '^SlicerTPlayheadTest$' --output-on-failure` | 8 | test-9-red.log |
| GREEN build | `cmake --build build --target zene SlicerTPlayheadTest slicert -- -j4` | 0 | build-10-green.log |
| GREEN test | `cd build/tests && ctest -R '^SlicerTPlayheadTest$' --output-on-failure` | 0 | test-10-green.log |
| family (before the other binaries were rebuilt) | `ctest -R '^(SlicerTPlayheadTest\|SlicerTZoomTest\|SlicerTWaveformTest\|SlicerTSliceControlsTest\|SamplerTakeTest\|SamplerTakeGuiTest\|SamplerVoiceReuseTest)$'` | 8 (3 "Not Run": binaries absent) | test-11-family.log |
| build the missing family binaries | `cmake --build build --target SamplerVoiceReuseTest SamplerTakeTest SamplerTakeGuiTest -- -j4` | 0 | build-11-family.log |
| family | as above | 0 | test-12-family.log |
| rt-safety and complexity sweeps | `cd build/tests && ctest -R '^(RtSafetySweep\|RtSafetySelfTest\|ComplexityGate\|ComplexityRegression)$' --output-on-failure` | 0 | test-13-gates.log |
| 4b patch | `git diff -- plugins/SlicerT/SlicerT.h plugins/SlicerT/SlicerT.cpp plugins/SlicerT/SlicerTView.cpp plugins/SlicerT/SlicerTWaveform.cpp > $LOGS/fix.patch` | 0 | fix.patch |
| 4b revert | `git apply -R $LOGS/fix.patch` | 0 | - |
| 4b rebuild | `cmake --build build --target zene SlicerTPlayheadTest slicert -- -j4` | 0 | build-12-red-reverted.log |
| 4b test (must fail) | `cd build/tests && ctest -R '^SlicerTPlayheadTest$' --output-on-failure` | 8 | test-14-red-reverted.log |
| 4b restore | `git apply $LOGS/fix.patch` | 0 | - |
| 4b rebuild | `cmake --build build --target zene SlicerTPlayheadTest slicert -- -j4` | 0 | build-13-green-restored.log |
| 4b test (must pass) | `cd build/tests && ctest -R '^SlicerTPlayheadTest$' --output-on-failure` | 0 | test-15-green-restored.log |
| rebuild the sibling SlicerT tests (header change) | `cmake --build build --target SlicerTZoomTest SlicerTWaveformTest SlicerTSliceControlsTest -- -j4` | 0 | build-14-slicer-tests.log |
| family, rebuilt | `ctest -R '^(...\|AudioFileProcessorPlayheadTest)$'` | 8 (that one binary is not built in this tree) | test-16-family-rebuilt.log |
| family, final | `ctest -R '^(SlicerTPlayheadTest\|SlicerTZoomTest\|SlicerTWaveformTest\|SlicerTSliceControlsTest\|SamplerTakeTest\|SamplerTakeGuiTest\|SamplerVoiceReuseTest)$' --output-on-failure` | 0 (7/7) | test-18-family-final.log |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | feature-check-2.log |

The 4b pair in one line: RED_EXIT=8 (`afterReplacement > 0.3f` returned FALSE, peak `0`) then
GREEN_EXIT=0, with the product patch reverted and restored by `git apply -R` / `git apply`.

`feature-check-2.log`:

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

## Why the design is sound without TSan (which does not run on this box)

* The only reader of the publication on the render thread is `playNoteImpl` (inside a render
  period); the only writer is `publishPlaybackSnapshot` (control thread, under the engine's
  model-change guard). `renderNextPeriod()` holds `m_changeMutex` for the whole period
  (`src/core/AudioEngine.cpp:1224`) and the guard takes the same `std::recursive_mutex`
  (`include/AudioEngine.h:994`), so reader and writer cannot overlap.
* The audio thread never frees: it copies the current `shared_ptr` once, on a note's first period
  (an atomic refcount increment, no allocation), and its release in `deleteNotePluginData` is the
  last one only if the plugin no longer holds the same object - which cannot happen while the plugin
  lives, because a publication is either current or in `m_retiredSnapshots`, and that list drops an
  entry only at `use_count() == 1` (no note pins it). The `use_count()` read is itself ordered by
  the guard, which the publisher takes after the pinning period ended.
* `RtSafetySweep` (test-13-gates.log) reports no new allocation, lock, growth or logging hit on
  `SlicerT::playNoteImpl`, `SlicerT::acquirePlaybackSlot` or `SlicerT::deleteNotePluginData`, and
  `firstNoteOnFreshSlicerInstrumentAllocatesNothing` measures `0` allocations across a note's first
  render (test-18-family-final.log).

## Registration

`tests/upstream-modifications.txt` gains one line per changed inherited file
(`plugins/SlicerT/SlicerT.cpp`, `.h`, `SlicerTView.cpp`, `SlicerTWaveform.cpp`), each naming the
regression test; the changed test file `tests/src/core/SlicerTPlayheadTest.cpp` is already a
fork-added file listed in `tests/fork-sources.txt:3012`. No new ctest executable, so
`tests/CMakeLists.txt` is unchanged. `fork-sources` and `no-upstream-regression` PASS
(feature-check-2.log).

## Self-check

Filled from `submit_check.py` (see below for the items it asked and the answers).

## Other findings

* The same "publish under the engine guard" shape is already in `AudioFileProcessor::replaceSample`
  (cluster new-p9-552) and `SampleClip::replaceSample`; SlicerT was the remaining plugin of that
  family that still read its editing state on the render thread.
* A publication retired by the last replacement that a sounding note pinned stays in
  `m_retiredSnapshots` until the next publication (or plugin destruction). That is at most one extra
  sample's memory while the user replaces samples repeatedly during playback, the same bound
  AudioFileProcessor accepts.
* Noticed, not touched: `SlicerT::findSlices()` sets `m_slicePoints[0] = 0` and overwrites the last
  point, so an onset detected at the very start of a file is dropped from the map (it becomes the
  slice start). Unrelated to this race; left as it is.
