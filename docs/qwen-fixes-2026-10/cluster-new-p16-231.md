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
- plays one full-scratch warm tick and requires all 200 models to get their automation value;
- resets every model to zero, then forces capacity 8 for the 200 clips with the `resizeAutomationScratchForTest` seam;
- plays 4 periods inside a `RealtimeScope` and requires an overflow to be counted, 0 allocations, and every model
  still at zero (the held tick applied nothing);
- fires the song's `automationWriteDrainTimer` once by hand (`QMetaObject::invokeMethod(..., "timeout")`, no
  wall-clock wait), which grows the scratch on the control thread;
- then requires 6 more realtime periods with 0 allocations, no new overflow, and all 200 models to hold their
  expected values.

## Round 2 (proof manifest needs a state-sequence)
The round-2 test drives these states, in this order (line numbers in
`tests/src/core/AutomationRenderAllocationTest.cpp`):
1. full-scratch warm tick: `song->processNextBuffer()` (line 356) and `project.verifyApplied()` (line 357) —
   every one of the 200 models holds its automation value.
2. models reset to zero: `project.resetModels()` (line 358) — `Fixture::resetModels` (lines 101-105) writes
   `setValue(0.0f, true)`.
3. capacity forced short: `song->resizeAutomationScratchForTest(8)` (line 359) with `overflowsBefore` sampled
   (line 360).
4. four realtime periods: `for (int i = 0; i < 4; ++i) { song->processNextBuffer(); }` inside a `RealtimeScope`
   (lines 364-367); then the overflow count rose (line 369), allocations are zero (line 370), and
   `project.verifyAllZero()` (line 371) proves the held tick applied nothing (helper at lines 118-124).
5. the drain fires: `QMetaObject::invokeMethod(drain, "timeout", Qt::DirectConnection)` (line 376) with
   `overflowsAfterGrowth` sampled (line 378).
6. the post-drain periods: `allocationsOverPeriods(song, 6)` is zero (line 379), `project.verifyApplied()`
   (line 380) and the overflow count did not move (lines 381-382) — all 200 models hold their expected values.

Fresh RED/GREEN with the product patch reversed and restored (logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-231.r3/logs`):
- `git diff HEAD^ HEAD -- include/Song.h src/core/Song.cpp > .../logs/fix.patch` DIFF_EXIT=0 (144 lines);
  `git apply -R .../logs/fix.patch` REVERT_EXIT=0.
- RED: `cmake --build build --target zene AutomationRenderAllocationTest -j4` BUILD_EXIT=0 (03-build-red.log);
  `ctest -R '^AutomationRenderAllocationTest$' --output-on-failure` TEST_EXIT=8 (04-test-red.log):
  `FAIL! ... anOverflowOnTheRenderPathAllocatesNothingAndTheControlThreadGrowsTheScratch()`,
  `Actual (overflowAllocations): 86`, `Loc: [../../tests/src/core/AutomationRenderAllocationTest.cpp(370)]`.
- `git apply .../logs/fix.patch` APPLY_EXIT=0;
  `cmake --build build --target zene AutomationRenderAllocationTest -j4` BUILD_EXIT=0 (05-build-green.log);
  `ctest -R '^AutomationRenderAllocationTest$' --output-on-failure` TEST_EXIT=0 (06-test-green.log,
  "100% tests passed out of 1").

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-231.r3/logs)
- Test-only build with the fix in place (before the fresh RED): `cmake --build build --target
  AutomationRenderAllocationTest -j4` EXIT=0 (01-build-test.log). `ctest -R '^AutomationRenderAllocationTest$'
  --output-on-failure` EXIT=0, "100% tests passed out of 1" (02-test-green.log).
- The fresh RED/GREEN of step 4b are the six numbered lines in the Round 2 section above: RED TEST_EXIT=8,
  GREEN TEST_EXIT=0.
- Family: `ctest -R 'Automation|^Song|MixerConcurrency' --output-on-failure` TEST_EXIT=0, "100% tests passed
  out of 21" (07-test-family.log).
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` EXIT=0, all PASS
  (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures,
  complexity "no new item in your files", file-length "no new item in your files"), in 08-feature-check.log.

## Self-check
- Fails without the fix, passes with it: step 4b above, red EXIT=8 and green EXIT=0.
- Deterministic: no sleeps or timing. It uses the `resizeAutomationScratchForTest` seam, the allocation probe
  (`test::tlCountAllocations`), and fires the drain timer's `timeout` synchronously with `Qt::DirectConnection`.
- Fix at the cause, siblings checked: all four former overflow sites are covered by the single check. Values and
  inner were redone; recording clips were redone; `m_oldAutomatedValues` and `m_recordedModels` grew. The last two
  can no longer grow on the render path, because the hold check makes sure their capacity covers this tick's values
  and clips (recorded models are at most the clips). The control-path redo is kept as before. The only caller of
  `processAutomations` is `processNextBuffer` (`Song.cpp:652`).
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
