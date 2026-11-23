FIXED

# cluster new-p9-552 - Replacing a sampler file can race active playback

## Verdict

REAL, and fixed. `AudioFileProcessor::setAudioFile()` replaced the instrument's `Sample`
(and with it the non-atomic `std::shared_ptr<const SampleBuffer>` it renders from) on the
control thread while `playNoteImpl()` read the same object on the render/audio workers. A
worker could observe a half-updated `Sample`, and the copy assignment could free the old
buffer while a note was still reading its frames.

Round-3 revision (the round-2 reviewer's checkpoint): the snapshot design is kept and the two
missing pieces are added - the playback cursor is atomic, and the whole publish sequence in
`setAudioFile()` runs under one engine model guard. The product diff against `21c2f3ee2e` is
**40 added non-comment lines** (limit 40; round 1 was 51).

## Evidence (code before the fix, branch `bugs/qwen-new-p9-552`)

- `plugins/AudioFileProcessor/AudioFileProcessor.cpp:444` (old) - `setAudioFile()` did
  `m_sample = Sample(SampleBuffer::fromFile(_audio_file));` with no synchronisation.
- `plugins/AudioFileProcessor/AudioFileProcessor.cpp:344` (old) - the embedded-sample
  branch of `loadSettings()` did the same `m_sample = Sample(...)`.
- `plugins/AudioFileProcessor/AudioFileProcessor.cpp:166,181,210` (old) - `playNoteImpl()`
  reads `m_sample` on the render path (`startFrame()`, `endFrame()`, `m_sample.play(...)`).
- `include/Sample.h:148` - `Sample` holds `std::shared_ptr<const SampleBuffer> m_buffer`
  (not atomic). `src/core/Sample.cpp:72` - `Sample::operator=(const Sample&)` assigns it.
  Assigning a `shared_ptr` while another thread reads the same `shared_ptr` is a data race,
  and the assignment can drop the last reference and free the sample data under the reader.

The render path is the audio path: `AudioEngine::renderNextPeriod()` holds
`m_changeMutex` (a `std::recursive_mutex`, `include/AudioEngine.h:888`) for the whole
period and runs instrument `playNoteImpl()` inside it. Nothing in the old `setAudioFile()`
took that lock.

## Fix (at the cause)

- Every sounding note captures the sample source once, on its first period
  (`plugins/AudioFileProcessor/AudioFileProcessor.cpp:192`): the voice's `NoteVoice` carries
  a `Sample` snapshot and loop mode (`AudioFileProcessor.h:139-146`). The note renders from
  that snapshot for its whole life (`AudioFileProcessor.cpp:214-220`). While the instrument
  still publishes the same buffer, the snapshot is refreshed from the instrument (one
  `Sample` copy of its atomic point/reverse/amp settings), so knobs and automation stay
  live; after a replacement the note finishes on the buffer it started on instead of
  switching under playback.
- `replaceSample()` (`AudioFileProcessor.cpp:466-482`) is now the only place that publishes
  a new instrument sample, for both `setAudioFile()` (`:456`) and the embedded-sample branch
  of `loadSettings()` (`:353`). It takes the engine's model guard for the swap, so a note's
  first-period capture cannot observe a half-replaced `Sample`.
- Inside that same guard it retires the old buffer in `m_retiredSamples` and erases entries
  whose `use_count()` is 1 (`:474-479`): the last reference a sounding note drops on the
  audio thread therefore never frees the sample data - the control thread does, at the next
  publish. The guard (recursive mutex) also serialises two control threads publishing.
- `beatLen()` is also reached from a MIDI-input thread that does not hold the model lock,
  so it reads the mirrored source rate (`std::atomic<int> m_sampleRate`,
  `AudioFileProcessor.h:156`, `AudioFileProcessor.cpp:423-425`) instead of dereferencing the
  buffer pointer.

- Round 3, cursor: `m_nextPlayStartPoint` / `m_nextPlayBackwards` are now
  `std::atomic<f_cnt_t>` / `std::atomic<bool>` (`AudioFileProcessor.h:179-180`). They are
  written by the knob/automation slots (control thread), by the stutter and end-of-note
  writes on the render thread, and by the magic-key write, and read by note starts and by
  `beatLen()` (MIDI thread). The existing assignments and comparisons compile unchanged, so
  they use the default (sequentially consistent) ordering, not an explicit relaxed one;
  `beatLen()` needed one `static_cast<f_cnt_t>` (`AudioFileProcessor.cpp:429`) because the
  `?:` operands differ in type.
- Round 3, atomic publish: `setAudioFile()` decodes the file first (outside the lock), then
  holds the engine model guard across `replaceSample()`, `loopPointChanged()`,
  `ampModelChanged()` and `reverseModelChanged()` (`AudioFileProcessor.cpp:458-466`), so the new
  sample, its point frames and the reset cursor appear together to a starting note. The guard
  is released before the final `emit sampleUpdated()`. `replaceSample()` now takes the guard
  with the same RAII idiom (`:480`); the lock is recursive, so the nesting is safe. The
  `sampledata` branch of `loadSettings()` (`:353`) calls `replaceSample()`, which holds the
  guard for the swap; it does not call the point/amp/reverse slots (the model loads that
  follow do), so there is no wider sequence to hold there.
- Known limit: `loopPointChanged()`, `ampModelChanged()` and `reverseModelChanged()` emit
  `sampleUpdated()` themselves, so those inner emits run under the guard (the test below
  relies on that to observe it). Only the final emit is outside. The handlers are GUI repaints.

Realtime: the only audio-thread additions are a `shared_ptr` copy (atomic refcount) for the
per-note snapshot, one `Sample` assignment for the live-settings refresh (atomic loads and
refcount ops), and an atomic rate read in `beatLen()`. Nothing on the render path allocates,
frees, locks, waits, logs or emits.

Product-line count (the round-1 send-back):

- `git diff 21c2f3ee2e -- plugins/AudioFileProcessor/AudioFileProcessor.cpp plugins/AudioFileProcessor/AudioFileProcessor.h | grep '^+' | grep -v '^+++' | sed 's/^+//' | sed 's/^[[:space:]]*//' | grep -v '^//' | awk 'NF' | wc -l` -> `40` (limit 40; round 1 measured 51; round 2 was 30 before the cursor/guard additions).

## Regression test

`tests/src/core/SamplerVoiceReuseTest.cpp:185`
`replacingTheSampleKeepsASoundingNoteOnItsSource`

It starts a held note on a flat 0.5 sample, replaces the sampler's file with a ramp that
starts at 0 while the note is sounding, and checks that the note's next period is still the
flat 0.5 buffer (peak > 0.4) rather than the ramp (~0.01). It then checks that a note
started after the replacement plays the new ramp (peak < 0.1), and that a second
replacement still leaves the first note on its own source. Deterministic: fixed sample
files, fixed period counts, no sleeps, threads, clocks or timestamps.

Second regression test, `tests/src/core/SamplerVoiceReuseTest.cpp:235`
`setAudioFilePublishesUnderOneModelGuard`: a direct-connected probe on `sampleUpdated()`
records `AudioEngine::isChangeInModelHeld()` (thread-local lock depth of the emitting thread)
at each emission. With the fix the first emission (from `ampModelChanged()`) is under the
guard and the last (the final emit) is not. Without it the first emission is unguarded.
Deterministic: single thread, no timing. It covers the atomic-publish part; the cursor
atomics are a type-level change with no deterministic behavioural test (TSan does not run on
this box), so they are justified by the design argument only.

The allocation case `noteStartAndVoiceReuseDoNotAllocate` (`:223`) is a companion check, not
proof of the lifetime fix: it counts `operator new` only, during note start, voice reuse and
rendering; it stops counting before the handles are released, so it says nothing about
frees at disposal. The "no free on the audio thread" guarantee above is a design argument
(retirement list, `use_count()==1`), not something that test measures.

## Test matrix

| path | coverage |
| --- | --- |
| normal playback | note sounds its file; existing `SamplerVoiceReuseTest` cases pass |
| reconfiguration (the bug) | `replacingTheSampleKeepsASoundingNoteOnItsSource`: held note keeps its source, new note uses the new file, a second replacement does not disturb it |
| failure / missing file | `SampleBuffer::fromFile()` still returns the empty buffer on a decode failure; `replaceSample()` treats the empty buffer as "nothing to retire" and publishes it, so a failed load behaves as before |
| exhaustion | `everyNoteSoundsWhenItStarts` and the voice-limit/reuse cases exercise the pool at Capacity; the per-note snapshot does not change voice accounting |
| audio-thread constraints | `noteStartAndVoiceReuseDoNotAllocate` counts 0 `operator new` across start, reuse and render; no locks/allocations added to `playNoteImpl` |
| old projects | `loadSettings()` still loads `src` through `setAudioFile()` and `sampledata` through `replaceSample()` with identical `m_sample` contents; `SamplerEnvelopeTest` (defaults, save/load round trip) passes |
| supported platforms | no Qt-6-only API, no platform-specific code; the change is portable C++20 (`<atomic>`, `<vector>`) |

## Commands and results (logs in `~/.config/kads/zene/state/runs/new-p9-552.r3/logs/`)

Run by a Haiku assistant, with the exit codes and test lines checked against the logs by me.
`ctest -R Sampler` returns 8 because `SamplerTakeGuiTest` is registered but its binary is not
built in this tree (`Not Run`); the three built Sampler tests passed (`test-1.log`, `test-4.log`).

- Green, tree as committed + round-3 edits: `cmake --build build --target zene SamplerVoiceReuseTest -j4` -> `EXIT=0` (`build-1.log`);
  `ctest -R Sampler` -> SamplerTakeTest, SamplerEnvelopeTest, SamplerVoiceReuseTest Passed (`test-1.log`).
- Step 4b, red with only the round-3 delta reversed (`git diff HEAD -- plugins > delta.patch; git apply -R delta.patch`
  -> exit 0, rebuild `EXIT=0` `build-2.log`): `ctest -R SamplerVoiceReuseTest` -> `EXIT=8`,
  9 passed 1 failed (`test-2.log`):
  `FAIL! : SamplerVoiceReuseTest::setAudioFilePublishesUnderOneModelGuard() 'probe.held.front()' returned FALSE.`
  Delta restored with `git apply delta.patch` -> exit 0.
- Step 4b, red with the whole product fix reversed (`git diff 21c2f3ee2e -- plugins > fix.patch; git apply -R fix.patch`
  -> exit 0, rebuild `EXIT=0` `build-3.log`): `ctest -R SamplerVoiceReuseTest` -> `EXIT=8`,
  8 passed 2 failed (`test-3.log`):
  `FAIL! : ... replacingTheSampleKeepsASoundingNoteOnItsSource() ... (the sounding note switched samples: last-period peak 0.010437)` and
  `FAIL! : ... setAudioFilePublishesUnderOneModelGuard() 'probe.held.front()' returned FALSE.`
- Restored with `git apply fix.patch` -> exit 0; `git diff 21c2f3ee2e -- plugins | cmp - fix.patch` -> `CMP=0`;
  rebuild `EXIT=0` (`build-4.log`); `ctest -R Sampler` -> the three built tests Passed (`test-4.log`).
- Family: `cmake --build build --target zene SamplerTakeTest AudioFileProcessorWaveViewTest AudioFileProcessorPlayheadTest SamplerEnvelopeTest SamplerVoiceReuseTest -j4`
  -> `EXIT=0` (`build-family.log`); `ctest -R '^(SamplerTakeTest|AudioFileProcessorWaveViewTest|AudioFileProcessorPlayheadTest|SamplerEnvelopeTest|SamplerVoiceReuseTest)$'`
  -> `EXIT=0`, `100% tests passed out of 5` (`test-family.log`).
- Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> `EXIT=0`, `RESULT: PASS`,
  all eight rows PASS (`feature-check.log`).

Not verified: no ThreadSanitizer run (it does not run on this box); the race reasoning is by design.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  Reversed the product patch (whole fix, and separately the round-3 delta), rebuilt, and saw the
  failures quoted above (`test-2.log`, `test-3.log`); re-applied, rebuilt, and the tests pass
  (`test-4.log`). Exact commands and logs are listed above.
- **The test is deterministic: no wall-clock timing, sleeps or races.** It uses fixed WAV
  fixtures (a flat 0.5 file and a ramp), a fixed number of rendered periods, and checks the
  rendered peak of the last period. It starts no thread and reads no clock; all ordering is
  explicit (`renderPeriods` calls `NotePlayHandle::play` directly).
- **The fix is at the cause and complete; every caller and sibling was checked.**
  `setAudioFile()` (`AudioFileProcessor.cpp:456`) and the embedded-sample branch of
  `loadSettings()` (`:353`) both go through `replaceSample()`; the `src` branch of
  `loadSettings()` already goes through `setAudioFile()`. `m_sample` is no longer read from
  the render path except its atomic point/reverse/amp settings and the buffer-identity check
  under the engine's model guard. `beatLen()` was the other off-render reader of the buffer
  and now uses the mirrored atomic rate. `AudioFileProcessorView`/`AudioFileProcessorWaveView`
  are GUI-thread consumers of `sample()` and are unchanged.
- **No collateral damage.** A failed or empty load still publishes the empty buffer as
  before; a note started before any sample was loaded stays silent rather than switching
  mid-note (that switch *was* the bug). Old projects load both `src` and `sampledata`
  through the same `Sample` construction as before. No new limit or rejection was added.
  `SamplerEnvelopeTest` (Q050-103 defaults, persistence, old-project defaults) passes.
- **ALREADY-FIXED / FALSE-POSITIVE** does not apply; the verdict is FIXED.
- **Sibling commands in the command files changed.** This fix changes no command file, no
  control-command registration and no commands snapshot; it is a sampler playback fix
  driven by the existing `setAudioFile` slot and the existing project-load paths. The
  command files the branch touches (for example `app.*` and `arrangement.*`) are other
  clusters' work on this train and are unaffected by this change; none reads or writes a
  sampler sample buffer.

## Other findings (not fixed here)

- `AudioFileProcessorWaveView` keeps a raw `Sample const*` into the instrument and repaints
  from it; a control-command thread replacing the file while the GUI paints could race with
  it. Outside this finding's audio-playback scope.

Delegation: a Haiku assistant ran every build and ctest run (green, the two red reversals,
restore, the family run) and the feature-check gate, and reported exit codes and test lines
that I re-read from the logs. I designed and wrote the code, both tests, and this report.
