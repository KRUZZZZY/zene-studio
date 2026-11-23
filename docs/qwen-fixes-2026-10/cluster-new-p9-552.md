FIXED

# cluster new-p9-552 - Replacing a sampler file can race active playback

## Verdict

REAL, and fixed. `AudioFileProcessor::setAudioFile()` replaced the instrument's `Sample`
(and with it the non-atomic `std::shared_ptr<const SampleBuffer>` it renders from) on the
control thread while `playNoteImpl()` read the same object on the render/audio workers. A
worker could observe a half-updated `Sample`, and the copy assignment could free the old
buffer while a note was still reading its frames.

Round-2 revision: the code was trimmed to **30 added non-comment product lines** (limit
40; round 1 was 51) with no change in behaviour. The mutex and the GUI-poll pruning call
were removed; retirement now happens inside the model-guarded publish in `replaceSample()`,
which serialises control threads as well as the render thread. The report below describes
the trimmed code and the re-run proof.

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

Realtime: the only audio-thread additions are a `shared_ptr` copy (atomic refcount) for the
per-note snapshot, one `Sample` assignment for the live-settings refresh (atomic loads and
refcount ops), and an atomic rate read in `beatLen()`. Nothing on the render path allocates,
frees, locks, waits, logs or emits.

Product-line count (the round-1 send-back):

- `git diff 21c2f3ee2e -- plugins/AudioFileProcessor/AudioFileProcessor.cpp plugins/AudioFileProcessor/AudioFileProcessor.h | grep '^+' | grep -v '^+++' | sed 's/^+//' | sed 's/^[[:space:]]*//' | grep -v '^//' | awk 'NF' | wc -l` -> `30` (limit 40; round 1 measured 51).

## Regression test

`tests/src/core/SamplerVoiceReuseTest.cpp:185`
`replacingTheSampleKeepsASoundingNoteOnItsSource`

It starts a held note on a flat 0.5 sample, replaces the sampler's file with a ramp that
starts at 0 while the note is sounding, and checks that the note's next period is still the
flat 0.5 buffer (peak > 0.4) rather than the ramp (~0.01). It then checks that a note
started after the replacement plays the new ramp (peak < 0.1), and that a second
replacement still leaves the first note on its own source. Deterministic: fixed sample
files, fixed period counts, no sleeps, threads, clocks or timestamps.

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

## Commands and results (logs in `~/.config/kads/zene/state/runs/new-p9-552.r2/logs/`)

Trimmed fix builds and passes:

- `cmake --build build --target audiofileprocessor SamplerVoiceReuseTest -j4` -> `EXIT=0` (`build-1.log`)
- `ctest -R '^SamplerVoiceReuseTest$' --output-on-failure` -> `EXIT=0` (`test-green-1.log`)

Step 4b, reverting the whole product change to the base commit and rebuilding:

- `git diff 21c2f3ee2e -- plugins/AudioFileProcessor/AudioFileProcessor.cpp plugins/AudioFileProcessor/AudioFileProcessor.h > .../logs/fix.patch` -> `DIFF_EXIT=0`
- `git apply -R .../logs/fix.patch` -> `REVERSE_APPLY_EXIT=0`
- `cmake --build build --target audiofileprocessor SamplerVoiceReuseTest -j4` -> `BUILD_EXIT=0` (`build-red-without-fix.log`)
- `ctest -R '^SamplerVoiceReuseTest$' --output-on-failure` -> `TEST_EXIT=8` (`test-red-without-fix.log`)
  `FAIL! : ... replacingTheSampleKeepsASoundingNoteOnItsSource() 'lastPeriodPeak(held) > 0.4f' returned FALSE. (the sounding note switched samples: last-period peak 0.010437)`
- `git apply .../logs/fix.patch` -> `APPLY_EXIT=0`
- `cmake --build build --target audiofileprocessor SamplerVoiceReuseTest -j4` -> `BUILD_EXIT=0` (`build-green-after-reapply.log`)
- `ctest -R '^SamplerVoiceReuseTest$' --output-on-failure` -> `TEST_EXIT=0` (`test-green-after-reapply.log`)

Family (all tests around the changed area):

- `ctest -R '^(SamplerTakeTest|AudioFileProcessorWaveViewTest|AudioFileProcessorPlayheadTest|SamplerEnvelopeTest|SamplerVoiceReuseTest)$' --output-on-failure` -> `EXIT=0`, 5/5 passed (`test-family-1.log`)

Gates:

- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> `EXIT=0`, `RESULT: PASS` (`feature-check-1.log`), all eight rows PASS including complexity and file-length.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  Reverted the cumulative product patch to the base commit and rebuilt (`REVERSE_APPLY_EXIT=0`,
  `BUILD_EXIT=0`): `TEST_EXIT=8` with the failure quoted above. Re-applied
  (`APPLY_EXIT=0`), rebuilt, reran: `TEST_EXIT=0`. Exact commands and logs are listed above.
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

- `m_nextPlayStartPoint` / `m_nextPlayBackwards` are plain members written by the control
  thread (`pointChanged()`, `stutterModelChanged()`, `setAudioFile()`'s follow-up calls)
  and read/written by `playNoteImpl()` on the render thread, and `m_nextPlayStartPoint` is
  also read by `beatLen()` on a MIDI thread. That is a pre-existing value race unrelated to
  the buffer lifetime this cluster reports; making them atomic would touch far more of the
  playback state and was left for a separate finding.
- `AudioFileProcessorWaveView` keeps a raw `Sample const*` into the instrument and repaints
  from it; a control-command thread replacing the file while the GUI paints could race with
  it. Also outside this finding's audio-playback scope.
