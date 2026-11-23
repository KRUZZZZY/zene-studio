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

## Regression tests (`tests/src/core/MidiHandoffQueueTest.cpp`)

- `drainWithoutCutsDoesNotScanPendingSlots` (`:428`, kept from round 1) — 128 events on a 512-capacity queue
  with no overflow: `pendingSlotReads() == 0` and all 128 events consumed.
- `publishedCutIsNotHiddenByAClearedStamp` (`:440`, new) — fills a 4-capacity queue, drains it, then records a
  refusal with the publication hooks. At the `afterRaise` pause the consumer sends the due cut (clearing
  `m_cutAt`); at the `afterRemember` pause a second producer queues the post-refusal note and the consumer
  drains with a one-event budget. The test asserts the budget was spent on the cut (`consumed == 4`) and the
  trace is `E1 E2 E3 E4 CUT3 CUT3 CUT3 E5`, i.e. every cut precedes the post-refusal note. The two pauses are
  the exact window the review named; no thread, sleep or wall clock is involved (the hooks run the consumer
  from inside the producer's record call, the note is queued by the second-producer simulation).

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

## Evidence (this session; logs in the r2 log folder)

| Step | Command | Exit | Result |
| --- | --- | ---: | --- |
| RED: ordering test added, round-1 fast path still in place | `cmake --build build --target MidiHandoffQueueTest -j4` (`build-1-red.log`) | 0 | built |
| RED | `cd build/tests && ctest -R '^MidiHandoffQueueTest$' --output-on-failure` (`test-1-red.log`) | 8 | `consumedInBudgetCall`: 5 vs 4 |
| first counter attempt (found the ordering/fallback interaction) | build (`build-2-green.log`), test (`test-2-green.log`) | 0 / 8 | two older tests exposed the `ordered` flag and the 0-latest overwrite |
| counter + guards | build (`build-4-green.log` / `build-5-green.log`), test (`test-4-green.log` / `test-5-green.log`) | 0 / 0 | all 17 pass |
| staged seam only (no counter), RED re-proof | build (`build-6-red-prefix.log`), test (`test-6-red-prefix.log`) | 0 / 8 | same consumed 5 vs 4 |
| GREEN final code | build (`build-13-green.log`), test (`test-13-green.log`) | 0 / 0 | 17 passed |

### Step 4b — the test catches the bug

`fix.patch` is `git diff -- include/MidiHandoffQueue.h` with the test seam and test file staged, so it holds
only the behavioural counter change (43 insertions, 21 deletions; no test code, no publication hooks).

| Command | Exit | Result |
| --- | ---: | --- |
| `git diff -- include/MidiHandoffQueue.h > .../logs/fix.patch` | 0 | patch with the counter fix only |
| `git apply -R .../logs/fix.patch` | 0 | reverted |
| `cmake --build build --target MidiHandoffQueueTest -j4` (`build-14-red.log`) | 0 | built |
| `cd build/tests && ctest -R '^MidiHandoffQueueTest$' --output-on-failure` (`test-14-red.log`) | 8 | `queue.consumed()` 5 vs 4, and `sent` = `E1 E2 E3 E4 CUT3 E5 CUT3` vs `... CUT3 CUT3 CUT3 E5` |
| `git apply .../logs/fix.patch` | 0 | restored |
| `cmake --build build --target zene vstbase MidiHandoffQueueTest CarlaMidiHandoffTest VestigeMidiHandoffTest Sf2PlayerRealtimeTest -j4` (`build-15-green-family.log`) | 0 | all built |
| `cd build/tests && ctest -R '^(MidiHandoffQueueTest\|CarlaMidiHandoffTest\|VestigeMidiHandoffTest\|Sf2PlayerRealtimeTest)$'` (`test-15-green-family.log`) | 0 | 4/4 passed |

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-2.log`, exit 0):
fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures,
complexity, file-length — all PASS. `bash tests/file-length-gate.sh --check` after the compact test rewrite:
PASS (exit 0, `gate-filelength-2.log`). The test file sits at the 500-code-line limit, so both regressions are
kept in the existing target rather than a new one.

## Files in the commit

- `include/MidiHandoffQueue.h`
- `tests/src/core/MidiHandoffQueueTest.cpp`
- `tests/upstream-modifications.txt` (the test-file reason now covers both regressions)
- `docs/qwen-fixes-2026-10/cluster-new-p18-56.md`

## Self-check

- **The regression test fails with the product change reverted and passes with it.** Reverted (`git apply -R
  logs/fix.patch`, rebuild, `ctest -R '^MidiHandoffQueueTest$'`): exit 8, `queue.consumed()` 5 vs 4 and the
  note before the cut (`test-14-red.log`). Restored (rebuild, family ctest): exit 0, 4/4 passed
  (`test-15-green-family.log`). The pre-seam RED with the round-1 code also exited 8 (`test-1-red.log`).
- **The test is deterministic.** It uses the compile-time seam `recordWithPublicationHooks()` (after-raise and
  after-remember callbacks inside `recordChannel()`) and the `pendingSlotReads()` probe; the consumer drains
  from inside those callbacks, and the post-refusal note is queued by the simulated second producer. There is
  no thread, sleep, wall clock or race in the test: the queue, the refusal position (4) and the one-event
  budgets are injected values.
- **Fix at the cause, callers and siblings.** `drainMidi()` (`:618`) is the only caller of `dueCut()`/`clear()`;
  both `sharedCut()` (drain) and `sharedStamp()` (`stamp()`/`due()`), plus the deferred path through
  `effectiveStamp()`, stay consistent (the raised-stamp fallback keeps `ordered == false`). Render callers
  (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:387`, `plugins/Vestige/Vestige.cpp:405`,
  `RemotePlugin::sendQueuedMidiEvents()`, Carla's `processPeriod()`) keep the same `drainMidi()` contract.
  `refreshEarliest()` still scans after a served cut. All existing queue tests pass, including the 512-capacity
  and deferred-stamp ones.
- **No collateral damage.** Header-only runtime logic; no project/save/format/serialization change, no new
  limit or validation. The count only ever skips a scan that would find no slot; a published slot always
  forces the scan. A redundant cut at the same stamp still occurs after a served cut, exactly as before.
- **Not ALREADY-FIXED / FALSE-POSITIVE.** REAL: the reverted build emits the post-refusal note before the
  cut (`test-14-red.log`), and the fix makes the cut precede it.
- **Sibling commands.** No command file or registration was changed; `app.about`, `app.ask`, `app.help`,
  `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`, `app.update_check_get`,
  `app.update_check_run`, `app.update_check_set`, `app.version`, `app.whats_new`, `arrangement.fit_tracks`
  and `arrangement.get_state` are untouched. No control command was added (the ControlCommandsSnapshot
  canary is unchanged).

## Other findings

- The lane build tree refuses reconfiguration (`bunny-guard: configuring is the parent's job`), so a new test
  target cannot be built or run here; the two regressions consequently live in the existing
  `MidiHandoffQueueTest` target at exactly 500 code lines. A future change to that file will need to split it
  when the parent reconfigure is available.
- `MidiChannelOverflow` is compiled with `LMMS_TESTING` in test TUs but without it in product/plugin TUs while
  objects cross the vtable boundary in `VestigeMidiHandoffTest`. The probe counter is a test-only *function*
  (not data), and `m_pendingUsed` is declared in every build, so the layout stays identical; the round-1
  hook refactor is method-only.
