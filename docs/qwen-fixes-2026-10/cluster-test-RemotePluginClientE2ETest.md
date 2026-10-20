FIXED

# test-RemotePluginClientE2ETest: the remote-plugin end-to-end test failed under machine load

## What failed
RemotePluginClientE2ETest failed intermittently whenever the machine was loaded (merge-train bar 050/m0-train-9:
"beyond baseline"; reproduced on the train-9 build at load ~100: `matchingClientAudioLandsAtExactChannelFrameOffsets`
"period 1 was not processed" and `midiSentPerPeriodIsCapped`; also seen: `lateReplyIsDrained...`, `refusedNoteOff...`,
`lostConnectionIsReportedOnTheMainThread`). Not caused by the branches it was blamed on.

## Causes (three) and fixes
1. **The period's reply wait is one audio period (~20 ms, 7/8 used).** The fake client is another process; under load it
   misses that window, the period counts as overrun, and tests about what the client *says* fail for a reason they do not
   test. Fix: a tests-only seam `RemotePlugin::setPeriodBudgetForTesting()` (std::atomic, relaxed load in `periodBudget()`;
   0 = unchanged product behaviour) and a `ScopedReplyBudget` (5 s) in the 12 tests about message content. Tests about the
   deadline itself (late, stalled, mid-message, big-message first period, hung/late queries) keep the real budget.
2. **A 10 ms main-thread timer was expected to fire inside `QTest::qWait(30)`.** Every count report sets a pending flag and
   a period with one pending sends nothing; under load the timer is late. Fix: the tests apply the deferred request
   themselves (`applyDeferredClientRequests()`, the same call on the same main thread the timer makes) and drive periods
   until the report arrives (`runPeriodsUntilTheCountMoves`, 10 s bound) instead of a fixed count - a period whose MIDI
   finds no socket room is skipped by design.
3. **Product bug: the per-period MIDI cap counted per process() call, not per client period.** A period that sent its
   128 events but found no room for IdStartProcessing was skipped; the next period sent another 128 before that same
   request, so the client received 256 events in one period (capper reported 4). Fix: `m_midiSentSinceRequest` counts
   frames (events and cuts) since the last IdStartProcessing; the drain gets only what is left of MidiDrainPerPeriod;
   reset when a request is sent. Plain audio-thread counter: no allocation, lock or wait.

## Regression (deterministic) and proof
New `midiCapHoldsAcrossPeriodsSkippedForRoom`: a tests-only send-room seam (`RemotePluginBase::setSendRoomForTesting`,
Linux SIOCOUTQ; 0 = unchanged) shrinks the room to 2048 bytes so every period runs out part-way. The capper client now
reports 3 for 1-128 note-ons in its period and 4 for more (stricter than before; the existing test's 3/4 expectations hold).

| Run | Command | Result |
|---|---|---|
| RED (cap fix reverted to `budget = MidiDrainPerPeriod`, seams kept) | `./RemotePluginClientE2ETest midiCapHoldsAcrossPeriodsSkippedForRoom` x5 | FAIL 5/5, Actual 4 Expected 3 |
| GREEN | same x5 | PASS 5/5 |
| Whole test under load (load avg 74-95) | `ctest -R '^RemotePluginClientE2ETest$' --repeat until-fail:25` | 25/25 passed, EXIT 0 |
| Whole test again after the new case | same | 25/25 passed, EXIT 0 |
| Before any fix, train-9 build, load ~100 | `ctest ... --repeat until-fail:15` | failed run 1 (EXIT 8) |
| Family | `ctest -R '^RemotePluginAudioPortsTest$'` | PASS, EXIT 0 |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` | RESULT: PASS (all 8 rows) |

Logs: ~/.local/share/zene-bunny-fix/qwen/runs/test-RemotePluginClientE2ETest.manual/

## Assertions
No assertion is weakened or dropped: the per-period `QVERIFY(!plugin.process())` lines of the three refused-note-off /
capper tests moved into `runPeriodsUntilTheCountMoves`, which asserts it for every period it runs; their final count
comparisons are unchanged. Added: the new test's checks.

## Not covered
- Main-thread query tests use `QueryReplyTimeout` (100 ms), not the period budget; none failed in 65 loaded runs, but
  under extreme load a reply later than 100 ms would still fail them. Left unchanged (no evidence).
- The send-room seam is Linux-only (SIOCOUTQ); the new case QSKIPs elsewhere.

## Files
include/RemotePlugin.h, src/core/RemotePlugin.cpp, include/RemotePluginBase.h, src/common/RemotePluginBase.cpp,
tests/src/core/RemotePluginClientE2ETest.cpp, tests/src/plugins/FakeRemotePluginClient.cpp,
tests/upstream-modifications.txt (all four upstream files declared), this report.
