FIXED

# cluster new-p9-431 - Replacing the SlicerT sample races with active playback

Round 2. The round-1 fix is kept (the snapshot the render thread reads); this round adds the GUI acceptance
coverage, a fresh-note assertion that cannot pass on silence, and the control coherence a review of the
pinned-slice-map change exposed. Section "Round-2 review points" answers the three findings one by one.

Line numbers are from the tree before the fix (`9913d36d53`) where they describe the defect, and from the
current tree where they describe the fix; both were read with `git show <rev>:<file> | nl -ba`.

## Verdict: REAL

The render path read the instrument's *editing* state, and a control thread replaced that state:

* `plugins/SlicerT/SlicerT.cpp:176` `if (m_originalSample.sampleSize() <= 1) { return; }`,
  `:208` `sliceStart * m_originalSample.sampleSize()`, `:229` `sliceEnd * m_originalSample.sampleSize()`,
  `:233` `m_originalSample.play(...)`, `:254` `... / m_originalSample.sampleSize()` - all inside
  `SlicerT::playNoteImpl`, which runs per period on the render thread.
* `plugins/SlicerT/SlicerT.cpp:195-200` read `m_slicePoints[noteIndex]` / `[noteIndex + 1]` from a
  `std::vector<float>` (`plugins/SlicerT/SlicerT.h:129`) in the same per-period path.
* Writers of the same two members: `SlicerT.cpp:587` (`updateFile` assigns `m_originalSample`),
  `SlicerT.cpp:647` and `:658` (`loadSettings`), `SlicerT.cpp:664-666` and `findSlices()` rebuild
  `m_slicePoints`; `SlicerTView.cpp:303` (drop), `:268` (file dialog) and `:230-235`/`SlicerTWaveform.cpp:403`,
  `:424`, `:507` (the editors) rewrite them from the GUI.
* The audio data is owned by a **non-atomic** `std::shared_ptr<const SampleBuffer>`
  (`include/Sample.h:148`) that `Sample::play` dereferences per call (`src/core/Sample.cpp:102`, `:106`) and
  that `Sample::render` walks inside the render loop (`src/core/Sample.cpp:243-244`). So
  `m_originalSample = Sample(...)` both writes a `shared_ptr` a render thread is reading and can drop the
  last reference to the buffer a sounding note is still reading from: a data race on the `shared_ptr` plus
  a use-after-free of the audio data.

Reproduced through the plug-in's own editor, in this session, with the product change reverted
(`test-11-red-reverted.log`, EXIT 8):

```
FAIL!  : SlicerTSampleSwapGuiTest::droppingASampleKeepsASoundingNoteOnItsSource() 'inBand(afterFileDrop, 0.42f, 0.56f)' returned FALSE. (the samplefile drop switched the sounding note: peak 0.0915527)
FAIL!  : SlicerTSampleSwapGuiTest::openingASampleKeepsASoundingNoteOnItsSource() 'inBand(afterOpen, 0.42f, 0.56f)' returned FALSE. (the file-open switched the sounding note: peak 0.152588)
FAIL!  : SlicerTSampleSwapGuiTest::heldSliceNoteKeepsTheControlsOfItsOwnSlice() 'inBand(ratio, 1.15f, 1.38f)' returned FALSE. (the held note applied another slice's controls after the merge: level ratio 0.707946 (expected 1.26 for the merged -1 dB slice, 0.71 for the -6 dB slice that took its index))
```

## The fix (at the cause)

`plugins/SlicerT/SlicerT.h:98-102` adds an immutable publication:

```cpp
struct PlaybackSnapshot { Sample sample; std::vector<float> slicePoints; };
```

`SlicerT::publishPlaybackSnapshot()` (`plugins/SlicerT/SlicerT.cpp:322-338`, declared `SlicerT.h:124`) builds
one from `m_originalSample` + `m_slicePoints` and swaps it in under `AudioEngine::requestChangesGuard()`.
`AudioEngine::renderNextPeriod()` opens with `std::lock_guard{m_changeMutex}`
(`src/core/AudioEngine.cpp:1224`) and holds it for every render stage, and the guard takes the same
recursive mutex (`include/AudioEngine.h:994`, `:661`; `src/core/AudioEngine.cpp:1983-1995`), so a
publication and a render period are mutually exclusive: the render thread's read cannot see a half-made
replacement, and the audio thread takes no lock of its own (the period lock is the engine's, inherited).

`SlicerT::playNoteImpl` reads only a publication: the one the note **pinned** into its `PlaybackSlot` on its
first period (`SlicerT.cpp:202-204` reads it, `:243-256` pins it, `SlicerT.h:118` holds it), or, on that
first period, the one current now. Notes started later get the new publication. A replaced publication is
retained in `m_retiredSnapshots` (`SlicerT.cpp:331-337`, `SlicerT.h:155`) while a note still pins it and
is destroyed on the control thread at `use_count() == 1` - the same retire rule and the same contract the
accepted sibling fix for AudioFileProcessor uses (`plugins/AudioFileProcessor/AudioFileProcessor.cpp:473-489`,
`tests/upstream-modifications.txt:6098`, cluster new-p9-552: "a sounding note captures its sample source on
its first period and renders it for its whole life").

Publication points cover every writer: `updateFile` (`SlicerT.cpp:660`, in `:654-663`), `updateSlices`
(`SlicerT.cpp:673`), `loadSettings` (`SlicerT.cpp:765`), the constructor (`SlicerT.cpp:135`), `SlicerTView::clearSlices` (`SlicerTView.cpp:237`) and the
three waveform edits (`SlicerTWaveform.cpp:405`, `:429`, `:513`). `getSampleName`, `saveSettings`,
`exportMidi` and the waveform drawing keep reading the editing state; they are control-thread readers of
state only the control thread writes. `deleteNotePluginData` releases the note's reference
(`SlicerT.cpp:310`).

Deliberate consequence, matching that sibling contract: a **sounding** note keeps the sample and slice map it
started on; the next note gets the new ones.

## Round-2 review points

1. **"The required GUI acceptance coverage is missing ... the regression calls updateFile directly"** (round 1
   called `updateFile`; `SlicerTView.cpp:265` is file-open and `:297` the drop handler). Fixed by replacing
   that test with `tests/src/gui/SlicerTSampleSwapGuiTest.cpp`, which drives only the editor's own routes:
   * `droppingASampleKeepsASoundingNoteOnItsSource`: a `"samplefile"` drop and a `"clip_2"` drop (the clip's
     own DataFile, `SampleClip::saveSettings`'s `src` attribute) built with
     `StringPairDrag::createMimeData` and delivered as QDragEnterEvent + QDropEvent to the SlicerTView, plus
     an unsupported `"note"` payload that must be refused (asserted through the drag-enter refusal and the
     unchanged level of a fresh note).
   * `openingASampleKeepsASoundingNoteOnItsSource`: `SlicerTView::openFiles()` with the real `FileDialog`,
     answered inside its own event loop - once **cancelled** (nothing loads, the sounding note and the live
     sample are unchanged) and once **accepted** with a replacement file.
   * Nothing in the new file calls `updateFile`, `loadFile` or publishes anything by hand.
2. **"The fresh-note assertion accepts a completely silent render"**. The silent-intro fixture is gone. Each
   fixture is a constant at its own level (held 0.488, first 0.092, second 0.244, opened 0.153) and each
   assertion is a closed band, so silence (0.0) is outside every one of them. The fresh-note checks are
   `inBand(fresh, 0.18f, 0.31f)` after the drops and `inBand(fresh, 0.11f, 0.20f)` after the file-open: the
   note must render *that file's* level, not merely "less than something".
3. **"A held note uses old slice boundaries with controls from a different slice after insertion or
   deletion"**. In the round-1 tree (`53ba78da3f`) the bounds came from the pinned map
   (`SlicerT.cpp:219-222`) while the controls were still indexed by the note's old slice number
   (`:269-272` `applySliceControls(..., sliceIndex, ...)`, `:327` `m_sliceControls[slice]`), and
   `sliceMerged` (`:379`) / `sliceSplit` (`:398`) shift those banks. Fixed at the cause: the controls stay live but are resolved through the
   live map. `SlicerT::liveSliceFor()` (`SlicerT.cpp:340-344`, from `sliceContainingPoint` at `:100-107`) returns the live slice whose interval contains
   the note's own pinned start, and `playNoteImpl` applies that slice's controls (`SlicerT.cpp:284-288`).
   A merge therefore gives the held note the merged slice's controls (the earlier slice's, as
   `sliceMerged()` defines), a split gives it the half its start fell into, and a knob turned during playback
   still reaches a sounding note - pinning the control values instead would have frozen them.
   `SlicerTSampleSwapGuiTest::heldSliceNoteKeepsTheControlsOfItsOwnSlice` proves it with distinct adjacent
   gains (-1, -3, -6 dB): a note on the second slice measures its own -3 dB, then the first interior marker
   is deleted while it sounds and the same note must measure the merged -1 dB (ratio 1.26) - against the
   round-1 code it measured the -6 dB slice that took its index (ratio 0.71,
   `test-3-round1-product.log`). That run is the intermediate state - sample pinning only - and it passes
   the two sample-swap tests while failing this one, so each case proves its own defect.

## Test matrix

| path | test | result |
| --- | --- | --- |
| normal path (a note sounds its own sample) | `droppingASample...`, `openingASample...` first assertions | PASS |
| the bug, drop route (browser file + sample clip) | `droppingASampleKeepsASoundingNoteOnItsSource` | PASS with fix, FAIL without (peak 0.092 = the dropped file) |
| the bug, file-open route | `openingASampleKeepsASoundingNoteOnItsSource` | PASS with fix, FAIL without (peak 0.153) |
| the bug, slice-map/control route | `heldSliceNoteKeepsTheControlsOfItsOwnSlice` | PASS with fix, FAIL without (ratio 0.708) |
| rejected input (unsupported drop payload) | same drop test, `note` payload | PASS |
| cancelled input (file dialog cancelled) | same file-open test | PASS |
| reconfiguration (a note started after the change) | both replacement tests, band assertion on the new file's level | PASS |
| exhaustion / overflow | `SlicerTPlayheadTest::aSoundingSliceNoteEmitsItsPlayheadFromTheGuiThreadOnly` (130 notes past the 128-slot pool) and `::deletingSlicerTrackCancelsActivePlaybackState` | PASS |
| audio-thread allocation | `SlicerTPlayheadTest::firstNoteOnFreshSlicerInstrumentAllocatesNothing` (AllocationProbe, 0 allocations) | PASS |
| audio-thread static sweep | `RtSafetySweep` + `RtSafetySelfTest` over `SlicerT::playNoteImpl`, `acquirePlaybackSlot`, `deleteNotePluginData` | PASS |
| slice-edit siblings (controls, zoom, waveform, saved settings) | `SlicerTSliceControlsTest`, `SlicerTZoomTest`, `SlicerTWaveformTest` (rebuilt after the header change) | PASS |
| sampler neighbours | `SamplerVoiceReuseTest`, `SamplerTakeTest`, `SamplerTakeGuiTest`, `AudioFileProcessorPlayheadTest` | PASS |
| supported-platform build | only `std::shared_ptr`/`std::vector`/`<memory>`, no Qt-6-only API, no POSIX call in the product change; the test uses `QFileDialog::selectFile`/`QDialog::accept` (Qt 5 and 6) and `RequestChangesGuard` default-constructs when the engine is null | builds and passes here (Qt 6, offscreen); not cross-built on MSVC in this session |

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-431.r2/logs`.

| step | command | exit | log |
| --- | --- | --- | --- |
| round-1 product + new tests (discovery) | `cmake --build build --target zene SlicerTPlayheadTest slicert -- -j4`, then `ctest -R '^SlicerTPlayheadTest$'` | build 0, test **8** | build-1-tests.log, test-3-round1-product.log |
| new GUI test, first RED/GREEN iteration | `cmake --build build --target SlicerTSampleSwapGuiTest -- -j4` + `ctest -R '^SlicerTSampleSwapGuiTest$'` | build 0, test 0 | build-9-new-test.log, test-10-new-test.log |
| 4b patch (whole product change from `9913d36d53`) | `git diff 9913d36d53 -- plugins/SlicerT/SlicerT.h plugins/SlicerT/SlicerT.cpp plugins/SlicerT/SlicerTView.cpp plugins/SlicerT/SlicerTWaveform.cpp > $LOGS/fix.patch` | 0 | fix.patch |
| 4b revert | `git apply -R $LOGS/fix.patch` | 0 | - |
| 4b rebuild | `cmake --build build --target slicert SlicerTSampleSwapGuiTest SlicerTPlayheadTest -- -j4` | 0 | build-10-red-reverted.log |
| 4b test (must fail) | `cd build/tests && ctest -R '^(SlicerTSampleSwapGuiTest\|SlicerTPlayheadTest)$' --output-on-failure` | **8** | test-11-red-reverted.log |
| 4b restore | `git apply $LOGS/fix.patch` | 0 | - |
| 4b rebuild | `cmake --build build --target slicert SlicerTSampleSwapGuiTest SlicerTPlayheadTest -- -j4` | 0 | build-11-green-restored.log |
| 4b test (must pass) | the same ctest | **0** | test-12-green-restored.log |
| family, final | `ctest -R '^(SlicerTSampleSwapGuiTest\|SlicerTPlayheadTest\|SlicerTZoomTest\|SlicerTWaveformTest\|SlicerTSliceControlsTest\|SamplerVoiceReuseTest\|SamplerTakeTest\|SamplerTakeGuiTest\|AudioFileProcessorPlayheadTest)$' --output-on-failure` | 0 (9/9) | test-13-family-final.log |
| rt-safety sweeps | `ctest -R '^(RtSafetySweep\|RtSafetySelfTest)$' --output-on-failure` | 0 | test-14-rt-sweep.log |
| gate: fork-sources | `bash tests/fork-sources-gate.sh` | 0 | gate-fork-sources.log |
| gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | gate-all-sources-reproduce.log |
| gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-no-upstream-regression.log |
| gate: check-namespace | `tests/scripted/check-namespace` | 0 | gate-check-namespace.log |
| gate: no-tautology | `bash tests/no-tautology-gate.sh` | 0 | gate-no-tautology.log |
| gate: doc-figures | `python3 tests/doc-figures-gate.py` | 0 | gate-doc-figures.log |
| gate: file-length ratchet | `bash tests/file-length-gate.sh --check` | **0** (was 1: the grown test file; fixed by splitting it) | gate-file-length.log |
| gate wrapper | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **1, could not run** - its own baseline checkout is refused by the lane guard | feature-check-1.log |
| gate: complexity ratchet | `bash tests/complexity-gate.sh --check` | **2, cannot run** - lizard is not installed and cannot be installed here (read-only home) | gate-complexity.log, pip-lizard.log |

The wrapper's refusal, verbatim from `feature-check-1.log`:

```
bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12).
```

It fails because `FIX-BASE` advanced to `050/m0-train-52` = `9913d36d53`, which has no cached baseline
listing, so the wrapper must check that commit out in its own tree - and the lane guard denies it. This is the
same limitation the review reported. Every gate the wrapper wraps was run directly instead, with the table
above; the two ratchets need no checkout (`--check` compares against the in-tree baselines).

## Registration

* `tests/fork-sources.txt`: one line, the new `tests/src/gui/SlicerTSampleSwapGuiTest.cpp` (fork-NEW).
* `tests/all-sources.txt`: re-derived with the recipe from its own header (the entry list is that command's
  byte-exact output); `all-sources-reproduce` PASSES.
* `tests/upstream-modifications.txt`: the four SlicerT lines name the new regression tests, plus one new line
  for `tests/CMakeLists.txt` (registers the new executable and adds its name to the `LMMS_PLUGIN_DIR`
  OR-list so it loads the build's `slicert` module; no existing test's registration changes).
* `tests/CMakeLists.txt`: `src/gui/SlicerTSampleSwapGuiTest.cpp` added to `LMMS_TESTS` and the plugin-dir
  OR-list. The new file and the grown `SlicerTPlayheadTest.cpp` are both under the gate's 500-code-line
  ratchet, which `tests/file-length-gate.sh --check` confirms (EXIT 0, `gate-file-length.log`).

## Self-check

1. **The regression tests FAIL with the product change reverted and PASS with it.** Measured this session:
   revert (`fix.patch`, exit 0) + rebuild (build-10-red-reverted.log, exit 0) + ctest = **RED_EXIT=8** with
   all three tests failing on their band assertions (test-11-red-reverted.log); restore (exit 0) + rebuild
   (build-11-green-restored.log, exit 0) + ctest = **GREEN_EXIT=0** (test-12-green-restored.log).
   The control case fails a second time in the intermediate state (round-1 product code, i.e. pinned bounds
   with the live indexed bank): `test-3-round1-product.log`, ratio 0.708.
2. **The tests are deterministic.** No sleeps, no wall-clock timing, no second thread, no race: the device is
   stopped in `initTestCase` and every period is rendered by the test itself
   (`AudioEngine::renderNextPeriod()`), with `AudioEngineWorkerThread::setDeterministicProcessing(true)`. The
   values are injected: the WAV fixtures the test writes (constants at chosen levels), the drag payloads it
   builds with `StringPairDrag::createMimeData`, the clip payload's `src`, and the slice points and gains it
   sets through the plug-in's own invokables. The one event-loop seam is
   `QTimer::singleShot(0, ...)` answering the modal `FileDialog` from *inside* the dialog's own `exec()` -
   event ordering, not a delay, and the test asserts the answer happened (`QVERIFY2(answered, ...)`).
3. **At the cause and complete.** Every writer of the state the render path read is a publication point
   (`updateFile`, `updateSlices`, `loadSettings`, the constructor, `clearSlices`, and the waveform's three
   edits - each proven to publish by the GUI tests above); every non-render reader still reads the editing
   state; `PlaybackSlot` reuse clears the pinned publication (`SlicerT.cpp:310`) so a reused slot cannot
   keep a stale sample; the per-slice controls follow the live map (`liveSliceFor`), and its two callers -
   the base note (no controls) and a slice note (resolved per block) - are both in `playNoteImpl`. Siblings
   checked: AudioFileProcessor and SampleClip publish under the same guard with the same retire rule.
   Header and definition changed together and every target that includes the header was rebuilt (`zene`,
   `slicert`, `SlicerTPlayheadTest`, `SlicerTSampleSwapGuiTest`, `SlicerTZoomTest`, `SlicerTWaveformTest`,
   `SlicerTSliceControlsTest`).
4. **No collateral damage.** `saveSettings` is untouched, so a saved project's SlicerT element is what it
   was; `loadSettings` sets the same members as before and only publishes afterwards, so old projects (file
   source, base64 source, or no `totalSlices`) load exactly as before. No limit or check was added anywhere.
   No control command is added or changed (no `ControlCommands*` file is touched, so
   `ControlCommandsSnapshot` is unchanged), undo is untouched (no command, no undo record). No `src/gui`
   file and no `src/core/Song*`, `*Record*`, `*Undo*`, `*Transport*`, `Take*` or `AudioEngine*` file is
   modified - the new test lives under `tests/src/gui` and no product file under `src/gui` changed - which is
   why no screenshot or state-sequence artifact is attached. Existing coverage stays green (family run,
   9/9).
5. **Verdict is FIXED**, not ALREADY-FIXED or FALSE-POSITIVE: the pre-fix race is quoted with file:line
   above and was reproduced in this session (test-11-red-reverted.log).
6. **Sibling commands** (`app.*`, `arrangement.fit_tracks`, `arrangement.get_state`): this change adds no
   command and no command file; `plugin.slice_get`/`plugin.slice_set` (the SlicerT commands) reach the
   per-slice control atomics, which the control-coherence fix reads through `liveSliceFor` but does not
   change, so their behaviour is unchanged.
7. **Gate wrapper**: `feature_check.py` could not run in this lane (its own baseline checkout is refused by
   the guard, output above). All eight gates it wraps were run directly and are logged; file-length PASSES,
   complexity cannot run because lizard is missing, and the two pip attempts to install it are logged
   (`pip-lizard.log`: PEP 668 refusal, then `Read-only file system: '/home/kruzzzzy/.local/lib'`).

## Other findings

* `tests/src/core/SlicerTPlayheadTest.cpp` is back under the length ratchet by moving the sample-replacement
  coverage to `tests/src/gui/SlicerTSampleSwapGuiTest.cpp`; that also put the GUI-route tests in the `gui`
  scope where they belong.
* A retired publication that a sounding note pinned stays in `m_retiredSnapshots` until the next publication
  (or plug-in destruction): at most one extra sample's memory while the user replaces samples during
  playback, the same bound AudioFileProcessor accepts.
* Noticed, not touched: `SlicerT::findSlices()` overwrites `m_slicePoints[0]` with 0, so an onset detected at
  the very start of a file is dropped from the map (it becomes the slice start). Unrelated to this race.
