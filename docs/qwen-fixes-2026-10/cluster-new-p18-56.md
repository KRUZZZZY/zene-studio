FIXED

# Cluster new-p18-56 — MIDI draining repeatedly scans thousands of atomic slots

## Verdict: REAL

`drainMidi()` (`include/MidiHandoffQueue.h:581`) checks all 16 channels on every loop iteration. Each check
calls `MidiChannelOverflow::dueCut()` -> `sharedCut()` (`:408`). In the common case (no refusal happened yet,
or every cut has been served) the cached earliest cut `m_earliestCutAt[channel]` is 0, and the old code then
fell back to `pendingStamp()` (`:462`), which scans all 513 slots of that channel with an acquire load each.
With no overflow at all, `m_cutAt[channel]` is also 0, so the whole scan is known in advance to find nothing:

- 16 channels x (128 events + 1 final empty check) x 513 slots = **1,058,832 atomic loads per 128-event
  period**, measured by the regression test before the fix (see RED below). That is the finding, and it is
  on the render thread (`ZynAddSubFxInstrument::forwardQueuedMidi()`, `VestigeInstrument::processImpl()`,
  `RemotePlugin::sendQueuedMidiEvents()`, Carla's `processPeriod()` all call `drainMidi()` from their render
  callback with `maxEvents == queue.capacity()`).

### Why the scan can be skipped (the invariant)

A pending slot is only ever published by `remember()` (`:448`), which runs *after* `raise()` has already made
`m_cutAt[channel]` non-zero in the same `recordChannel()` call (`:370`). The single consumer clears a slot only
through `clearCovered()`, and `clear()` (`:312`) clears `m_cutAt` whenever it clears every slot covered by the
sent cut; a slot above the sent cut keeps a `m_cutAt` at least that high. Therefore:

> `m_cutAt[channel] == 0` implies the channel has no pending slot.

`sharedCut()` and `sharedStamp()` now read `m_cutAt` before falling back to the 513-slot scan and return the
empty result when it is 0, reducing the no-cut cost to two acquire loads per channel. Nothing changes when any
cut is outstanding (`m_cutAt != 0`): the scan still runs, so intermediate cuts and the deferred-stamp path are
untouched. A cut recorded from a full queue cannot become due before 512 further pops (queue capacity), and a
cut recorded after retry exhaustion is carried by the deferred flag that `collectDeferred()` reads at the top
of every iteration, so the fast path cannot make a due cut disappear.

## Fix

`include/MidiHandoffQueue.h`

- `:214-221` — document the raise-before-publish / clear-together invariant that makes the fast path sound.
- `:408-425` — `sharedCut()` returns early with `stamp == 0` when `m_cutAt[channel]` is 0, instead of scanning
  `MaxPendingCuts` slots per channel per event.
- `:476-487` — `sharedStamp()` (the public `stamp()`/`due()` path) takes the same fast path, so both entry
  points stay consistent.
- `:343-346`, `:466-469`, `:531-536` — test-only probe counter `pendingSlotReads()` / `m_pendingSlotReads`,
  incremented per slot read in `pendingStamp()`. The member is declared in every build (only used under
  `LMMS_TESTING`): a test-only *data* member changes the layout of classes that embed `MidiChannelOverflow`
  (`RemotePlugin.h:385`) between test and product translation units and broke the `VestigeMidiHandoffTest`
  vtable thunks (link error `undefined reference to non-virtual thunk to lmms::VstPlugin::saveSettings`, thunk
  offset 74040 vs the library's 73976). The comment on the member records that hazard.

No behaviour outside the no-cut path changes: no new allocation, lock, wait or log on the render path, no
command or serialization change.

## Test matrix

| Path | Coverage |
| --- | --- |
| Normal (no overflow) | `drainWithoutCutsDoesNotScanPendingSlots`: 128 events, 512-capacity queue, 0 slot reads, 0 allocations. |
| Failure / overflow cut | Same test, second half: a refused event records a cut, the drain emits exactly one cut and clears it (`stamp(3) == 0`). Existing `drainSendsTheCutAtTheRefusalPoint`, `repeatedOverflowKeepsEarlierRecoveryCut`, `exhaustedStampUpdateDefersTheCutBehindQueuedEvents`, `fiveTwelveCapacityKeepsAllRecoveryCuts` still pass. |
| Exhaustion | `pushRetriesAreBoundedWhenContended`, `CarlaMidiHandoffTest::retryExhaustionThroughEnqueueBoundsOverflowAccounting` pass unchanged. |
| Reconfiguration | After the cut is served the next drain makes no further slot reads (`readsAfterCut` unchanged). |
| Audio-thread constraints | Allocation probe around the 128-event drain returns 0; no lock/wait/log added. |
| Supported platforms | Header-only change built for `zene`, `vstbase`, and the MIDI hand-off test family on linux x86-64 (RelWithDebInfo). |

## Evidence (all commands from the worktree; logs in the run log folder)

| Step | Command | Exit | Result |
| --- | --- | ---: | --- |
| Baseline before test | `cmake --build build --target MidiHandoffQueueTest -j4` (`build-1-baseline.log`) | 0 | built |
| Baseline before test | `cd build/tests && ctest -R '^MidiHandoffQueueTest$' --output-on-failure` (`test-1-baseline.log`) | 0 | 1/1 passed |
| RED, test added, no fix | build as above (`build-2-red.log`) | 0 | built |
| RED, test added, no fix | same ctest (`test-2-red.log`) | 8 | `drainWithoutCutsDoesNotScanPendingSlots`: Actual 1058832, Expected 0 |
| GREEN with fix | `cmake --build build --target zene MidiHandoffQueueTest CarlaMidiHandoffTest -j4` (`build-3-green.log`) | 0 | built |
| GREEN with fix | `ctest -R '^(MidiHandoffQueueTest\|CarlaMidiHandoffTest)$'` (`test-3-green.log`) | 0 | 2/2 passed |
| Layout regression found while rebuilding the header's dependents | `cmake --build build --target VestigeMidiHandoffTest -j4` (`build-6-family.log`) | 2 | thunk link error from the test-only data member; fixed by declaring the counter member in every build |
| Family rebuild | `cmake --build build --target zene vstbase MidiHandoffQueueTest CarlaMidiHandoffTest VestigeMidiHandoffTest Sf2PlayerRealtimeTest -j4` (`build-14-green-final.log`) | 0 | all built |
| Family tests | `ctest -R '^(MidiHandoffQueueTest\|CarlaMidiHandoffTest\|VestigeMidiHandoffTest\|Sf2PlayerRealtimeTest)$'` (`test-12-green-final.log`) | 0 | 4/4 passed |

### Step 4b — the test catches the bug

`fix.patch` is `git diff -- include/MidiHandoffQueue.h` with the test seam staged, so it contains only the
behavioural fix (no test code, no probe counter).

| Command | Exit | Result |
| --- | ---: | --- |
| `git diff -- include/MidiHandoffQueue.h > .../logs/fix.patch` | 0 | 47-line patch, fix hunks only |
| `git apply -R .../logs/fix.patch` | 0 | reverted |
| `cmake --build build --target MidiHandoffQueueTest -j4` (`build-13-red-final.log`) | 0 | built |
| `cd build/tests && ctest -R '^MidiHandoffQueueTest$' --output-on-failure` (`test-11-red-final.log`) | 8 | FAIL: Actual 1058832, Expected 0 |
| `git apply .../logs/fix.patch` | 0 | restored |
| `cmake --build build --target zene vstbase MidiHandoffQueueTest CarlaMidiHandoffTest VestigeMidiHandoffTest Sf2PlayerRealtimeTest -j4` (`build-14-green-final.log`) | 0 | all built |
| `cd build/tests && ctest -R '^(MidiHandoffQueueTest\|CarlaMidiHandoffTest\|VestigeMidiHandoffTest\|Sf2PlayerRealtimeTest)$'` (`test-12-green-final.log`) | 0 | 4/4 passed |

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-2.log`,
exit 0): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
doc-figures, complexity, file-length — all PASS.

The first run was red with `file-length NEW: tests/src/core/MidiHandoffQueueTest.cpp (514)` because the
regression pushed the file past 500 code lines (`gates-1.log`, exit 1). The two cases were folded into one
compact test; `bash tests/file-length-gate.sh --check` then reported PASS (exit 0), and the full gate run is
green.

## Files in the commit

- `include/MidiHandoffQueue.h`
- `tests/src/core/MidiHandoffQueueTest.cpp`
- `tests/upstream-modifications.txt` (declares the test-file change)
- `docs/qwen-fixes-2026-10/cluster-new-p18-56.md`

## Self-check

(To be completed from the completion check output.)
