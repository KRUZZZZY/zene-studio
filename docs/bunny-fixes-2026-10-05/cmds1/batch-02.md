# cmds1 batch-02 — 2026-10-05

Lane: control commands `ControlCommands[A-M]*.cpp`. 6 findings: 5 FIXED, 1 SKIPPED-SCOPE.

### Cluster 3771 — Repeating `track.set_solo` can corrupt solo state in headless mode

Verdict: FIXED

Evidence: `src/core/ControlCommandsArrangement.cpp:417-418` called `track->toggleSolo()` on the
headless path for every call, including one where `setSolo()` changed nothing. `Track::toggleSolo()`
(`src/core/Track.cpp:1222-1274`) re-captures `m_mutedBeforeSolo = isMuted()` for every track whenever
no other track was soloed, so a repeated `solo: true` overwrote the pre-solo snapshot with the
post-solo mute states. The call is now made only when the flag actually moved, which is exactly when
TrackView's `dataChanged` hook fires on a display-ful instance. 3771 and 5593 are two defects in the
same function, not duplicates of one finding; they are fixed and committed separately.

Commit: 5fb3a6bbc

Test: ControlEditCommandsTest::trackSetSoloRepeatedInHeadlessModeIsIdempotent

Red/green: `cd build/tests && ctest -R '^ControlEditCommandsTest$' --output-on-failure`
EXIT=8 — `FAIL! trackSetSoloRepeatedInHeadlessModeIsIdempotent '!muted(other)' returned FALSE.`
After the fix EXIT=8 with that case PASS (the 5593 case still failed, then green);
`bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 5593 — `track.set_solo` reports the wrong inverse for a no-op request

Verdict: FIXED

Evidence: the inverse built at `src/core/ControlCommandsArrangement.cpp:431-436` carried `!value`, so
an idempotent request advertised an inverse that changed state: `solo: true` on an already-soloed
track was recorded as reversing with `solo: false`. The transaction is recorded even for a call that
changed nothing, because `changedNothing()` returns false once a handler supplies `__transaction`
(`src/core/ControlRegistry.cpp:300`), so the wrong inverse was observable. It now names the solo state
the track was in before the call.

Commit: 3d7a49e5c

Test: ControlEditCommandsTest::trackSetSoloNoOpRequestInvertsToTheStateItFound

Red/green: `cd build/tests && ctest -R '^ControlEditCommandsTest$' --output-on-failure` EXIT=8 —
`FAIL! trackSetSoloNoOpRequestInvertsToTheStateItFound`, Actual 0 vs Expected 1. Green: same command
EXIT=0 (11 passed); `ctest -R '^ReversibilityUndoTest$'` EXIT=0; gates EXIT=0 / EXIT=0.
Note: the reply never carries `__transaction` (`src/core/ControlSchema.cpp:230` strips it), so the
regression reads the recorded transaction through `control.transactions`.

### Cluster 5595 — Batch disarm reports unchanged tracks as changed

Verdict: FIXED

Evidence: `disarmTrackArm()` always succeeds (`src/core/ControlCommandsArrangementState.cpp:164-175`),
so `armEach()` appended every named track to `changed` even when its route was already disarmed, and
a repeated id was acted on and reported once per occurrence. The batch now skips a route already in the
requested state, asking the route's own `armed_before`, and acts on each id once. The Busy reply for
an all-already-disarmed batch became reachable for the first time, so its wording follows the request
direction.

Commit: bf6a8ac01

Test: ControlEditCommandsTest::trackSetArmManyReportsOnlyTheTracksItChanged

Red/green: `cd build/tests && ctest -R '^ControlEditCommandsTest$' --output-on-failure` EXIT=8 —
`FAIL! trackSetArmManyReportsOnlyTheTracksItChanged '!noop.ok' returned FALSE. (a disarm of
already-disarmed routes reported changed tracks)`. Green: same command EXIT=0 (12 passed);
`ctest -R '^UndoHonestyTest$'` EXIT=0; gates EXIT=0 / EXIT=0.

### Cluster 4736 — Track reordering can detach an active recording route from its track

Verdict: SKIPPED-SCOPE

Evidence: the mapping is real — `trackRouteOf()` returns the track's index in the song
(`src/core/ControlCommandsArrangementState.cpp:132-140`) and the command's own contract documents it
("a song track is mapped to a RECORD ROUTE by its position in the song", same file, lines 105-130), so
inserting, moving or removing tracks while a route is armed does resolve a different route. Fixing it
is a design change, not a local correction: binding routes to stable track ids needs a persistent
track-to-route association that does not exist, and the alternative the report offers — refusing
track moves and removals while a route is armed — changes the behaviour of the track.remove /
reorder commands across the group. Left alone per rules 7 and 8.

Commit: none

Test: none

Red/green: none

### Cluster 2793 — Audio analysis accepts a zero sample rate

Verdict: FIXED

Evidence: `audioClipOf()` checked the clip resolves, carries data and has a non-empty window, but not
the rate (`src/core/ControlCommandsAudioAnalysis.cpp:63-71`). `clip.audio_to_midi` handed that rate
to the pitch detector (line 86) and `sample.spectrogram`'s `bandBins()` divides by it for Nyquist
(lines 115-118) and reported `high_hz` as `rate / 2` (line 160). A nonpositive rate is now refused
with InvalidArgs, naming the reason. `SampleBuffer` itself does not validate the rate
(`src/core/SampleBuffer.cpp:38-42`), so the malformed state is constructible and the test builds it.

Commit: 9f558da0c

Test: AudioAnalysisTest::anAudioClipWithNoSampleRateIsRefused

Red/green: `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure` EXIT=8 —
`FAIL! anAudioClipWithNoSampleRateIsRefused`, Actual 1 vs Expected InvalidArgs (3): the rate-0 clip
was analysed and only happened to find no pitched line. Green: same command EXIT=0; gates EXIT=0 /
EXIT=0.

### Cluster 3781 — Invalid split positions are silently ignored

Verdict: FIXED

Evidence: the `positions` branch converted every entry with `toInt()`, which answers 0 for anything it
cannot convert, and the out-of-range filter then dropped that 0 exactly as quietly as it drops a real
cut past the end (`src/core/ControlCommandsAudioSlice.cpp:86-88` before the change). Entries are now
checked to be whole numbers before the undo step is taken; a whole number outside the clip is still
ignored, as the command's own description states ("cuts outside the clip are ignored", line 257). The
check is a separate helper because the inline loop pushed `cutPoints` to CCN 12 and the complexity gate
flagged it.

Commit: 13520241d

Test: AudioEditingTest::splittingAtPositionsRefusesAnEntryThatIsNotATick

Red/green: `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure` EXIT=8 —
`FAIL! splittingAtPositionsRefusesAnEntryThatIsNotATick '!mixed.ok' returned FALSE. (a split position
that is not a tick was ignored and the rest went ahead)`. Green: same command EXIT=0; AudioEditingTest
is the only suite driving these commands; gates EXIT=0 / EXIT=0.

Leftovers: cluster 4736 stands unfixed by design (route/track association is positional and stable-id
binding is a design change); it may be worth raising as its own task. Cluster 5593 also suggested
skipping the undo step when nothing changed — not done, as that would alter the composite-checkpoint
contract ReversibilityUndoTest::trackSetSoloIsOneUndoableStepForEveryTrack pins.