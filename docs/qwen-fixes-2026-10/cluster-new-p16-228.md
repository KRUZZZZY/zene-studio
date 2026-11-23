FIXED

# Cluster new-p16-228 - Extreme Sfxr pitches can monopolize an audio period

new-p16-228: FIXED

## Verdict: REAL

The finding describes `plugins/Sfxr/Sfxr.cpp` on this branch. Before the fix the render path took the note's
pitch ratio and rendered that many 44100 Hz synth frames for every frame of the period, with no ceiling:

- `plugins/Sfxr/Sfxr.cpp:490` (before) - `int32_t pitchedFrameNum = (_n->frequency() / baseFreq) * frameNum;`
- `plugins/Sfxr/Sfxr.cpp:492` (before) - `pitchedFrameNum /= ( currentSampleRate / 44100 );`
- `plugins/Sfxr/Sfxr.cpp:456` (before) - `renderPitched(...)` then ran the synth through **every** one of those
  frames: `while (source >= blockEnd)` in 256-frame blocks plus a discard call for the tail
  (`plugins/Sfxr/Sfxr.cpp:465` before: `synth.update(nullptr, ...)`).

`baseFreq` is the tuning's base frequency (`InstrumentTrack::baseFreq()`, `src/tracks/InstrumentTrack.cpp:202`,
440 Hz in the default tuning and the keymap's own base frequency when the microtuner is active), and the note's
frequency is `DefaultBaseFreq * exp2((key - baseNote)/12 ...)` (`src/core/NotePlayHandle.cpp:716-719`). So the
ratio is unbounded input: 1530.6 on the plain default path for key 127 against base note 0, and arbitrarily
large for a tuning whose base frequency sits far below the note. One period then renders
`ratio * framesPerPeriod` synth frames; at the 256-frame period of this build that is 391,838 synth frames for
one note, all inside a single audio callback.

Two observed consequences on this branch, both reproduced in this session (see the RED run below):

1. the period's work grows with the ratio and nothing caps it (the finding);
2. when the ratio is above 256 the resampler needs a discard call for the frames past the last one it reads
   (`plugins/Sfxr/Sfxr.cpp:465` before), and `SfxrSynth::update(nullptr, n)` writes `buffer[i][j] = 0.0f`
   without checking the buffer once the sound has ended (`plugins/Sfxr/Sfxr.cpp:157-163`) - the RED run
   segfaults there. Both come from the same unbounded frame count, so the bound removes both.

## Fix (smallest change at the cause)

`plugins/Sfxr/Sfxr.cpp`:

- `plugins/Sfxr/Sfxr.cpp:451` - `constexpr f_cnt_t SfxrMaxSynthFramesPerOutputFrame = 64;` documents the
  ceiling: the most synth frames one output frame may take. 64 frames per output frame is a pitch of 28 kHz
  against the 440 Hz base the default tuning reports, two octaves above the highest note that tuning reaches
  (`2^(127-57)/12 = 28.5`), so only notes that are already ultrasonic are held to it.
- `plugins/Sfxr/Sfxr.cpp:503-513` - the period's synth-frame count keeps the arithmetic it always had (the
  float product truncated, then divided by the engine's rate ratio and truncated again) but is computed in
  `double`, where an extreme ratio cannot overflow the `int32` it used to be squeezed into, and it is clamped
  **in `double`, before the conversion to `f_cnt_t`** (`pitchedCeiling` at `:510`, the clamp at `:511-513`):
  `!(pitchedFrames < ceiling)` takes the ceiling, which also covers `+inf` and `NaN`; a count at or below zero
  (`-inf` included) is zero.
- `plugins/Sfxr/Sfxr.cpp:456-458` - `renderPitched()` takes the count as `f_cnt_t` (the frame type) instead of
  `int32_t`, so the clamp is the only bound and the count cannot be truncated on the way in.

Output is unchanged for every note inside the bound: no operation moved, the two truncations happen exactly
where they happened before, and the resampler, the synth and the per-period stack block are untouched. One
period's synthesis is now at most 64x the period's own frame count (about a tenth of the period's running
time at the ~37 ns per synth frame this machine showed in the RED/GREEN runs) instead of an unbounded
multiple of it.

Callers and siblings checked: `renderPitched()` has exactly one caller, `SfxrInstrument::playNoteImpl()`
(`plugins/Sfxr/Sfxr.cpp:482`), which is the only note entry point the class overrides
(`plugins/Sfxr/Sfxr.h:195` - `playNoteImpl` only; the inherited `playNote(NotePlayHandle*, SampleFrame*)`
forwards to it through `Instrument.h:114-118` and `Instrument.h:302-304`). `SfxrSynth::update()` has no other
caller in the plugin (`plugins/Sfxr/Sfxr.cpp:82-310`). Nothing was added on the audio thread: the fix removes
work, allocates nothing, locks nothing, logs nothing (the `// debug code` block stayed as it was).

### Round 2 (this run): the ceiling is applied before the conversion

Round 1 clamped the count *after* converting it to `f_cnt_t`
(`std::min<f_cnt_t>(static_cast<f_cnt_t>(std::max(pitchedFrames, 0.0)), frameNum * 64)`), which is undefined
behaviour whenever the count does not fit: a converter of an out-of-range `double` to an unsigned 64-bit
integer is not representable, and the standard leaves the result open. That input is reachable - MTS-ESP
accepts any finite positive frequency a float holds (`src/core/SessionTuning.cpp:373-381`,
`frequency > MaxTableFrequency` is the only bound), so a note tuned to `1e30` Hz asks for about `5.8e29` synth
frames in a 256-frame period.

What the round-1 order actually produced here, measured with this build's own flags (`-O2 -g -DNDEBUG
-std=gnu++20`) by `logs/probe-cast.cpp`:

```
in=1e+30          round1=0                      fixed=16384
in=1.98e+38       round1=0                      fixed=16384
in=inf            round1=0                      fixed=16384
in=nan            round1=16384                  fixed=16384
in=391836         round1=16384                  fixed=16384
in=1024           round1=1024                   fixed=1024
in=-5             round1=0                      fixed=0
```

So the undefined conversion did not merely corrupt a number: it turned an accepted, tuned note into a period
of **silence** (`0` synth frames), and the value it yields is the compiler's choice, not ours. The fix clamps
in `double` first, so the conversion only ever sees a value in `[0, frameNum * 64]`.

Size: the whole product change against the branch base is **26 insertions + 8 deletions = 34 changed lines**
(`git diff --numstat fa4a5f2e7 -- plugins/Sfxr/Sfxr.cpp`), inside the 40-line limit; the round-2 part is 9
insertions + 11 deletions.

## Regression test

`tests/src/plugins/SfxrPitchBoundTest.cpp` (new, fork-NEW). It compiles the plugin's own sources into the test
(`tests/CMakeLists.txt`, the Watsyn/Vestige shape) and drives the shipping note path
(`SfxrInstrument::playNote`) with a real `NotePlayHandle`, then compares the period against a reference
`SfxrSynth` the test drives itself. Deterministic: same model values, the default Square wave (the only
waveform with no PRNG in it), no sleeps, no wall-clock timing, no threads.

- `pitchedNoteAboveTheBoundIsClampedToTheBoundedPitch` (line 194) - key 84 against base note 0, ratio 128,
  twice the bound; the period must contain exactly the bounded pitch. **Fails without the fix** (assertion).
- `extremePitchedNoteRendersABoundedPeriod` (line 216) - the finding's case: key 127 against base note 0,
  ratio 1530.6, 391,838 synth frames for one 256-frame period unbounded; the period must contain exactly the
  bounded pitch. **Crashes without the fix** (the null discard write above).
- `normalPitchedNoteIsRenderedUnchanged` (line 241) - ratio 4, inside the bound: the period must still be
  every synth frame it always was (collateral guard, passes both ways).
- `periodsPastTheEndOfTheSoundStaySilent` (line 267) - 40 periods of a 4x note: each period lines up with the
  synth's own frames, and the periods after the sound has run out are silent (exhaustion path, passes both
  ways).
- `sessionPitchBeyondFrameCountRangeIsClampedBeforeConversion` (line 279) - the round-2 case: the session
  table is set to `1e30` Hz for the note's key through `SessionTuning::setNoteFrequency` (an ACCEPTED value,
  the same call the MTS-ESP path uses), the note carries it, and the period must be the bounded pitch - not the
  undefined conversion. **Fails without the round-2 change** (that state renders silence, `0` frames, so the
  buffer is all zeros against the reference). It is the last slot and it resets the session table
  (`cleaningTestCase()` resets it too), so the other slots keep the 12-TET tuning they assume.

Test matrix the change was checked against:

| path | covered by | result |
|---|---|---|
| normal pitch (ratio <= 64) | `normalPitchedNoteIsRenderedUnchanged` | unchanged, passes |
| above the bound (ratio 128) | `pitchedNoteAboveTheBoundIsClampedToTheBoundedPitch` | clamped, fails before the fix |
| extreme (ratio 1530.6) | `extremePitchedNoteRendersABoundedPeriod` | clamped, crashes before the fix |
| exhaustion (sound ends inside a note) | `periodsPastTheEndOfTheSoundStaySilent` | silent tail, passes |
| accepted extreme MTS frequency (`1e30` Hz, past `f_cnt_t`) | `sessionPitchBeyondFrameCountRangeIsClampedBeforeConversion` | clamped before the conversion, fails (silence) without the round-2 change |
| custom tuning above 64:1 | the bound is a ratio, so it applies at any absolute frequency: see the collateral note under Self-check | clamped, documented |
| reconfiguration (engine rate) | not exercised: `AudioEngine` has no public sample-rate setter (`include/AudioEngine.h:346`), so the harness runs at the engine's own rate; `HeldNote::synthFramesForPeriod()` re-derives the rate-scaled count, so the same test holds at another rate | unverified here |
| audio thread | the fix removes synth frames; no allocation, lock, wait or log is added (the test's reference synth is test-side only) | by inspection |
| supported-platform build | the test target builds with the plugin's own source and its generated artwork, the same shape other plugin tests use (`tests/CMakeLists.txt`, Vestige/VstEffect pattern); built on this Linux tree only | Linux verified, other platforms not run |

## Commands and EXIT codes (copied from the logs in the run folder)

Round 2 (this run) logs live in `.../runs/new-p16-228.r2/logs`, round 1's in `.../runs/new-p16-228.r1/logs`.

Round 2, RED - the new MTS slot against the round-1 product code:
```
cmake --build build --target SfxrPitchBoundTest -j4                        # build-1.log EXIT=0
cd build/tests && ./SfxrPitchBoundTest                                      # test-1-red-mts.log EXIT=1
```
`test-1-red-mts.log`: `FAIL!  : lmms::SfxrPitchBoundTest::sessionPitchBeyondFrameCountRangeIsClampedBeforeConversion()
'sameFrames(note.buffer(), resampled(raw, frames))' returned FALSE. (an accepted 1e30 Hz tuning must render the
bounded pitch, not an undefined conversion)`, `Totals: 6 passed, 1 failed, 0 skipped, 0 blacklisted, 521ms`.
The slot's own guards (`frequency > 1e29`, `count == ceiling`) pass, so the test computed the ceiling while the
render path produced `0` frames - the period was silent.

Round 2, GREEN - the clamp moved in front of the conversion:
```
cmake --build build --target SfxrPitchBoundTest zene -j4                    # build-2-green.log EXIT=0
cd build/tests && ./SfxrPitchBoundTest                                      # test-2-green-mts.log EXIT=0
cd build/tests && ctest -R SfxrPitchBoundTest --output-on-failure           # test-3-green-ctest.log EXIT=0
```
`test-2-green-mts.log`: `Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 508ms` (5 slots + init/cleanup);
`test-3-green-ctest.log`: `1/1 Test #643 ... Passed 0.59 sec`, `100% tests passed out of 1`.

Round 2, step 4b - this round's product diff reversed, then restored:
```
git diff -- plugins/Sfxr/Sfxr.cpp > .../r2/logs/fix.patch     # DIFF_EXIT=0, 32 lines
git apply -R .../r2/logs/fix.patch                            # REVERSE_APPLY_EXIT=0
cmake --build build --target SfxrPitchBoundTest -j4           # build-3-reverted.log BUILD_EXIT=0
cd build/tests && ctest -R SfxrPitchBoundTest --output-on-failure  # test-4-red-4b.log CTEST_EXIT=8
git apply .../r2/logs/fix.patch                               # APPLY_EXIT=0
cmake --build build --target SfxrPitchBoundTest zene -j4      # build-4-reapplied.log BUILD_EXIT=0
cd build/tests && ctest -R SfxrPitchBoundTest --output-on-failure  # test-5-green-4b.log CTEST_EXIT=0
```
`test-4-red-4b.log`: `FAIL!  : ....sessionPitchBeyondFrameCountRangeIsClampedBeforeConversion() ... returned
FALSE`, `Totals: 6 passed, 1 failed`, `0% tests passed, 1 tests failed out of 1`.
`test-5-green-4b.log`: `1/1 Test #643: SfxrPitchBoundTest ....... Passed 0.53 sec`, `100% tests passed out of 1`.

Round 2, the measurement behind the diagnosis (this build's flags, `-O2 -g -DNDEBUG -std=gnu++20`):
```
g++ -O2 -g -DNDEBUG -std=gnu++20 -o probe-cast probe-cast.cpp   # probe-cast.cpp in the r2 log folder
./probe-cast                                                     # EXIT=0, output quoted above
```

Round 2, family:
```
ctest -R "SfxrPitchBoundTest|PluginPortsMigrationTest|InstrumentTrackRenderPathTest|BitInvaderAllocationTest|WatsynWaveSnapshotTest" \
    --output-on-failure                                       # test-6-family.log CTEST_EXIT=8
```
`1/5 WatsynWaveSnapshotTest Passed`, `2/5 BitInvaderAllocationTest Passed`, `3/5 SfxrPitchBoundTest Passed`,
`4/5 InstrumentTrackRenderPathTest Passed`, `5/5 PluginPortsMigrationTest ***Failed` - the same four fixture
failures round 1 documented below (Mallets' `thirdparty/stk-rawwaves` is absent, the SWH Decimator LADSPA
fixture is not installed, and `Sfxr` is in none of that target's sources). Gates on the round-2 tree:
`gates-1.log` **EXIT=0**, the table below.

Round 1, RED, product unfixed:
```
cmake --build build --target SfxrPitchBoundTest zene -j4            # build-6.log   EXIT=0
cd build/tests && ctest -R SfxrPitchBoundTest --output-on-failure   # test-2-red.log EXIT=8
```
`test-2-red.log`: `FAIL!  : lmms::SfxrPitchBoundTest::pitchedNoteAboveTheBoundIsClampedToTheBoundedPitch()
'sameFrames(note.buffer(), resampled(raw, frames))' returned FALSE.` (line 211) and then
`Received signal 11 (SIGSEGV) ... at instruction address ... <lmms::SfxrSynth::update(SampleFrame*, int)+2104>`
in the extreme test.

GREEN, fix applied:
```
cmake --build build --target SfxrPitchBoundTest zene -j4            # build-7-green.log EXIT=0
cd build/tests && ctest -R SfxrPitchBoundTest --output-on-failure   # test-4-green.log EXIT=0
cd build/tests && ./SfxrPitchBoundTest                               # test-5-gunit.log EXIT=0
```
`test-5-gunit.log`: `Totals: 6 passed, 0 failed, 0 skipped, 0 blacklisted, 481ms` (4 test slots + init/cleanup).
Note: the first green attempt (`test-3-green.log`, EXIT=8) failed on an over-strict guard of mine
(`!note.handle().isFinished()`), which cannot hold in a harness that renders a note directly instead of through
`NotePlayHandle::play()`; I removed that one assertion and kept the observable claim (audible, then silent).

Step 4b, proof the test catches the bug:
```
git diff -- plugins/Sfxr/Sfxr.cpp > .../logs/fix.patch     # DIFF_EXIT=0, 58 lines
git apply -R .../logs/fix.patch                            # REVERSE_APPLY_EXIT=0
cmake --build build --target SfxrPitchBoundTest -j4        # build-10-reverted.log EXIT=0
cd build/tests && ctest -R SfxrPitchBoundTest --output-on-failure  # test-7-red-after-revert.log EXIT=8
git apply .../logs/fix.patch                               # APPLY_EXIT=0
cmake --build build --target SfxrPitchBoundTest zene -j4    # build-11-reapplied.log EXIT=0
cd build/tests && ctest -R SfxrPitchBoundTest --output-on-failure  # test-8-green-after-reapply.log EXIT=0
```
`test-7-red-after-revert.log` repeats the `FAIL!` line and the SIGSEGV; `test-8-green-after-reapply.log`:
`1/1 Test #643: SfxrPitchBoundTest .......   Passed    0.52 sec`, `100% tests passed out of 1`.

Related targets (built, then run from build/tests):
```
cmake --build build --target PluginPortsMigrationTest InstrumentTrackRenderPathTest \
    BitInvaderAllocationTest WatsynWaveSnapshotTest -j4      # build-12-family.log BUILD_EXIT=0
ctest -R "SfxrPitchBoundTest|PluginPortsMigrationTest|InstrumentTrackRenderPathTest|BitInvaderAllocationTest|WatsynWaveSnapshotTest" \
    --output-on-failure                                      # test-9-family-final.log CTEST_EXIT=8
```
`test-9-family-final.log`: `1/5 WatsynWaveSnapshotTest Passed`, `2/5 BitInvaderAllocationTest Passed`,
`3/5 SfxrPitchBoundTest Passed`, `4/5 InstrumentTrackRenderPathTest Passed`,
`5/5 PluginPortsMigrationTest ***Failed` (93 passed, 4 failed).

The four `PluginPortsMigrationTest` failures are fixtures this checkout does not have, not this change:
`malletsVoiceLimitIsEnforcedPerInstrument`, `malletsVoiceLimitSurvivesDeviceChange` and
`slice8PluginsAreLiveAndExact` all render silence because Mallets' raw waves are missing -
`FileRead::open: could not open or find file
(/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/thirdparty/stk-rawwaves/marmstk1.raw)`
and that directory does not exist (`ls .../thirdparty/stk-rawwaves` -> `No such file or directory`);
`nonRealtimeLadspaEffectsAreInvalid` needs the SWH Decimator LADSPA fixture (`SWH Decimator test fixture was
not loaded`). That test binary does not contain this change at all: `Sfxr` appears nowhere in
`build/tests/CMakeFiles/PluginPortsMigrationTest.dir/build.make` nor in its two sources (rg found no match,
exit 1), so the fix cannot move it.

Gates:
```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # gates-1.log EXIT=0
```
```
feature-check kads-new-p16-228 (base fa4a5f2e7, 433 changed files; ...)
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

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Round 2 (this run, `.../r2/logs`): reverted `git apply -R fix.patch` (`REVERSE_APPLY_EXIT=0`) and rebuilt
  (`build-3-reverted.log` `BUILD_EXIT=0`), `ctest -R SfxrPitchBoundTest --output-on-failure` =
  `test-4-red-4b.log` **CTEST_EXIT=8** with `FAIL! :
  lmms::SfxrPitchBoundTest::sessionPitchBeyondFrameCountRangeIsClampedBeforeConversion()` and `Totals: 6
  passed, 1 failed`; restored `git apply fix.patch` (`APPLY_EXIT=0`) and rebuilt (`build-4-reapplied.log`
  `BUILD_EXIT=0`), `test-5-green-4b.log` **CTEST_EXIT=0**, `1/1 ... Passed 0.53 sec`. Before the product
  change was written, the same slot on the round-1 code was `test-1-red-mts.log` **EXIT=1**
  (`./SfxrPitchBoundTest`, `Totals: 6 passed, 1 failed`); after it, `test-2-green-mts.log` **EXIT=0**
  (`Totals: 7 passed, 0 failed`).
  Round 1 (`.../r1/logs`), unchanged and still on the branch: reverted (`build-10-reverted.log` EXIT=0),
  `test-7-red-after-revert.log` **EXIT=8** - `FAIL! : ...pitchedNoteAboveTheBoundIsClampedToTheBoundedPitch()
  'sameFrames(note.buffer(), resampled(raw, frames))' returned FALSE.` and the SIGSEGV in
  `SfxrSynth::update`; restored (`build-11-reapplied.log` EXIT=0), `test-8-green-after-reapply.log`
  **EXIT=0**.
  The exact command pair the round-2 proof manifest carries was run end to end, one line each, from the clean
  tree: `git apply -R .../r2/logs/fix.patch && cmake --build build --target SfxrPitchBoundTest -j4 && cd
  build/tests && ctest -R SfxrPitchBoundTest --output-on-failure` = `proof-red-1.log` **RED_EXIT=8**
  (`FAIL! : ...sessionPitchBeyondFrameCountRangeIsClampedBeforeConversion()`, `0% tests passed, 1 tests failed
  out of 1`), then `git apply .../r2/logs/fix.patch && ... ctest ...` = `proof-green-1.log` **GREEN_EXIT=0**
  (`1/1 Test #643: SfxrPitchBoundTest ....... Passed 0.53 sec`, `100% tests passed out of 1`). The tree was
  clean again afterwards, with the product change back in place (`git diff --numstat -- plugins/Sfxr/Sfxr.cpp`
  = `9 11` against the round-1 tip).
- **The test is deterministic: no wall-clock timing, sleeps or races.** There is no timer and no thread: the
  test renders the period in the calling thread and compares it, sample for sample, with the frames a
  reference `SfxrSynth` it drives itself produces. The seam is the synth's own model values, which both
  instances read from the same `SfxrInstrument` (the note's synth is created inside `playNoteImpl` from the
  same instrument), plus the default `SfxrWave::Square` waveform, whose code path reads no PRNG (the noise
  buffer is only read for `SfxrWave::Noise`, `plugins/Sfxr/Sfxr.cpp:243`); the engine's audio device is
  stopped in `initTestCase`, and the frame counts are exact integers of the period
  (`HeldNote::synthFramesForPeriod()`). No injections are needed because nothing in the path depends on time
  or randomness. The round-2 slot injects its input rather than waiting for anything: the tuning is written
  through `SessionTuning::setNoteFrequency(NormalKey, 1e30, ...)` - the same call the MTS-ESP path uses, so the
  value is one the product accepts - and the table is reset at the end of the slot and in `cleanupTestCase()`,
  so the other slots always run on 12-TET.
- **The fix is at the cause and complete, every caller and sibling checked.** `renderPitched()` has one
  caller (`SfxrInstrument::playNoteImpl`, `plugins/Sfxr/Sfxr.cpp:482`), `playNoteImpl` is the only note entry
  point the class overrides (`plugins/Sfxr/Sfxr.h:195`; the inherited `playNote(NotePlayHandle*, SampleFrame*)`
  forwards to it through `Instrument.h:114-118` and `Instrument.h:302-304`), and `SfxrSynth::update()` has no
  other caller in the plugin. Its count parameter moved from `int32_t` to `f_cnt_t` (the frame type,
  `include/LmmsTypes.h:43`) so the clamp is the only bound; the intermediate is `double` and the clamp is
  applied to that `double` (`plugins/Sfxr/Sfxr.cpp:510-513`), so neither the int32 overflow nor the
  out-of-range `double`-to-`f_cnt_t` conversion can return. The wider type the value flows into was checked
  with it: `renderPitched()` takes `f_cnt_t` for both the period's frames and the synth frames
  (`plugins/Sfxr/Sfxr.cpp:456`) and only ever narrows the 256-frame block and the discard tail to `int32_t`,
  both of which are inside `[0, 320]` once the count is clamped. Registration kept in step: `tests/fork-sources.txt`,
  `tests/all-sources.txt` (regenerated from its own recipe: `all-sources-reproduce.sh` prints
  `REPRODUCES`) and `tests/upstream-modifications.txt` (reason for the inherited file).
- **No collateral damage.** No model, setting, save or load path was touched: the change is inside the render
  function, so old projects load exactly as before. The bound only clamps notes that ask for more than 64
  synth frames per output frame, i.e. a pitch above 28 kHz against the 440 Hz base the default tuning reports
  - two octaves above the highest note that tuning reaches (ratio 28.5 at key 127), and for a microtuned
  track, more than six octaves above the keymap's own base frequency. Every note inside the bound renders
  exactly the frames it rendered before: the two truncations happen in the same order with the same float
  operations, and `normalPitchedNoteIsRenderedUnchanged` asserts a ratio-4 note still takes exactly 4 synth
  frames per output frame and still renders the unbounded reference - it passes in both the fixed and the
  reverted tree. `renderPitched()`'s resampling, block size and synth are untouched, so nothing else changes
  output.
- **Custom tunings above 64:1 are held to the bound at any absolute frequency - stated, not hidden.** The
  ceiling is a ratio (64 synth frames per output frame), so it does not depend on an absolute Hz value: a note
  is clamped when its frequency is more than 64x `InstrumentTrack::baseFreq()` (`src/tracks/InstrumentTrack.cpp:202`).
  In the default 12-TET tuning that base is 440 Hz, so only pitches above 28 kHz are held to it, two octaves
  above the highest note the tuning reaches. A session table (MTS-ESP) can put the *absolute* frequencies
  lower - a scale based on 110 Hz, say - and then a note six octaves above that base (7 kHz, audible) is
  clamped as well, and one tuned by MTS to any value past the base+6 octaves plays at the bound instead of at
  its own pitch. That is the intended trade the finding allows ("Bound accepted pitch ratios or redesign the
  resampling path so per-period work has a fixed limit"): past the bound the synth's own 22 kHz Nyquist is
  exceeded many times over and the output is aliasing, while the work has to stay finite. Notes inside the
  bound - every note of a 12-TET project, and every tuned note within six octaves of its base - render exactly
  what they rendered before, which `normalPitchedNoteIsRenderedUnchanged` pins.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED, and the pre-fix cause is
  quoted above with the lines it had (`plugins/Sfxr/Sfxr.cpp:490`, `:492`, `:456`, `:465`) and reproduced by
  the RED run in this session.
- **Sibling commands.** This fix changes no command file and no control command. The two commits of this job
  touch `plugins/Sfxr/Sfxr.cpp`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
  `tests/upstream-modifications.txt`, `tests/all-sources.txt`, `tests/src/plugins/SfxrPitchBoundTest.cpp` and
  this report - the round-2 commit is exactly `docs/qwen-fixes-2026-10/cluster-new-p16-228.md`,
  `plugins/Sfxr/Sfxr.cpp`, `tests/src/plugins/SfxrPitchBoundTest.cpp`
  (`git show --name-only --format= HEAD`). No file under `src/gui/` or the command registry
  (`ControlRegistry`, the `app.*` / `arrangement.*` command tables) is in either commit. The `app.*` and
  `arrangement.*` siblings the check lists come from other commits on this branch, not from these, so none of
  them is affected and the ControlCommandsSnapshot canary does not move.

## Other findings

- `SfxrSynth::update(nullptr, n)` writes `buffer[i][j] = 0.0f` with no null check once `playing_sample` is
  false (`plugins/Sfxr/Sfxr.cpp:157-163`). After this fix the plugin itself can no longer reach it: the
  discard call in `renderPitched()` only happens when the period asks for more than 256 synth frames per
  output frame, and the bound is 64. Not fixed here (nothing in the product reaches it any more); a future
  caller of that method with a null buffer and a finished sound would still fault.
- `pitchedNoteAboveTheBoundIsClampedToTheBoundedPitch` pins the limit's value (64) in the test, next to the
  source constant, as the existing limit tests in this tree do (e.g. the 256-variant limit test in
  `tests/src/core/SessionAudioCellPlaybackTest.cpp`): moving the constant has to move the test.
