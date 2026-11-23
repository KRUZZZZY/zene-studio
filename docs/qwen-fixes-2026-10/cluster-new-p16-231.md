FIXED

# cluster new-p16-231: Automation scratch overflow can allocate on the render thread

Round 3. It adds the GUI-harness screenshot the round-3 review asked for, on top of round 2's state-sequence and
round 1's fix. Round 1 had reported NEEDS-CONTEXT. This round fixes the bug without changing the existing contract
test.

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
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-231.r4/logs`):
- `git diff d6bbc50a41^ d6bbc50a41 -- include/Song.h src/core/Song.cpp > .../logs/fix.patch` DIFF_EXIT=0
  (144 lines); `git apply -R .../logs/fix.patch` REVERT_EXIT=0.
- RED: `cmake --build build --target zene AutomationRenderAllocationTest -j4` BUILD_EXIT=0 (04-build-red.log);
  `ctest -R '^AutomationRenderAllocationTest$' --output-on-failure` TEST_EXIT=8 (05-test-red.log):
  `FAIL! ... anOverflowOnTheRenderPathAllocatesNothingAndTheControlThreadGrowsTheScratch()`,
  `Actual (overflowAllocations): 86`, `Loc: [../../tests/src/core/AutomationRenderAllocationTest.cpp(370)]`.
- `git apply .../logs/fix.patch` APPLY_EXIT=0;
  `cmake --build build --target zene AutomationRenderAllocationTest -j4` BUILD_EXIT=0 (06-build-green.log);
  `ctest -R '^AutomationRenderAllocationTest$' --output-on-failure` TEST_EXIT=0 (07-test-green.log,
  "100% tests passed out of 1").

## Round 3 (proof manifest needs a screenshot)
The round-3 review asked for a `screenshot-or-recording` artifact from the GUI harness. The fix changes no GUI
file, so this proves the product still renders and is driven through the control socket. I ran the reviewed shell
driver, which starts the real `zene` binary headless and calls `window.screenshot` over the socket:
- command: `cd tests && QT_QPA_PLATFORM=offscreen python3 control-shell.py ../build/zene
  /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-231.r4/logs/gui` EXIT=0 (01-gui-harness.log,
  "PASS: shell control-surface driver (every check held)").
- artifacts (each 1600 x 1000 PNG, rendered by `window.screenshot`):
  - `logs/gui/shell-arrange.png` sha256 `3f9233ec8840a80207d4511e6186cf43315252ba967c5aa8d2489e75d730e618`
  - `logs/gui/shell-mix.png` sha256 `cb364b7ae380aa715ff2cffcee36e2def2ecb00282065e65c4b55e0783662e31`
  - `logs/gui/shell-edit.png` sha256 `82526510e3f96a5a41439356dcb28b3455671a2222e472d6cacf5d896e9ed3ab`
- `logs/proof.json` carries the `screenshot` artifact (`shell-arrange.png`, its sha256 above) next to the
  `regression-test` and `state-sequence` artifacts.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-231.r4/logs)
- Green build/run with the fix in place, before the fresh RED: `cmake --build build --target zene
  AutomationRenderAllocationTest -j4` BUILD_EXIT=0 (02-build-green.log). `ctest -R
  '^AutomationRenderAllocationTest$' --output-on-failure` TEST_EXIT=0, "100% tests passed out of 1"
  (03-test-green.log).
- The fresh RED/GREEN of step 4b are the six numbered lines in the Round 2 section above: RED TEST_EXIT=8,
  GREEN TEST_EXIT=0.
- Family: `ctest -R 'Automation|^Song|MixerConcurrency' --output-on-failure` TEST_EXIT=0, "100% tests passed
  out of 21" (08-test-family.log).
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` EXIT=0, all PASS
  (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures,
  complexity "no new item in your files", file-length "no new item in your files"), in 09-feature-check.log.

## Self-check
- The regression test FAILS with the product change reverted and PASSES with it: fresh round-3 step 4b above —
  RED `ctest ...` TEST_EXIT=8 with `Actual (overflowAllocations): 86`
  (05-test-red.log), GREEN TEST_EXIT=0, "100% tests passed out of 1" (07-test-green.log). The reverts/applies of
  `logs/fix.patch` printed REVERT_EXIT=0 and APPLY_EXIT=0, and both rebuilds BUILD_EXIT=0 (04-build-red.log,
  06-build-green.log). The manifest in `logs/proof.json` records red_exit 8 and green_exit 0.
- Deterministic: no wall-clock timing, sleeps or races. The test uses the `resizeAutomationScratchForTest`
  capacity seam, the `test::tlCountAllocations` allocation probe, and fires the drain timer's `timeout`
  synchronously with `Qt::DirectConnection`. No `QTest::qWait`, sleep or elapsed-time assertion is involved.
- Fix at the cause and complete: all four former overflow sites are covered by one check. Values and inner were
  redone; recording clips were redone; `m_oldAutomatedValues` and `m_recordedModels` grew. The last two can no
  longer grow on the render path, because the hold check makes sure their capacity covers this tick's values and
  clips (recorded models are at most the clips). The control-path redo is kept as before. The only caller of
  `processAutomations` is `processNextBuffer` (`Song.cpp:652`).
- No collateral damage: no file format, save or load change; the off-render path behaves exactly as before; no
  limit was added and no control command was added. `git diff --name-only 200c5b1ff6..HEAD` lists only
  `docs/qwen-fixes-2026-10/cluster-new-p16-231.md`, `include/Song.h`, `src/core/Song.cpp`,
  `tests/src/core/AutomationRenderAllocationTest.cpp` and `tests/upstream-modifications.txt`.
- ALREADY-FIXED / FALSE-POSITIVE: not applicable; the verdict is FIXED.
- Sibling commands (app.about, app.ask, app.help, app.online_help, app.task_cancel, app.tasks, app.tour,
  app.tours, app.update_check_get, app.update_check_run, app.update_check_set, app.version, app.whats_new,
  arrangement.fit_tracks, arrangement.get_state): this fix changes no command file and adds no command. Those
  test files come from earlier commits on this branch, and the diff of this fix (listed above) does not touch
  them, so each stays consistent.
- Realtime: the new render-path code does relaxed atomic stores and integer comparisons only. Growth happens on the
  control thread under `requestChangesGuard`. Local TSan does not run here. The design argument is that the flag is
  an atomic, and the scratch is resized only with the render thread locked out, as `ensureAutomationScratchCapacity`
  already did.
- GUI: no src/gui change. Transport, recording and undo state are not changed by this fix: a held tick only skips
  that tick's automation work. The state sequence the test drives (warm tick, reset, short capacity, four held
  realtime periods, drain, post-drain periods) is recorded in `logs/proof.json` as the `state-sequence` artifact.

## Other findings
- If a realtime overflow happened with no Qt event loop running on the song's thread (for example a headless
  export with no loop), the scratch would not be grown and automation would stay held until an edit or stop.
  I found no such configuration in this session, and did not verify that one exists.
