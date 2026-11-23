FIXED

5418: FIXED — REAL. `Track::playFrozenTake()` armed every pooled voice with the whole take file
(`src/core/Track.cpp:1590`, formerly `const qint64 sourceOut = static_cast<qint64>(m_frozenBuffer->size());`),
while the take's own window is `[FrozenTake::startTicks, FrozenTake::endTicks)` (`include/Track.h:218-219`:
`endTicks` is "where it ends (0 = to its own end)"). The `pastTheEnd` check at `src/core/Track.cpp:1549-1557`
refused only a pass that *started* at or after `endTicks`; a pass that started before it played to the end of
the file. So a take whose audio file runs past its declared end played past that end. The fix crops the pass's
`sourceOut` at the source frame the same tempo-map-aware mapping gives for `endTicks`
(`src/core/Track.cpp:1591-1598`), clamped to the buffer, and leaves `endTicks <= startTicks` ("to its own end")
reading the whole buffer, the same condition `pastTheEnd` and `frozenTakeSilencesSource()` already use.

Both callers go through the one function and are unchanged: `src/tracks/SampleTrack.cpp:158` and
`src/tracks/InstrumentTrack.cpp:945`. No header changed; no new function, command, allocation, lock or log on
the render path (the crop is one extra call of `frozenTakeSourceFrame()`, which this path already calls for
`sourceIn`).

Regression test: `tests/src/core/FrozenTakeRoundTwoTest.cpp:497`
`frozenPassStopsAtTheTakesDeclaredEndTick` — a 2-second take (88200 frames, the length the RED run printed)
frozen with `endTicks = 48` must render the frame the mapping gives for tick 48, not the whole file (asserted:
the pass length is below the file length **and** equal to `Track::frozenTakeSourceFrame(...endTicks...)`, whose
fixture check also requires it to be under half the file), and a second pass under an active tempo map with a
tempo event inside the window must stop at the mapped end and not at the one-rate end (the two are asserted to
be more than 1000 frames apart before the pass is played, and the pass is then matched to the mapped one within
a frame). No sleeps, no wall clock:
the engine is driven period by period with `AudioEngineWorkerThread::setDeterministicProcessing(true)` and
`renderNextPeriod()`.

Collateral: `tests/src/core/FreezeRegionRepairTest.cpp` derived a pass's *start* frame as
`fileSize - totalFrames()`, an identity that only held while the pass ran to the end of the file — it encoded
the defect. Both sites now compare the pass's length to the render's own remaining length at that tick
(`playbackFrames(position) == renderLength - renderOffset(position)`, and
`min(fileFrames, mappedEnd) - mappedStart` in the pin-exhaustion case): the same information, one frame either
side, with no tolerance change on the family.

Commands and exit codes (all logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/5418.r1/logs`):

| Step | Command | EXIT | Log |
|---|---|---|---|
| RED (no fix) | `cmake --build build --target FrozenTakeRoundTwoTest -j4` | 0 | build-1.log |
| RED (no fix) | `ctest -R FrozenTakeRoundTwoTest --output-on-failure` (from `build/tests`) | 8 | test-1-red.log |
| GREEN | `cmake --build build --target zene FrozenTakeRoundTwoTest -j4` | 0 | build-2.log |
| GREEN | `ctest -R FrozenTakeRoundTwoTest --output-on-failure` | 0 | test-2-green.log |
| 4b revert | `git diff -- src/core/Track.cpp > .../fix.patch` | 0 | fix.patch (21 lines) |
| 4b revert | `git apply -R .../fix.patch` | 0 | — |
| 4b RED | `cmake --build build --target zene FrozenTakeRoundTwoTest -j4` | 0 | build-7-red-revert.log |
| 4b RED | `ctest -R FrozenTakeRoundTwoTest --output-on-failure` | 8 | test-7-red-revert.log |
| 4b restore | `git apply .../fix.patch` | 0 | — |
| 4b GREEN | `cmake --build build --target zene FrozenTakeRoundTwoTest FreezeRegionRepairTest -j4` | 0 | build-8-green-reapply.log |
| 4b GREEN | `ctest -R "FrozenTakeRoundTwoTest\|FreezeRegionRepairTest" --output-on-failure` | 0 | test-8-green-reapply.log |
| Family | `ctest -R "FrozenTakeHandoffTest\|FrozenTakeRoundTwoTest\|FreezeRegionRepairTest\|MixerBusFreezeTest\|BatchFreezeTest\|AutomationRenderAllocationTest\|ClipPolarityTest\|LoopImportWarpTest\|TakeLaneCompPlaybackTest\|SampleClipWindowTest\|ControlVerbInverseTest\|InstrumentTrackRenderPathTest\|BounceInPlaceTest\|ControlBounceRenderTest" --output-on-failure` | 0 (14/14) | test-6-family.log |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (RESULT: PASS) | feature-check-1.log |

Red output, verbatim (test-7-red-revert.log:17):

```
FAIL!  : FrozenTakeRoundTwoTest::frozenPassStopsAtTheTakesDeclaredEndTick() 'static_cast<qint64>(lengths.front()) < fileFrames' returned FALSE. (the pass ran 88200 frames, past the declared end of a 88200-frame file)
```

Green output (test-8-green-reapply.log:3-7):

```
1/2 Test #163: FrozenTakeRoundTwoTest ...........   Passed    6.23 sec
2/2 Test #164: FreezeRegionRepairTest ...........   Passed    1.53 sec
100% tests passed out of 2
```

An intermediate run (test-3-family.log) is also in the folder: with the fix but before the
`FreezeRegionRepairTest` sites were made consistent, `takeFramesMatchTheRenderedRegionAtBothEnds` failed
(`playbackFrame(regionStart)` actual 22050, expected 0) — the evidence that the old assertion encoded the
defect rather than a contract.

`tests/upstream-modifications.txt` carries the ledger line for the inherited file; both test files are
fork-NEW and already registered in `tests/fork-sources.txt` (lines 2820, 2822).

## Self-check

- Verdict is backed by code read this session: `src/core/Track.cpp:1535-1625` (the whole function),
  `include/Track.h:215-237` (`FrozenTake`, `endTicks` contract), `src/tracks/SampleTrack.cpp:150-165`,
  `src/tracks/InstrumentTrack.cpp:935-950`, `src/core/AudioEngine.cpp:267-276` and
  `src/core/SamplePlayHandle.cpp:475-496` (`armPooledVoice` → `m_window = {sourceIn, sourceOut}`), which is
  what makes the crop observable as `totalFrames()`.
- RED/GREEN is not claimed from memory: the patch was reversed and re-applied with `git apply`, each step's
  exit code and log are listed above, and the failing assertion text is quoted from test-7-red-revert.log.
- Determinism: the test renders fixed periods with deterministic processing, injects the tempo map through
  `TempoMapPublisher::edit`, and asserts frame counts, not time. `QTest::qWait(130)` only lets the
  control-thread recycler retire pooled slots, as every test in this file already does; no assertion depends
  on it.
- Cause and siblings: the crop is in the one function both `SampleTrack::play` and `InstrumentTrack::play`
  reach; `frozenTakeSilencesSource()`, `updateFrozenVoiceCapacity()` and `loadFrozenTake()` read the same
  `endTicks > startTicks` condition and are untouched, so a whole-track take still silences its source
  everywhere and capacity arithmetic is unchanged.
- No collateral damage: a take with no declared end (`endTicks <= startTicks`) or a declared end past its file
  still plays the whole buffer — the `min()` clamp — which is why `BatchFreezeTest`,
  `AutomationRenderAllocationTest`, `MixerBusFreezeTest`, `TakeLaneCompPlaybackTest` and the bounce/render tests
  in the family all still pass. No header, command, undo path or GUI file changed.
- Realtime: the added work on the render path is one call to the existing `frozenTakeSourceFrame()` and a
  `std::min` on `qint64`; no allocation, lock, wait or logging, no Qt call.

The six items `submit_check.py` asks for, one line each:

1. **Fails reverted, passes with the fix.** `git apply -R .../logs/fix.patch` exit 0 → build exit 0 →
   `ctest -R FrozenTakeRoundTwoTest` **exit 8** (test-7-red-revert.log:17). `git apply .../logs/fix.patch`
   exit 0 → build exit 0 → same ctest **exit 0** (test-8-green-reapply.log). Same binary, same command, same
   test slot.
2. **Deterministic.** No wall clock, no sleep in any assertion, no race: `initTestCase()` calls
   `audioDev()->stopProcessing()` and `AudioEngineWorkerThread::setDeterministicProcessing(true)`, so periods
   are rendered only by explicit `renderNextPeriod()` calls on the test thread. The two injected values are the
   take window (`freezeTo(wav, 0, 48, ...)`) and the tempo map (`TempoMapPublisher::edit` adding tempo 60 at
   tick 24); the assertions compare frame counts from the armed window
   (`SamplePlayHandle::totalFrames()`), not elapsed time. `QTest::qWait(130)` appears only to let the
   control-thread recycler retire pooled slots between passes — no assertion reads anything it produces, and
   the RED run failed with a deterministic frame count (88200) on the first assertion.
3. **Cause and siblings.** Changed: `Track::playFrozenTake()` (`src/core/Track.cpp:1591-1598`). Its only
   callers, `SampleTrack::play` (`src/tracks/SampleTrack.cpp:158`) and `InstrumentTrack::play`
   (`src/tracks/InstrumentTrack.cpp:945`), are unchanged and take the same `bool` result: a cropped pass that
   cannot start returns false, and `frozenTakeSilencesSource()` — which already owns the "does the take own
   this tick" question — decides whether the source plays instead. `frozenTakeSourceFrame()` is reused, not
   re-derived, so the crop uses exactly the mapping the render cut the take with. The two existing
   `FreezeRegionRepairTest` assertions that encoded the old "runs to the end of the file" identity were updated
   to assert the same fact through the pass's length against the render's own remaining length; no gate,
   baseline or limit was touched.
4. **No collateral damage.** Save and load are untouched (`saveTrack` writes `frozenAudio/frozenStart/
   frozenEnd/frozenMuted/frozenRegion`, `loadFrozenTake()` parses them): no format change, so an existing
   project with a frozen take loads with the same take and the same window. The new limit is a `min()` against
   the buffer, applied only when `endTicks > startTicks`, so a take with no declared end, or one whose declared
   end maps past its file, arms the same `sourceOut` as before — the existing family run passes 14/14
   (test-6-family.log), including `BatchFreezeTest`, `AutomationRenderAllocationTest` (45 s of render-path
   allocation probes), `MixerBusFreezeTest`, `ControlVerbInverseTest` and the two take-lane tests.
5. **ALREADY-FIXED / FALSE-POSITIVE.** Not the verdict here: the verdict is FIXED, and the code that showed the
   defect is `src/core/Track.cpp:1590` before the change (`sourceOut` = the whole buffer) against the
   `FrozenTake::endTicks` contract in `include/Track.h:218-219` and the `pastTheEnd` guard at
   `src/core/Track.cpp:1549-1557`, which refused only a pass that started past the end.
6. **Sibling commands.** No command file was changed and no new control command was added, so no command's
   sibling or inverse is affected and the `ControlCommandsSnapshot` canary does not move: the change is inside
   one render-path function, `freeze.*`/`render.*` behave exactly as before (they call `freezeTo()`, whose
   signature and semantics are untouched).
