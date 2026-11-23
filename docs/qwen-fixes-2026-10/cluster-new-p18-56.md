FIXED

# Cluster new-p18-56 — MIDI draining repeatedly scans thousands of atomic slots

## Verdict: REAL

`drainMidi()` (`include/MidiHandoffQueue.h:618`) checks all 16 channels on every loop iteration. Each check
calls `MidiChannelOverflow::dueCut()` -> `sharedCut()` (`:426`). In the common case (no refusal pending, or
every cut served) the cached earliest cut is 0 and the old code scanned all 513 pending slots of the channel
with an acquire load each, although the scan could only find nothing: 16 channels x (128 events + 1 final
check) x 513 slots = **1,058,832 atomic loads per 128-event period** on the render thread (measured by the
scan-count regression in round 1, and still 0 with the fix). `ZynAddSubFxInstrument::forwardQueuedMidi()`,
`VestigeInstrument::processImpl()`, `RemotePlugin::sendQueuedMidiEvents()` and Carla's `processPeriod()` all
drain from their render callback.

Round 1 added a fast path that skipped the scan when `m_cutAt == 0`. The round-1 review found the race that
made it wrong, and this round fixes it: a producer raises `m_cutAt`, the consumer's `clear()` resets it, and
the producer publishes its slot only afterwards, pausing before it updates the earliest-cut cache. The slot
is then published with `m_cutAt == 0`, and the old fast path hid it, so a post-refusal note was popped before
the recovery cut. The reproduction in this session showed exactly the reviewed traces:
`E1..E4, CUT3, E5, CUT3` (note before the second cut, consumed=5) versus the baseline
`E1..E4, CUT3, CUT3, CUT3, E5` (consumed=4 in the one-event-budget callback).

## Fix (round 2)

`include/MidiHandoffQueue.h`

- `:563-565` — new per-channel `m_pendingUsed` count: "published slots the consumer has not cleared yet".
  It is declared in every build (the layout of classes embedding `MidiChannelOverflow`, e.g.
  `RemotePlugin.h:385`, must stay identical between test and product TUs).
- `:472-491` — `remember()` now announces occupancy **before** publishing the slot (seq-cst add), publishes
  with the existing compare-exchange, and retracts the announcement if another producer took the slot. A
  published slot is therefore always announced first, so a consumer that reads 0 knows no slot is visible.
- `:325-340`, `:545-560` — `clearCovered()` reports whether it actually cleared a stamp; `clear()` subtracts
  one only for a pending slot it cleared. A cache stamp such as `m_cutAt` is not counted, so a clear cannot
  make the count lie. `refreshEarliest()` still scans after a served cut, as before.
- `:426-447`, `:508-516` — `sharedCut()` and `sharedStamp()` take the fast path only when
  `m_pendingUsed[index] == 0`: two loads instead of a 513-slot scan. The raised `m_cutAt` is still returned
  (with `ordered == false`), so a refusal whose `remember()` failed keeps its deferred-stamp semantics, and
  the pending scan still runs whenever any slot is published.
- `:251-263` — test-only `recordWithPublicationHooks()` calls an `afterRaise` and an `afterRemember` callback
  inside `recordChannel()`, before `lower()` updates the earliest cache. This is the deterministic pause the
  ordering regression needs; the hooks are compiled only with `LMMS_TESTING`.

The count is exact when the system is quiet, conservative while a producer is between the announcement and
the publish (the consumer scans, which is safe), and is decremented only by the single consumer after a
successful slot clear. No allocation, lock, wait or log is added to the render path; the only new render-side
work is one seq-cst load per channel per drain check and one seq-cst subtract per cleared slot.

## Round 3: consumer read order

The review's checkpoint asked for the consumer's read order to be fixed on top of the `m_pendingUsed` accounting.
Before this round `sharedCut()` / `sharedStamp()` read `m_earliestCutAt`, then `m_cutAt`, then `m_pendingUsed`.
A producer that raises `m_cutAt` and publishes its slot *between* the `m_cutAt` read and the `m_pendingUsed`
read left the consumer with a stale `latest == 0` and `used == 0`, so the cut was missed and the post-refusal
note could be popped first (baseline reads the raised stamp last, after the slot scan).

Fix (`include/MidiHandoffQueue.h`, `sharedCut()` and `sharedStamp()`): read `m_earliestCutAt`, then
`m_pendingUsed`, then (only if `used > 0`) `pendingStamp()`, and read `m_cutAt` **last**, so the `used == 0`
branch also returns a fresh raised stamp. Since a producer raises the stamp before it announces or publishes a
slot, a stamp read after the occupancy is at least as fresh as that occupancy. The `ordered`/deferred-stamp
semantics are unchanged (`ordered` is true only when a published slot was found), and with no pending cut the
drain is still two loads per channel (`pendingSlotReads() == 0`).

Test seams (compiled only with `LMMS_TESTING`, no layout change):
- `recordWithPublicationHooks()` now has a third pause, `afterAnnounce`, between `used++` and the slot CAS
  (`remember()`), in addition to `afterRaise` and `afterRemember`.
- `dueCutWithTestHook()` runs a callback between the consumer's occupancy read and its raised-stamp read
  (`sharedCut()` calls it through `dueCutImpl()`; product `dueCut()` passes a no-op).

## Regression tests

- `MidiHandoffQueueTest::drainWithoutCutsDoesNotScanPendingSlots` (kept): `pendingSlotReads() == 0`, 128 events.
- `MidiHandoffQueueTest::publishedCutIsNotHiddenByAClearedStamp` (kept; hook call gains the no-op middle hook):
  trace `E1 E2 E3 E4 CUT3 CUT3 CUT3 E5`, one-event budget spent on the cut.
- `VestigeMidiHandoffTest::aCutIsNotHiddenBeforeItsSlotIsPublished` (new): pause after the occupancy
  announcement and before the slot CAS; a note is queued; the one-event-budget drain must take the cut
  (`consumed() == 4`). This guards the third pause; it also passes on the pre-round-3 order, because the stamp
  was already raised there (reported honestly: it is not the RED test).
- `VestigeMidiHandoffTest::theRaisedStampIsReadAfterTheSlotOccupancy` (new, the RED test): the producer's full
  publication runs inside the consumer's read window; the cut must be due (`effective == 5`). It fails with the
  old order (`effective` 0) and passes with the new one.
- Why in the Vestige target: `MidiHandoffQueueTest.cpp` is at the 500-code-line limit (adding the tests there
  failed the file-length gate: 529) and the lane build tree refuses reconfiguration
  (`cmake build` -> `bunny-guard: configuring is the parent's job`, exit 97, `configure-1.log`), so a new test
  target was not possible. The Vestige test already includes `MidiHandoffQueue.h` and is built with
  `LMMS_TESTING`.

## Test matrix

| Path | Coverage |
| --- | --- |
| Normal, no overflow | scan test: 0 slot probes, 0 freed budget, all events delivered. |
| Paused publication / ordering | ordering test: cut before the post-refusal note after the `m_cutAt` clear race. |
| Failure / overflow cut | ordering test emits the cut and clears it; `drainSendsTheCutAtTheRefusalPoint`, `repeatedOverflowKeepsEarlierRecoveryCut`, `stampRaisedDuringTheCutEmitIsCoveredOrKept`, `drainKeepsACutThatCouldNotBeSent` pass unchanged. |
| Exhaustion | `pushRetriesAreBoundedWhenContended`, `pausedCutIsNotHidden...` via normal accounting, `exhaustedStampUpdateDefersTheCutBehindQueuedEvents`, `fiveTwelveCapacityKeepsAllRecoveryCuts` pass. The deferred-stamp path is preserved by keeping `ordered == false` for the raised-stamp fallback. |
| Reconfiguration | after a cut is cleared the count returns to 0; the scan test starts empty and the ordering test clears between drains. |
| Audio thread | no allocation/lock/wait/log added; the existing allocation test `pushAndPopAllocateNothing` still passes. |
| Platforms | header-only change built for `zene`, `vstbase` and the MIDI test family on linux x86-64 (RelWithDebInfo). |

## Evidence (this session; logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p18-56.r3/logs)

| Step | Command | Exit | Result |
| --- | --- | ---: | --- |
| RED (old read order, new seams) | `cmake --build build --target MidiHandoffQueueTest -j4` (`build-red2.log`) | 0 | built |
| RED | `cd build/tests && ctest -R MidiHandoffQueueTest --output-on-failure` (`test-red2.log`) | 8 | `raisedStampIsReadAfterTheSlotOccupancy`: `cut.effective` 0 vs 5 (19 passed, 1 failed) |
| tests moved to the Vestige target | file-length gate said 529 > 500 in `MidiHandoffQueueTest.cpp`; `cmake build` refused (`configure-1.log`) | 97 | see above |
| 4b: `git diff -- include/MidiHandoffQueue.h > fix.patch` (seams staged, read-order only) | 0 | 53-line patch |
| 4b RED: `git apply -R fix.patch`, `cmake --build build --target VestigeMidiHandoffTest MidiHandoffQueueTest -j4` (`build-f-red2.log`) | 0 / 0 | reverted, built |
| 4b RED: `ctest -R '^(VestigeMidiHandoffTest\|MidiHandoffQueueTest)$' --output-on-failure` (`test-f-red.log`) | 8 | `VestigeMidiHandoffTest::theRaisedStampIsReadAfterTheSlotOccupancy`: Actual 0, Expected 5 (5 passed, 1 failed) |
| 4b GREEN: `git apply fix.patch` | 0 | restored |
| GREEN build: `cmake --build build --target zene vstbase MidiHandoffQueueTest CarlaMidiHandoffTest VestigeMidiHandoffTest Sf2PlayerRealtimeTest -j4` (`build-f-green.log`) | 0 | built |
| GREEN: `ctest -R '^(MidiHandoffQueueTest\|CarlaMidiHandoffTest\|VestigeMidiHandoffTest\|Sf2PlayerRealtimeTest)$' --output-on-failure` (`test-f-green.log`) | 0 | 4/4 passed; Vestige binary: 6 passed |
| Gates: `feature_check.py <worktree>` (`gates-3.log`) | 0 | all 8 PASS |

Earlier rounds' evidence (round 2: the `m_pendingUsed` count and the `m_cutAt`-cleared-stamp RED/GREEN) is
unchanged and still holds; see the commit history of this file.

Concurrency note (reasoned, not run: local TSan does not work on this box): the producer's order is
raise `m_cutAt` -> `used++` (seq-cst) -> slot CAS -> lower `m_earliestCutAt`; the consumer's is
`m_earliestCutAt` -> `used` (seq-cst) -> slots -> `m_cutAt`. Whatever `used` the consumer sees, a refusal whose
raise precedes that read is visible through the final `m_cutAt` read; one whose raise follows it is a refusal
the producer had not yet made visible, which is the same window the baseline has.

## Gates

`feature_check.py` exit 0: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology, doc-figures, complexity, file-length all PASS.

## Files in the commit

- `include/MidiHandoffQueue.h`
- `tests/src/core/MidiHandoffQueueTest.cpp` (one-line hook-arity update)
- `tests/src/plugins/VestigeMidiHandoffTest.cpp` (two new slots)
- `tests/upstream-modifications.txt` (reason text)
- `docs/qwen-fixes-2026-10/cluster-new-p18-56.md`

Delegation: Haiku ran the RED build/test (twice: first hit a compile error in my seam placement), the GREEN
family build/tests and the first 4b revert/restore cycle; the final RED/GREEN cycle and the reconfigure probe
were run by me because the Haiku start budget (5) was spent. I read every log line cited here myself.

## Self-check

- **The regression test fails without the fix and passes with it.** `test-f-red.log` (fix reverted): exit 8,
  `theRaisedStampIsReadAfterTheSlotOccupancy` Actual 0 / Expected 5. `test-f-green.log` (fix applied): exit 0,
  4/4 passed.
- **Deterministic.** Both new tests run the producer's steps inside seam callbacks on one thread; no thread,
  sleep or wall clock.
- **Fix at the cause, callers and siblings.** Both consumer readers of the shared stamps (`sharedCut()` for the
  drain, `sharedStamp()` for `stamp()`/`due()`) got the same order; the only product caller of `dueCut()` is
  `drainMidi()`. `refreshEarliest()` (consumer, after a served cut) already reads the slots then `m_cutAt`.
- **Scan-count regression kept.** `drainWithoutCutsDoesNotScanPendingSlots` passes (`pendingSlotReads()==0`),
  as does `publishedCutIsNotHiddenByAClearedStamp` (`CUT3 x3` then `E5`).
- **No collateral damage.** Header-only logic; no saved-data change. Seams are `LMMS_TESTING`-only functions; no
  data member was added, so classes embedding `MidiChannelOverflow` keep their layout. No allocation, lock,
  wait or log on the audio thread; the only change on that path is the order of loads already present.
- **Sibling commands.** No command file was changed, so `app.about`, `app.ask`, `app.help`, `app.online_help`,
  `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`, `app.update_check_get`, `app.update_check_run`,
  `app.update_check_set`, `app.version`, `app.whats_new`, `arrangement.fit_tracks` and `arrangement.get_state`
  are not affected. No control command was added (the ControlCommandsSnapshot canary is unchanged).
- **Not ALREADY-FIXED / FALSE-POSITIVE.** REAL: with the fix reverted the consumer misses a concurrently
  published refusal (`test-f-red.log`).

## Other findings

- The lane build tree refuses reconfiguration, so a new test target is impossible here; the queue test is at the
  500-line limit. A later change to `MidiHandoffQueueTest.cpp` needs the file split when the parent can
  reconfigure.
