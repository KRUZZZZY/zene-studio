FIXED

Cluster new-p10-227-port: LocklessList dereferences a null allocation when full.

Evidence:
- include/LocklessList.h:65-86: `push()` called `m_allocator->alloc()` and dereferenced the result in `std::construct_at` without a null check. `LocklessAllocator::alloc()` (src/core/LocklessAllocator.cpp:110-124) returns `nullptr` once capacity is exhausted.
- Fixed at cause: `push()` now returns `bool`, returns `false` on allocation failure before any dereference, and `true` after a successful link. Callers updated:
  - src/core/AudioEngine.cpp: `AudioEngine::addPlayHandle()` checks the result and only publishes to the bus on success; an exhausted staging list now falls through to `disposePlayHandle()` (or returns `false` for `InputMonitorHandle`, whose caller releases it).
  - src/tracks/SampleTrack.cpp: `SampleTrack::monitorModeChanged()` deletes the monitor handle and clears `m_monitorHandle` when registration fails.
- src/core/LocklessAllocator.cpp: exhausted `alloc()` now returns `nullptr` silently (no `fprintf` on the realtime path).

Regression: tests/src/core/AudioEngineTeardownTest.cpp::exhaustedListReportsFailureWithoutLosingEntries (LocklessList<int>, capacity 32, pushes 33, expects the 33rd to fail, then reuses freed slots).

Verification:
- RED (reverted product files): build exit 0; ctest exit 8 (SIGSEGV on null dereference).
- GREEN: build exit 0; ctest exit 0 (1/1 passed).
- Reverse-patch proof: fix.patch applied -R, rebuild exit 0, test exit 8 (FAIL); restored, rebuild exit 0, test exit 0 (PASS).
- Affected family: 1/1 test executed.
- Gates: python3 feature_check.py exit 0 (all rows PASS).

## Self-check
- The regression test FAILS with the product change reverted and PASSES with it (exit codes 8 and 0).
- The test is deterministic: no sleeps, timers or races; capacity is fixed at compile time (sizeof(int)*8 = 32) and reuse is exercised directly.
- Every caller and sibling checked: `AudioEngine::addPlayHandle`, `SampleTrack::monitorModeChanged`, `SampleTrack::play` (SamplePlayHandle path disposes on failure), `removePlayHandlesOfTypes` sweeps `m_newPlayHandles`; no other `LocklessList::push` callers exist.
- No collateral: old projects unaffected; the capacity check only rejects when the fixed staging list is full, matching the existing disposal path; no new allocation/free/log on the audio thread beyond the pre-existing allocator behavior, and the exhaustion log was removed.
- Verdict evidence: include/LocklessList.h:67 dereferenced `m_allocator->alloc()` without a null check; LocklessAllocator::alloc() documents nullptr on exhaustion.
