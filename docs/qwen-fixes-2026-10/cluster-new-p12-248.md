NEEDS-CONTEXT

# cluster new-p12-248 — Sample accurate automation locks on the audio thread

new-p12-248: NEEDS-CONTEXT

## Finding
`Song::buildAutomationRamps()` runs once per audio block and takes each
opt-in clip's `m_clipMutex`. A control-thread edit holding that lock can make
the audio thread wait. The proposed fix is to publish preallocated, immutable
automation data that the audio thread reads without locking.

## What the code shows (seen this session, saved in logs/01-evidence.log)
- The lock is real and on the audio thread:
  `Song::processNextBuffer` (src/core/Song.cpp:436) → `buildAutomationRamps` (src/core/Song.cpp:582) →
  `AutomationClip::writeBlockRamp` takes `QMutexLocker m(&m_clipMutex)`
  (src/core/AutomationClip.cpp:1073-1077). `m_clipMutex` is a
  `QRecursiveMutex` (include/AutomationClip.h:443).
- The lock is the documented and allowlisted contract, not an accident:
  - include/Song.h:219-220: "Realtime-safe: no allocation, no growth, one lock per clip (the clip's own)".
  - docs/SAMPLE-ACCURATE-AUTOMATION.md:31: "`AutomationClip::writeBlockRamp()` — builds the ramp for one block, under the clip's own lock".
  - tests/rt-safety-scope.txt:173: "... no allocation and no lock beyond the clip's own existing one".
  - docs/RELEASE-NOTES-v0.3.0-alpha.md:2885: the ramp takes the clip lock "once per block (rather than once per knot, as the per-tick path does)".
- The ramp path is not the only one that does this. The per-tick automation
  read, inherited from upstream LMMS, takes the same lock:
  `Song::processAutomations` (src/core/Song.cpp:650/743) →
  `TrackContainer::collectAutomatedValues` (src/core/TrackContainer.cpp:583) →
  `AutomationClip::valueAt` → `QMutexLocker m(&m_clipMutex)`
  (src/core/AutomationClip.cpp:862-864, and again at 918-920).
  tests/rt-safety-scope.txt:174 records this: "the clip lock stays in valueAt()".
- `AutomationClip.cpp` has 28 `QMutexLocker m(&m_clipMutex)` sites. Every
  mutator (put/remove/drag/record/tangents/curve/load/objects) uses the same
  `QMap` timeMap that both audio-thread readers walk under that lock.

## Why this is NEEDS-CONTEXT and not a fix
The issue is real, but the requested fix redesigns how `AutomationClip`
handles concurrency. It is not a local bug fix:
1. **To be complete, the fix must cover both readers.** Making only
   `buildAutomationRamps` lock-free still leaves `processAutomations` /
   `valueAt` waiting on the same mutex in the same audio period. That would
   not fix the cause (definition-of-done item 3).
2. **The published data has no agreed design yet.** The timeMap is a `QMap`
   (implicitly shared, allocates on change). An immutable snapshot needs
   decisions only the owners can make:
   - a fixed node capacity, or a bound per clip (and what to do with clips that exceed it);
   - where retired snapshots are freed (off the audio thread: a deferred-free queue or epoch);
   - how all 28 mutator sites publish a new snapshot;
   - what happens to undo and journal coalescing during drags and recording.
   Choosing a capacity would also reject projects that legitimately exceed it
   (item 4).
3. **The cheap alternative changes the sound.** With `tryLock` and a skip when
   the lock is taken, the audio thread would not wait. But that block would
   play a stale ramp or no automation value, which is an audible behaviour
   change. Whether a stale ramp is better than a wait is a product decision,
   and the contract above says the wait is accepted today.

Decision needed from the owners: should automation reads become lock-free
through published immutable snapshots, covering both the per-block ramp and
the per-tick `valueAt` path? If so, what node capacity and reclamation scheme,
and is a `tryLock` skip with a stale ramp acceptable as an interim step?
Until then no product code was changed, and no regression test was written
(no fix exists for it to guard).

## Commands run (EXIT codes copied from the session)
- Evidence capture (rg/sed into logs/01-evidence.log): EXIT=0
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p12-248 > logs/02-submit-check-pre.log 2>&1`: EXIT=1 (report missing, before writing this file)
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/03-feature-check.log 2>&1`: EXIT=0, `RESULT: PASS`, 0 FAIL lines

Log folder: /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-248.r1/logs

## Self-check
- **Regression test FAILS when reverted, PASSES with the fix:** not applicable. No product change was made (NEEDS-CONTEXT), so there is nothing to revert. The tests that submit_check lists (tests/control-*.py) come from earlier branch history and are not part of this job.
- **Deterministic test:** not applicable; no test was added. A future fix could prove itself deterministically with a seam: hold `m_clipMutex` on a helper thread that is released only by a latch, then assert that `buildAutomationRamps` returns. With no sleeps involved, the test fails by hanging until a ctest timeout if the read still locks.
- **Fix at the cause and complete; callers and siblings checked:** both audio-thread readers of the clip lock were checked: `buildAutomationRamps`→`writeBlockRamp` (AutomationClip.cpp:1077) and `processAutomations`→`collectAutomatedValues`→`valueAt` (AutomationClip.cpp:864, 920). A complete fix has to cover both, which is the main reason for NEEDS-CONTEXT.
- **No collateral damage:** no product code, test, gate or list file changed. Only this report was added. feature_check: EXIT=0.
- **ALREADY-FIXED / FALSE-POSITIVE code reference:** not claimed. The lock is real (AutomationClip.cpp:1077), and it is the documented contract (Song.h:219-220, docs/SAMPLE-ACCURATE-AUTOMATION.md:31, tests/rt-safety-scope.txt:173-174).
- **Sibling commands in command files changed (app.*, arrangement.*):** not affected. This job changed no command file. The listed commands come from earlier branch history, and none of them touch automation clips or their lock.

## Other findings
- The per-tick automation read path (`AutomationClip::valueAt`, src/core/AutomationClip.cpp:864/920) takes the clip mutex on the audio thread too. It is inherited from upstream LMMS and has the same realtime exposure as this finding.
