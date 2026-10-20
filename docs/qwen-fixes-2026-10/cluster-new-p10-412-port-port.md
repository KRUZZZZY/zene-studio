# Cluster new-p10-412-port-port: tempo map snapshot can spin without a real-time bound

**Verdict: FIXED**

## Evidence

`include/TempoMap.h` `TempoMapPublisher::snapshot()` was a seqlock reader that retried until `m_version` came back even and unchanged. `edit()` bumped `m_version` to odd before running its callback, so a paused writer left the audio-thread `snapshot()` spinning with no bound. Replaced the seqlock with two preallocated published buffers: `snapshot()` pins the current buffer, copies it once, releases the pin (no retry, no allocation, no lock); `edit()` fills the other buffer and publishes it atomically, waiting for earlier readers only on the control thread. The newest line's try/catch rollback is retained: a throwing edit restores `m_map` and publishes nothing to the snapshot buffers, so `snapshot()` still answers the last completed edit.

## Regression

`TempoMapTest::thePublisherSnapshotReturnsWhileAnEditIsPaused()` (tests/src/core/TempoMapTest.cpp) pauses a writer inside `edit()` and checks that a separate reader's `snapshot()` returns the prior map within 250 ms.

## RED / GREEN / reversion proof

- RED (original code + new test): ctest exit 8 (log test-revert.log); assertion `returnedBeforeEdit` FALSE.
- GREEN (fixed): ctest exit 0 (log test-green.log); family 3/3 pass.
- Reversion proof: `git apply -R fix.patch` -> rebuild -> ctest exit 8; restore -> rebuild -> ctest exit 0.

## Gates

- complexity-gate: exit 1, 6 pre-existing regressions, no TempoMap entry.
- file-length-gate: exit 1, 15 pre-existing regressions, no TempoMap entry.
- feature_check.py: PASS.

## Files committed

- include/TempoMap.h
- tests/src/core/TempoMapTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p10-412-port-port.md

## Self-check

- The regression test FAILS with product change reverted and PASSES with it: revert run ctest exit 8 (log test-revert.log), green run ctest exit 0 (log test-green.log); family 3/3 exit 0 (log test-green.log).
- The test is deterministic: it uses std::latch to pause the writer inside edit() and a 250 ms deadline loop yielding; no sleeps/races beyond the latch synchronization. The implementation removes the retry loop entirely, so snapshot() returns in bounded time independent of writer progress.
- The fix is at the cause and complete: TempoMapPublisher::snapshot() no longer spins; edit() publishes to the new buffers and retains try/catch rollback plus publishRealtime() for the realtime path; callers Song/Track use withRealtimeMap (unchanged) and ControlCommandsDawProject uses map()/edit() (unchanged); siblings (m_rtSlots realtime scheme) left intact.
- No collateral damage: no saved data or project format changes; m_map/map() semantics preserved; throwing-edit regression still expects prior map and passes; old projects load unchanged.
