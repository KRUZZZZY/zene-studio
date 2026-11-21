FIXED

# cluster new-p12-285 — frozen take voice lifecycle

## Decision

FIXED. A frozen take voice is now tied to the take it was armed for. Mute, unfreeze
(clear) and take replacement each move the track's take generation, and the render
gate retires a voice whose generation no longer matches. The three reported symptoms
(mute, unfreeze and replace leaving an old take audible) are the three take-state
paths, and each is covered and has its own failing-then-passing slot.

## Code evidence

- `src/core/Track.cpp:359-372` — `subscribeFrozenTakeSignals()`: the muted-model
  connection no longer just clears `m_frozenTakePlaying`; it also does
  `m_frozenTakeGeneration.fetch_add(1, std::memory_order_release)`, so a live pass's
  already-armed voice is retired. A seek (`playbackPositionJumped`) deliberately still
  only clears the flag, because a loop wrap must keep older overlapping passes alive
  (`docs/m0-design/voice-pool.md`).
- `src/core/Track.cpp:1409-1412` — `freezeTo()` bumps the generation after installing
  the new `m_frozen`/buffer, so replacing a take invalidates voices armed for the old one.
- `src/core/Track.cpp:1445-1448` — `clearFrozenTake()` bumps the same generation, which is
  the unfreeze path (`freeze.unfreeze` clears the take), so unfreezing retires the voice.
- `src/core/Track.cpp:1542-1547` — `playFrozenTake()` captures
  `const std::uint64_t takeGeneration = frozenTakeGeneration();` *before* the mute check,
  so a mute racing the arm is still observed by the render gate.
- `src/core/Track.cpp:1630` — `handle->setFrozenTakeGeneration(takeGeneration);` stamps
  the armed handle with the take it belongs to.
- `src/core/SamplePlayHandle.cpp:552-563` — `SamplePlayHandle::play()` compares the
  handle's stamp against `m_track->frozenTakeGeneration()` (one acquire load) and, when
  they differ, zeroes the buffer and sets `m_frame = totalFrames()` so the stale voice
  ends silently.
- `include/Track.h` declares the acquire accessor `frozenTakeGeneration()` and the
  control-thread-written `std::atomic<std::uint64_t> m_frozenTakeGeneration{0}`.
  `include/SamplePlayHandle.h` declares the per-handle stamp and setter; it is seeded in
  `armPooledVoice()` and cleared in `resetPooledVoice()`.
- `src/core/SamplePlayHandle.cpp:489-496` records the arm-time generation and keeps
  `m_ignoresTrackMute` for frozen voices (the generation gate, not the mute flag, is what
  silences an already-armed voice).

## Cause fix and why every take-state path is covered

The root cause was that a frozen voice carried no identity for the take it rendered and
ignored the track mute, so once armed it kept rendering after the take's state changed.
The fix adds a monotonic per-track generation and an arm-time stamp:

1. mute (`subscribeFrozenTakeSignals` lambda),
2. take replacement (`freezeTo` earlier in the same call, before new voices can arm),
3. unfreeze/clear (`clearFrozenTake`).

Those are the only places that change which take a track owns or whether it is audible;
the seek signal is intentionally excluded per the voice-pool contract. The capture in
`playFrozenTake` happens before the mute check, closing the arm/mute race: whichever
side runs first, either the handle carries the new generation or the gate sees the
bumped one. `resetPooledVoice` clears the stamp so a recycled slot cannot inherit an old
generation, and `armPooledVoice` seeds it. Non-frozen pooled voices are untouched
because the gate is guarded by `isFrozenTakePoolVoice()`. No saved format changed: the
generation is runtime state with a zero default, so old projects load and render as before.

## Tests and RED/GREEN evidence

`tests/src/core/FrozenTakeVoiceLifecycleTest.cpp` (QTEST_GUILESS_MAIN) has three slots:

| Slot | RED (product change reverted) | GREEN (fix in place) |
|---|---|---|
| `muteStopsAnActiveFrozenVoice` | 0.356029 rendered after mute, exit 1 | pass, exit 0 |
| `unfreezeStopsAnActiveFrozenVoice` | 0.356029 rendered after unfreeze, exit 1 | pass, exit 0 |
| `replacingTheTakeStopsAnActiveFrozenVoice` | 0.356029 from the replaced take, exit 1 | pass, exit 0 |

The combined RED run reports `Totals: 2 passed, 3 failed` (the two passes are
`initTestCase`/`cleanupTestCase`); the combined GREEN run reports `Totals: 5 passed,
0 failed`. Each slot was then run alone against the reverted and restored build.

Determinism: the test stops the audio device, enables
`AudioEngineWorkerThread::setDeterministicProcessing(true)`, and drives
`AudioEngine::renderNextPeriod()` itself over fixed 440 Hz / 660 Hz WAV fixtures. It
discards exactly one known lag period and compares RMS against fixed thresholds; there
is no wall-clock timing or sleep in any assertion (the `qWait(130)` is teardown only,
letting the control-thread recycler return slots).

## Commands and exit codes

Recorded by the quiet runner; full logs are under
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-285.r1/logs/`. Every per-slot and
combined RED/GREEN log ends with its own `EXIT=` line, appended with
`; echo EXIT=$? >> <log>`, so the exit column below is read from the log rather than
reconstructed. `build-4-red` and `build-4-green` are the rebuilds after reverse-applying
and re-applying the product patch (four files).

| Step | Command | Exit | Evidence |
|---|---|---|---|
| build-1 | build of `lmmsobjs` + `FrozenTakeVoiceLifecycleTest` | 0 | `build-1.log` ends `Built target FrozenTakeVoiceLifecycleTest` |
| build-2 | rebuild of `FrozenTakeVoiceLifecycleTest` | 0 | `build-2.log` ends `Built target FrozenTakeVoiceLifecycleTest` |
| build-4-red | rebuild after reverse-applying `fix.patch` | 0 | `build-4-red.log` ends `Built target FrozenTakeVoiceLifecycleTest` |
| per-slot RED | `./FrozenTakeVoiceLifecycleTest <slot>` (reverted), each of the 3 slots | 1 each | `test-3-red-mute.log`, `test-3-red-unfreeze.log`, `test-3-red-replace.log` each end `EXIT=1` (`Totals: 2 passed, 1 failed`) |
| combined RED | `./FrozenTakeVoiceLifecycleTest muteStopsAnActiveFrozenVoice unfreezeStopsAnActiveFrozenVoice replacingTheTakeStopsAnActiveFrozenVoice` (reverted) | 3 | `test-3-red-all.log` ends `EXIT=3`; `Totals: 2 passed, 3 failed` |
| build-4-green | rebuild after re-applying `fix.patch` | 0 | `build-4-green.log` ends `Built target FrozenTakeVoiceLifecycleTest` |
| per-slot GREEN | `./FrozenTakeVoiceLifecycleTest <slot>` (restored), each of the 3 slots | 0 each | `test-3-green-mute.log`, `test-3-green-unfreeze.log`, `test-3-green-replace.log` each end `EXIT=0` (`Totals: 3 passed, 0 failed`) |
| combined GREEN | same three slots, restored | 0 | `test-3-green-all.log` ends `EXIT=0`; `Totals: 5 passed, 0 failed` |
| ctest-2 | `ctest` over the 7 freeze/render/control tests | 0 | `ctest-2.log`: `100% tests passed out of 7`, `#161 FrozenTakeVoiceLifecycleTest ... Passed` |
| feature-check | `feature_check.py` | 0 | `feature-check-1.log`: `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, gates all exit 0) |

## Realtime safety

No render-path allocation, lock, wait, logging or Qt call was added. The render gate is
one acquire atomic load plus an existing buffer zero and an early return inside
`SamplePlayHandle::play()`; the generation is bumped only from control-thread paths
(the mute model signal, `freezeTo`, `clearFrozenTake`). The handle stamp is a plain
`std::uint64_t` written at arm time before the handle is published.

## Registration

- `tests/fork-sources.txt`: `tests/src/core/FrozenTakeVoiceLifecycleTest.cpp` (position
  3005). Gate: `fork-sources-gate-1.log` PASS — 2744 fork entries, 1213 all-sources,
  49 tooling, 0 stale.
- `tests/all-sources.txt`: same file in recipe order (position 3006);
  `all-sources-1.log` REPRODUCES.
- `tests/upstream-modifications.txt`: cluster new-p12-285 entries for `src/core/Track.cpp`,
  `include/Track.h`, `src/core/SamplePlayHandle.cpp`, `include/SamplePlayHandle.h` and
  `tests/CMakeLists.txt`.
- `tests/CMakeLists.txt`: one line registering the new test in `LMMS_TESTS` (see diff below).

## Other findings

`ControlFreezeCommandsTranscript` is pre-existing and unrelated; it is not changed here.

- The pre-existing `ControlFreezeCommandsTranscript` `add_test`/`set_tests_properties`
  block in `tests/CMakeLists.txt` sets `ENVIRONMENT` and `TIMEOUT 900` but lacks
  `SKIP_RETURN_CODE 77`, while the sibling `ControlFreezeTracksTranscript` block sets
  `SKIP_RETURN_CODE 77`.
- The fixture prints `PASS: no audible instrument in this build: Skipped, never Passed`
  and exits 77 (`control-freeze-direct.log`), but without the property ctest records it
  as a failure: `ctest-preexisting-freeze.log` — test #849 `***Failed`, `0% tests passed,
  1 tests failed out of 1`, EXIT=8.
- `cmake-diff.log` shows the only change to `tests/CMakeLists.txt` in this cluster is the
  single added `src/core/FrozenTakeVoiceLifecycleTest.cpp` line; the unrelated gate was
  left alone.

## Self-check

1. **Regression test fails with the product change reverted, passes with it.** Reverting
   the product patch (four files) and rebuilding, the three slots fail with exit 3 combined
   (`test-3-red-all.log` ends `EXIT=3`, `Totals: 2 passed, 3 failed`; each measured
   0.356029) and exit 1 individually (`test-3-red-mute.log`,
   `test-3-red-unfreeze.log` and `test-3-red-replace.log` each end `EXIT=1`). Restored,
   they pass with exit 0 combined (`test-3-green-all.log` ends `EXIT=0`) and 0
   individually (`test-3-green-mute.log`, `test-3-green-unfreeze.log` and
   `test-3-green-replace.log` each end `EXIT=0`).
2. **Deterministic, no wall-clock timing, sleeps or races.** `setDeterministicProcessing(true)`,
   the device stopped, explicit `renderNextPeriod()` calls, fixed 440/660 Hz fixtures, a
   fixed one-period lag discard, and numeric RMS thresholds; only `qWait(130)` in
   teardown is timing-related and no assertion depends on it.
3. **Fix is at the cause and complete.** All take-state transitions bump the generation
   (mute at `Track.cpp:363-372`, replacement at `Track.cpp:1409-1412`, clear at
   `Track.cpp:1445-1448`); arm captures it first (`Track.cpp:1542-1547`) and stamps the
   handle (`Track.cpp:1630`); the slot is reset on recycle and seeded on arm
   (`SamplePlayHandle.cpp:489-496`, `512`). Seek is intentionally not a bump; the
   voice-pool design keeps older passes across a loop wrap. Callers of
   `playFrozenTake` need no change because the stamp is applied inside it.
4. **No collateral damage.** No serialized format changed (the generation is a runtime
   atomic defaulting to 0), so old projects and saved data load as before. The gate is
   restricted to `isFrozenTakePoolVoice()`, so ordinary and other pooled sample voices
   are unaffected; no limit or check was added that another caller could exceed.
5. **ALREADY-FIXED / FALSE-POSITIVE not applicable.** The verdict is FIXED: the defect
   reproduced in three deterministic RED slots against HEAD and was removed by the
   product diff above, so neither a pre-existing-fix nor a false-positive claim is made.
6. **Sibling commands in changed command files.** This cluster changed no command or
   command-registry file; its change set is the frozen-take sources, the new test and
   the registration lines listed above. The command ids the checker names (`app.*`,
   `arrangement.*`) come from the wider base..HEAD diff, not from this fix, so no
   sibling command is affected.
