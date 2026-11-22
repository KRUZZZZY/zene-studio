FIXED

# cluster new-p16-231: Automation scratch overflow can allocate on the render thread

Round 2. It addresses Luna's round-1 review ("tests: no test added or changed"). Round 1 had reported
NEEDS-CONTEXT. This round fixes the bug without changing the existing contract test.

## Verdict: REAL
In `src/core/Song.cpp`, `Song::processAutomations` (before this fix, lines 737-772) handled a read that overflowed
the reserved `m_automationLevel`, `m_automationInner` or `m_automationRecordClips` scratch by redoing it into the
local unbounded `spillValues`, `spillInner` and `spillClips`. That redo allocates, and it ran on the audio thread too.
The RED run below measured 86 heap allocations inside a `RealtimeScope` over 4 periods.

## Fix
- `src/core/Song.cpp` `Song::holdOverflowingAutomationTick()` (new). After the bounded read and the bounded
  recording-clip read, it checks for overflow. The check covers all three scratches, plus whether
  `m_oldAutomatedValues` or `m_recordedModels` is too small for this tick, since those two used to grow during the
  apply pass. If the tick is on the render path (`RealtimeScope::isActive()` or `AudioEngine::isRenderThread()`), it
  counts the overflow in `m_automationScratchOverflows`, sets `m_automationScratchShort`, and the caller returns.
  The tick is held: no value is applied and no cached model is handed back to its controller, so nothing is
  dropped. It does nothing but atomics and integer comparisons: no allocation, lock or logging.
- `Song::growAutomationScratchIfShort()` (new, control thread) runs from the existing 10 ms
  `automationWriteDrainTimer`, which is connected in the `Song` constructor. When the flag is set it calls
  `ensureAutomationScratchCapacity()`. That function recounts exactly and reserves 2x under the engine change
  lock, so it never races the render thread. If it runs out of memory, the flag is set again and the next drain
  retries.
- Off the render path (offline or test-thread calls), the old allocating redo is unchanged. Allocation is allowed
  there, and the existing contract test `anOverflowingReadIsRedoneAndStillAppliesEveryClip` still passes untouched.
- `include/Song.h`: the declarations, the new flag, and updated comments on `noteAutomationGrowth` and
  `automationScratchOverflows`.
- The finding's other ask, "ensure sufficient capacity before playback", was already met by every edit path through
  `noteAutomationGrowth` / `ensureAutomationScratchCapacity` (commit fb89c74f47, tests
  `targetsAddedToAnExistingClipPastTheCapacityAreAllApplied` and siblings). This fix covers the remaining last-resort
  path.

Trade-off: when an overflow does happen on the render path, automation holds its previous values for the ticks
until the next drain, at most about 10 ms plus the time the lock takes. It used to be applied on time at the cost
of an allocation. An overflow should only happen after an edit made on the render thread or a failed reserve, so
this path is a last resort.

## Regression test
`tests/src/core/AutomationRenderAllocationTest.cpp`
`anOverflowOnTheRenderPathAllocatesNothingAndTheControlThreadGrowsTheScratch`:
- forces capacity 8 for 200 clips with the `resizeAutomationScratchForTest` seam;
- plays 4 periods inside a `RealtimeScope` and requires an overflow to be counted with 0 allocations;
- fires the song's `automationWriteDrainTimer` once by hand (`QMetaObject::invokeMethod(..., "timeout")`, no
  wall-clock wait);
- then requires 6 more realtime periods with 0 allocations, every model to get its value, and no further overflow.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-231.r2/logs)
- RED, test only: `cmake --build build --target AutomationRenderAllocationTest -j4` EXIT=0 (01-build-red.log).
  `ctest -R '^AutomationRenderAllocationTest$' --output-on-failure` EXIT=8 (02-test-red.log):
  `FAIL! ... anOverflowOnTheRenderPathAllocatesNothingAndTheControlThreadGrowsTheScratch() ... Actual (overflowAllocations): 86`.
- GREEN: `cmake --build build --target zene AutomationRenderAllocationTest -j4` EXIT=0 (03-build-green.log).
  ctest EXIT=0, "100% tests passed out of 1" (04-test-green.log).
- Full build `cmake --build build -j4`: refused by the guard ("full builds are the parent's job",
  05-build-all.log). Instead I built zene and the 19 test targets of the family:
  `cmake --build build -j4 --target zene AutomationRenderAllocationTest AudioAutomationToolsTest ... SampleAccurateAutomationTest`
  BUILD_EXIT=0 (06-build-family.log). `ctest -R 'Automation|^Song|MixerConcurrency' --output-on-failure` TEST_EXIT=0,
  "100% tests passed out of 21" (07-test-family.log).
- Step 4b:
  `git diff -- include/Song.h src/core/Song.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` REVERT_EXIT=0.
  Rebuild EXIT=0. ctest EXIT=8 (09-test-reverted.log, "Actual (overflowAllocations): 86").
  `git apply .../logs/fix.patch` APPLY_EXIT=0. Rebuild EXIT=0. ctest EXIT=0 (11-test-reapplied.log, "100% tests passed out of 1").
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` EXIT=0, all PASS
  (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures,
  complexity "no new item in your files", file-length "no new item in your files"), in 12-feature-check.log.

## Self-check
- Fails without the fix, passes with it: step 4b above, red EXIT=8 and green EXIT=0.
- Deterministic: no sleeps or timing. It uses the `resizeAutomationScratchForTest` seam, the allocation probe
  (`test::tlCountAllocations`), and fires the drain timer's `timeout` synchronously with `Qt::DirectConnection`.
- Fix at the cause, siblings checked: all four former overflow sites are covered by the single check. Values and
  inner were redone; recording clips were redone; `m_oldAutomatedValues` and `m_recordedModels` grew. The last two
  can no longer grow on the render path, because the hold check makes sure their capacity covers this tick's values
  and clips (recorded models are at most the clips). The control-path redo is kept as before. The only caller of
  `processAutomations` is `processNextBuffer` (`Song.cpp:650`).
- No collateral damage: no file format, save or load change. The off-render path behaves exactly as before. No
  limit was added. No control command was added.
- Realtime: the new render-path code does relaxed atomic stores and integer comparisons only. Growth happens on the
  control thread under `requestChangesGuard`. Local TSan does not run here. The design argument is that the flag is
  an atomic, and the scratch is resized only with the render thread locked out, as `ensureAutomationScratchCapacity`
  already did.
- ALREADY-FIXED / FALSE-POSITIVE reference: not applicable (FIXED).
- Sibling commands (app.*, arrangement.*), as listed by submit_check: those test files come from earlier commits on
  this branch. This fix changes no command file and adds no command.
- GUI: no src/gui change. Transport, recording and undo state are not changed by this fix: a held tick only skips
  that tick's automation work.

## Other findings
- If a realtime overflow happened with no Qt event loop running on the song's thread (for example a headless
  export with no loop), the scratch would not be grown and automation would stay held until an edit or stop.
  I found no such configuration in this session, and did not verify that one exists.
