FIXED — cluster new-p10-342-port (port of the reverted `bugs/qwen-new-p10-342`).

**Verdict: FIXED.** The finding is real on this line, but the fix that was reverted
(`8b19c771a`, "Fix embedded sample rate validation") is NOT the behaviour this line
should have: this line already loads an embedded sample with an unusable rate at the
engine's output rate (`src/core/SampleClip.cpp:851-856`, cluster new-p12-204), and the
reverted fix's own test could not pass beside it. This port decides the **substitute**
behaviour - keep the user's frames, load them at the engine's rate - and completes it
at the factory the finding names, `SampleBuffer::fromBase64()`
(`src/core/SampleBuffer.cpp:108-124`), which still built a buffer with whatever rate it
was handed.

## The decision: substitute, not reject

The finding itself allows either ("reject it or substitute a valid rate before
constructing the buffer"), so the port had to pick one and keep the test honest. The
line's own, accepted behaviour wins:

- **new-p12-204 (kept on this line)** - `src/core/SampleClip.cpp:851-856`: a stated
  `sample_rate` that is not positive loads the way an absent one loads, at
  `Engine::audioEngine()->outputSampleRate()`. Its rule is stated in the code:
  "An invalid rate loads the way an absent one already does: the engine's own output
  rate."
- **The rejected alternative loses the user's only copy of the audio.** The reverted
  fix returned the EMPTY buffer for an unusable rate, so a project with embedded audio
  and `sample_rate="0"` loads silent. A pitch mismatch (the frames played at the
  engine's rate) is a smaller loss than a silently emptied clip, and the substitute
  rule is the one this line already applies at the parse site.
- **The rejected alternative could not pass its own test here anyway.** The reverted
  test asserted the loaded clip's buffer was EMPTY. With new-p12-204's substitution in
  `loadSettings()`, the factory never sees the unusable rate on the project path, so
  that assertion contradicts the line's accepted behaviour - and adopting it would
  have required weakening new-p12-204's own accepted regression
  (`SampleClipWarpTest::zeroSampleRateIsRejectedOnProjectLoad`, which pins that the
  loaded clip keeps the engine's rate AND a real tick length). No accepted test was
  touched.
- **The factory was the gap that was left.** The finding's own wording - "`fromBase64`
  ... constructs a buffer without checking it" - is still literally true of
  `src/core/SampleBuffer.cpp` on this line: the factory built a buffer whose rate was
  0, and the clip mapping divides by that rate
  (`Engine::framesPerTick(sample_rate_t)`, `src/core/Engine.cpp:135-139`;
  `SampleClip::clipFramesPerTick()`, `src/core/SampleClip.cpp:548-561`).

So the fix is the factory-level half of the same rule, at the cause, with the same
semantics as the parse site: **an unusable rate is the engine's output rate.**

## The fix

`src/core/SampleBuffer.cpp:108-124` - `SampleBuffer::fromBase64()` no longer builds a
buffer with a rate that is not positive. A rate of 0, a negative one, or text a caller
read back as 0 is replaced by `Engine::audioEngine()->outputSampleRate()` (with a
`qWarning`, on the control thread - this factory is never called on the audio thread).
The frames are kept; only the unusable rate is replaced.

Nothing else changed: no header, no signature, no new control command, no undo
change, and `SampleClip::loadSettings()` is untouched.

## Why the factory and not the constructor

`SampleBuffer`'s constructors still accept a rate of 0, on purpose: the accepted
regression `SampleClipWarpTest::nonpositiveRateMapsToNoLength`
(`tests/src/tracks/SampleClipWarpTest.cpp:637-646`) pins that a directly constructed
zero-rate buffer KEEPS its rate of 0 and maps to no length, and
`SampleClip::windowTicksFor()` (new-p12-204) is the guard for that case. Sanitising the
constructor would have broken that accepted test. The factory - the one the finding
names - is where the value arrives from a file, and it is the boundary where the
"unusable rate" rule belongs.

## The regression (RED before the fix, GREEN after)

`tests/src/core/ClipSerialisationTest.cpp` (already registered in
`tests/fork-sources.txt`; no registry change was needed):

- `embeddedSampleWithAnUnusableRateLoadsAtTheOutputRate()` (line 486) - the project
  path: a `<sampleclip>` with embedded `data` and a stated rate of `"0"`, `"-44100"`
  or `"not a rate"` is saved, its `sample_rate` is rewritten, and it is restored into
  a fresh clip (the real project-load path). It pins the DECIDED behaviour: the
  buffer's rate is the engine's output rate, the frames are still there
  (`buffer()->size() == 4`), and `clipFramesPerTick()` is finite and positive - no
  invalid frame count, no division by zero.
- `base64FactoryNeverBuildsANonPositiveRate()` (line 528) - the factory: for rates 0,
  -1 and -44100, `SampleBuffer::fromBase64()` must not hand back a buffer whose rate is
  not positive; the rate is the engine's output rate and the frames are kept. **This
  is the case that fails without the fix.**

Determinism: no wall-clock, no sleep, no race. The rates and the frame count are
injected values, the engine's rate is read from `Engine::audioEngine()` (the test
engine's own configured rate, 44100), and the project path is driven through
`saveState()`/`restoreState()` - no timing anywhere.

## Evidence table

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342-port.r1/logs`.
Commands were run from the worktree root unless the row says otherwise.

| Stage | Exact command | Exit |
|---|---|---:|
| First test build (my own test bug: `const QDomElement` cannot take `setAttribute`) | `cmake --build build --target ClipSerialisationTest > LOGS/build-1.log 2>&1` | 2 |
| Test build, corrected | `cmake --build build --target ClipSerialisationTest > LOGS/build-2.log 2>&1` | 0 |
| **RED** (product code unchanged) | `cd build/tests && ctest -R '^ClipSerialisationTest$' --output-on-failure > LOGS/test-1-red.log 2>&1` | **8** |
| Build product + test with the fix | `cmake --build build --target zene ClipSerialisationTest > LOGS/build-3.log 2>&1` | 0 |
| **GREEN** (fix applied) | `cd build/tests && ctest -R '^ClipSerialisationTest$' --output-on-failure > LOGS/test-2-green.log 2>&1` | **0** |
| Build the family binaries | `cmake --build build --target ClipSerialisationTest SampleClipWarpTest SampleClipWindowTest SampleClipStretchTest SampleClipRubberBandTest ClipWarpPersistenceTest AudioFileProcessorWaveViewTest AudioFileProcessorPlayheadTest SlicerTPlayheadTest SlicerTWaveformTest SlicerTZoomTest > LOGS/build-4-family.log 2>&1` | 0 |
| Family, first attempt (2 environmental failures, see below) | `cd build/tests && ctest -R 'ClipSerialisation\|SampleClip\|ClipWarpPersistence\|AudioFileProcessor\|SlicerT' --output-on-failure > LOGS/test-3-family.log 2>&1` | 8 |
| Save and reverse the product patch | `git diff -- src/core/SampleBuffer.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| Build on the unfixed product code | `cmake --build build --target zene ClipSerialisationTest > LOGS/build-5-reversed.log 2>&1` | 0 |
| **RED proof** (test only, product code original) | `cd build/tests && ctest -R '^ClipSerialisationTest$' --output-on-failure > LOGS/test-4-reversed-red.log 2>&1` | **8** |
| Re-apply the product patch | `git apply LOGS/fix.patch` | 0 |
| Build with the fix restored | `cmake --build build --target zene ClipSerialisationTest > LOGS/build-6-restored.log 2>&1` | 0 |
| **GREEN proof** | `cd build/tests && ctest -R '^ClipSerialisationTest$' --output-on-failure > LOGS/test-5-restored-green.log 2>&1` | **0** |
| Wider sweep (68 selected tests) | `cd build/tests && ctest -R 'Sample\|Clip\|Audio\|Wave\|Buffer' --output-on-failure > LOGS/test-6-wider-family.log 2>&1` | 8 |
| Baseline of the two sweep failures WITHOUT the fix | `git apply -R LOGS/fix.patch && cmake --build build --target zene > LOGS/build-7-pristine-zene.log 2>&1` then `cd build/tests && ctest -R '^(ControlGoldenAudio\|NeuralAmpWavRunSampleRateWarning)$' --output-on-failure > LOGS/test-7-pristine-baseline.log 2>&1` | 0 / **8** |
| Restore the fix and rebuild | `git apply LOGS/fix.patch && cmake --build build --target zene ClipSerialisationTest > LOGS/build-8-final-zene.log 2>&1` | 0 |
| **Family GREEN** - 6 tests executed, 6 passed | `cd build/tests && ctest -R 'ClipSerialisation\|SampleClip\|ClipWarpPersistence' --output-on-failure > LOGS/test-8-family-final.log 2>&1` | **0** |
| Gate suite | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-1-feature-check.log 2>&1` | **0** |
| Complexity ratchet (raw) | `bash tests/complexity-gate.sh --check > LOGS/gate-2-complexity.log 2>&1` | 1 - PRE-EXISTING, none in my files |
| File-length ratchet (raw) | `bash tests/file-length-gate.sh --check > LOGS/gate-3-file-length.log 2>&1` | 1 - PRE-EXISTING, none in my files |

RED and reversal-RED are the same failure (LOGS/test-1-red.log and
LOGS/test-4-reversed-red.log):

```
FAIL!  : ClipSerialisationTest::base64FactoryNeverBuildsANonPositiveRate() 'buffer->sampleRate() > 0' returned FALSE. (rate 0 reached the buffer)
   Loc: [../../tests/src/core/ClipSerialisationTest.cpp(542)]
Totals: 9 passed, 1 failed, 0 skipped, 0 blacklisted
```

Both GREEN runs pass (`100% tests passed out of 1`).

The two failures in the family sweep and in the wider sweep are environmental and
pre-existing, NOT caused by this change:

- `SlicerTPlayheadTest` and `AudioFileProcessorPlayheadTest` fail with
  `The plugin "slicert" wasn't found or could not be loaded! Reason: "Plugin not
  found."` - the plugin shared objects are not built in this tree
  (`build/plugins/SlicerT/` and `build/plugins/AudioFileProcessor/` contain only
  CMake files, no `.so`), and `LMMS_PLUGIN_DIR` must not be set.
- `ControlGoldenAudio` fails in the render subprocess with
  `terminate called after throwing an instance of 'std::bad_alloc'`, and
  `NeuralAmpWavRunSampleRateWarning` fails with
  `FileNotFoundError: .../build/plugins/nam_harness` (the harness binary is not built).
  LOGS/test-7-pristine-baseline.log shows **both failing identically on the tree with
  my change reverted**, so neither is collateral damage.

The remaining "Not Run" entries in the wider sweep are test binaries that are not
built in this tree (the same shape the new-p12-204 report recorded).

Both ratchet gates are red on the train on purpose: the complexity gate lists 6
functions and the file-length gate 14 files, and **none of them is in a file this
change touches** (`src/core/SampleBuffer.cpp`,
`tests/src/core/ClipSerialisationTest.cpp`); `feature_check.py` compares them with the
base commit and reports "no new item in your files" for both. No gate limit or
baseline was changed.

## Files committed

- `src/core/SampleBuffer.cpp` (the fix; inherited file, registered in
  `tests/upstream-modifications.txt`)
- `tests/src/core/ClipSerialisationTest.cpp` (the regressions; already registered in
  `tests/fork-sources.txt`)
- `tests/upstream-modifications.txt` (the divergence entry, with the reason)
- `docs/qwen-fixes-2026-10/cluster-new-p10-342-port.md` (this report)

No new file was added, so no `tests/fork-sources.txt` entry was needed; no header
changed, so no dependent target had to be rebuilt beyond `zene` and the tests.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  RED: `ctest -R '^ClipSerialisationTest$'` -> exit **8**
  (`base64FactoryNeverBuildsANonPositiveRate`: "rate 0 reached the buffer"). Reversal
  proof: `git apply -R LOGS/fix.patch` -> rebuild (exit 0) -> ctest -> exit **8**.
  Restored: `git apply LOGS/fix.patch` -> rebuild (exit 0) -> ctest -> exit **0**.
  (LOGS/test-1-red.log, LOGS/test-4-reversed-red.log, LOGS/test-5-restored-green.log.)
- **The test is deterministic.** No wall-clock, no sleep, no race: the unusable rates
  (0, -1, -44100) and the stated rates ("0", "-44100", "not a rate") are injected
  values, the frame count is a constant (4), the engine's rate is read from the test
  engine, and the project path is driven through `saveState()`/`restoreState()`.
- **The fix is at the cause and complete.** Every caller of
  `SampleBuffer::fromBase64()` was checked: `SampleClip::loadSettings()`
  (`src/core/SampleClip.cpp:856` - passes the engine's rate, so the guard does not
  fire), `AudioFileProcessor::loadSettings()`
  (`plugins/AudioFileProcessor/AudioFileProcessor.cpp:240` - default argument = the
  engine's rate), `SlicerT::loadSettings()`
  (`plugins/SlicerT/SlicerT.cpp:429` - default argument) and
  `SampleClipView::dropEvent()` (`src/gui/clips/SampleClipView.cpp:175` - default
  argument). The default argument itself is
  `Engine::audioEngine()->outputSampleRate()` (`include/SampleBuffer.h:87-88`), i.e.
  the same rule, so the factory and its callers agree. The sibling factory
  `SampleBuffer::fromFile()` (`src/core/SampleBuffer.cpp:74-106`) is not affected: its
  rates come from `SampleDecoder::decode()` (libsndfile's `sfInfo.samplerate`,
  vorbis' rate, or the engine's rate for a DS file), all positive. The wider type the
  value flows into is `sample_rate_t m_sampleRate` (`include/SampleBuffer.h:93`), whose
  only consumer that divides is `Engine::framesPerTick()`; a buffer built by this
  factory now always carries a positive rate.
- **No collateral damage.** The guard only fires for a rate that is not positive, and
  no caller on this line passes one - the project loader already substituted the
  engine's rate (new-p12-204), so a saved project loads byte for byte as before, with
  its embedded frames and its tick lengths unchanged. The check cannot hit a
  legitimate caller that exceeds it: the only limit is "positive", and the engine's
  own rate is always positive (`m_baseSampleRate` is at least
  `SUPPORTED_SAMPLERATES.front()` = 44100, `include/AudioEngine.h:81`,
  `src/core/AudioEngine.cpp:300-302`). The wider sweep and the family sweep show no
  new failure, and the two failures they do show are identical on the tree with the
  change reverted.
- **The intended contract, for the record** (the fifth ask is conditional on an
  ALREADY-FIXED / FALSE-POSITIVE verdict, which this cluster's is not - it is FIXED).
  The contract is the line's own: a sample rate that is not positive is the engine's
  output rate. It is written down in the code at `src/core/SampleClip.cpp:851-856`
  (the project loader) and, with this change, at `src/core/SampleBuffer.cpp:108-124`
  (the factory), and it is pinned by the two regressions named above. The
  zero-rate-buffer case that is not from a project load is covered by the accepted
  `SampleClip::windowTicksFor()` guard (new-p12-204) and by
  `SampleClipWarpTest::nonpositiveRateMapsToNoLength`.
- **Realtime, undo and commands.** `SampleBuffer::fromBase64()` is called from the
  control thread only (project load, drag and drop); it is not on any audio-thread
  path, and the added `qWarning()` is in the same place as the function's existing
  warning. Nothing new allocates, frees, locks or waits on the audio thread. No
  control command was added, so the ControlCommandsSnapshot canary is unmoved, and no
  undo step was introduced.
