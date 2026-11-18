# engine batch-22 — 2026-10-05

Clusters 4595, 4599, 5430, 5489, 5490, 4584.

One fixed, five REAL-UNTESTED: each of the remaining five is a real code
defect, but every deterministic regression for them needs capture hardware
(ALSA) or a JACK server, or is a race that cannot be forced headlessly.

### Cluster 4595 — Configured capture channels do not select the stereo bus pair
Verdict: FIXED
Evidence: `include/AudioDevice.h:126` declared
`prepareCapture(f_cnt_t maxFrames, int left = 0, int right = 1)`, and only
ALSA passed a pair (`src/core/audio/AudioAlsa.cpp:419-420`, clamped to its
granted width). JACK (`src/core/audio/AudioJack.cpp:249`), PortAudio
(`src/core/audio/AudioPortAudio.cpp:179`) and SDL
(`src/core/audio/AudioSdl.cpp:114`) passed the block size alone, so
`publishCaptured()` kept routing the stereo bus through channels 0/1
(`src/core/audio/AudioDevice.cpp:74-85`) no matter what `audioinput/left`
and `audioinput/right` said. A negative pair member now means "the
configured pair" (`AudioInputPath::configuredPlan()`, `src/core/AudioInputPath.cpp:87-100`),
resolved off the audio thread in `prepareCapture()`; ALSA keeps passing the
pair it clamps itself, and `publishCaptured()` still clamps to each block's
width. The N-channel path and the frame counter are untouched.
Commit: d0f3eca68
Test: CapturePublisherTest::aConfiguredPairReachesTheStereoBusWhenTheBackendPassesNone
Red/green:
  Red:  `cmake --build build --target zene CapturePublisherTest > build/bunny-b.log 2>&1` EXIT=0
        `cd build/tests && ctest -R '^CapturePublisherTest$' --output-on-failure` EXIT=8
        (bus[i][0] was 0 where configured channel 2 = 2000 was expected)
  Green:`cmake --build build --target zene CapturePublisherTest > build/bunny-b.log 2>&1` EXIT=0
        `cd build/tests && ctest -R '^CapturePublisherTest$' --output-on-failure` EXIT=0
        `cd build/tests && ctest -R '^(RecordingInputPathTest|RecordingLatencyTest)$'` EXIT=0
        `bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0

### Cluster 4599 — JACK recording latency uses the wrong capture block size
Verdict: REAL-UNTESTED
Evidence: `AudioDevice::captureBlockFrames()` returns
`framesPerAudioBuffer()` (`src/core/audio/AudioDevice.cpp:94-97`) and
`AudioDummy` overrides it (`include/AudioDummy.h:55`), but
`include/AudioJack.h:67-68` declares only `inputLatencyFrames()` and
`outputLatencyFrames()`, so JACK reports the engine's configured buffer size
while its callback renders JACK's own `nframes`
(`src/core/audio/AudioJack.cpp:200, 436`, `jack_set_buffer_size` still
commented out at :315). `AudioEngine::refreshRecordingLatency`
(`src/core/AudioEngine.cpp:245-252`) sums that figure, so the reported
latency is wrong by the difference. The defect is real; a regression needs a
live JACK server (`m_client` is null otherwise, so `jack_get_buffer_size()`
cannot be called), and no JACK-server test harness exists in `tests/`
(`MidiJackOutScheduleTest` exists precisely to avoid one).
Commit: none. Test: none. Red/green: none.

### Cluster 5430 — Replacing track versions edits clips outside the audio guard
Verdict: REAL-UNTESTED
Evidence: `TrackVersions::replaceClips()` holds
`requestChangesGuard()` only around `track->deleteClips()`
(`src/core/TrackVersions.cpp:84-89`); `createClip()` and
`clip->restoreState()` then run unguarded (`:90-94`), and `createClip()`
reaches `Track::addClip()`, which pushes into `std::vector<Clip*> m_clips`
(`src/core/Track.cpp:808-816`, `include/Track.h:463`) — the list the audio
thread walks via `Song::processNextBuffer()` →
`buildAutomationRamps()` → `track->getClips()`
(`src/core/Song.cpp:418`, `src/core/AutomationClip.cpp:1040`) and recording
via `Track::getClipsInRange()` (`src/core/Song.cpp:576-583`,
`src/core/Track.cpp:924-937`). The unsynchronised window is genuine. Forcing
it deterministically needs a thread interleaving between the main thread's
clip creation and the render thread's clip walk; no headless harness in
`tests/` can order those two, so no failing test could be written and the
code was left alone per procedure step 2.
Commit: none. Test: none. Red/green: none.

### Cluster 5489 — Permanent ALSA capture errors can spin and leave capture reported open
Verdict: REAL-UNTESTED
Evidence: `captureLoop()` discards `handleCaptureError()`'s return value and
`continue`s on both `snd_pcm_wait` failure
(`src/core/audio/AudioAlsa.cpp:572-576`) and `snd_pcm_readi` failure
(`:601-605`), while `handleCaptureError()` returns 0 for the recoverable
`-EPIPE`/`-ESTRPIPE` cases and `_err` for anything else (`:509-546`). An
unrecoverable error therefore re-enters the loop, and because
`closeCapture()` only publishes `AudioInputPath::publishClosed("closed")` when
`m_captureOpen` is true (`:467-481`), capture stays reported open. Real, but
it needs a capture device that fails permanently — no such fixture exists in
`tests/` (`TwoTrackAlsaCaptureProbe` drives a real external ALSA client).
Commit: none. Test: none. Red/green: none.

### Cluster 5490 — ALSA shutdown can hang during suspend recovery
Verdict: REAL-UNTESTED
Evidence: the `-ESTRPIPE` branch retries
`while( (err = snd_pcm_resume(m_captureHandle)) == -EAGAIN) sleep(1);`
without consulting `m_captureStop` and without a bound
(`src/core/audio/AudioAlsa.cpp:526-544`), while `closeCapture()` sets the
stop flag (`:467`) and joins the thread (`:487`). Real, and the fix is small,
but reaching it needs a suspended ALSA capture device that keeps returning
`-EAGAIN`; no headless fixture can produce that.
Commit: none. Test: none. Red/green: none.

### Cluster 4584 — ALSA capture error recovery logs from the capture thread
Verdict: REAL-UNTESTED
Evidence: `handleCaptureError()` `printf`s on failed overrun recovery
(`src/core/audio/AudioAlsa.cpp:516-517`) and failed suspend recovery
(`:539-540`), and both are called from `captureLoop()`
(`:574`, `:603`) — the very loop whose own comment says no logging is
allowed there (`:558-561`). Real (a stdio call can block the capture
thread), but exercising either branch needs a real ALSA capture overrun or
suspend. Untested per procedure step 2, so the code was left alone.
Commit: none. Test: none. Red/green: none.

Leftovers: 4599 (needs a JACK server fixture), 5430 (needs a forced
main/render thread interleaving) and 5489/5490/4584 (need an ALSA capture
device that errors, suspends or xruns) remain unfixed for want of a
deterministic headless regression, not for want of a fix.