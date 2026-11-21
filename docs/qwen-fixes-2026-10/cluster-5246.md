FIXED

# Cluster 5246: one record route, one source per period

## Verdict: REAL

`include/RecordRingBuffer.h:2` (and the class comment at `:43-58`) documents the ring as
single-producer/single-consumer. Before this fix, `recordertaps::set()` published a tap
immediately at `src/core/RecorderTaps.cpp:88-99`; the fixed pending store is now at
`src/core/RecorderTaps.cpp:100-104`. Mixer workers run `channelAt` for the
channel taps (`Mixer.cpp:987` and `Mixer.cpp:1022-1023`), and after the join at
`Mixer.cpp:2547` the render thread runs `master()` at `Mixer.cpp:2569`, all inside the same
period. A tap change made between those two points therefore let the channel tap and the master
tap feed the same route's ring in one period: two producers on an SPSC ring. This refutes the
earlier SKIPPED-SCOPE claim that all producers share the render thread; the channel producers
are the workers, and the master producer is the render thread.

## Fix

`include/RecorderTaps.h` and `src/core/RecorderTaps.cpp` now keep two tables: a pending table
written by `set()`, and an active table read by the audio path. `beginPeriod()` at
`src/core/RecorderTaps.cpp:118-135` publishes pending entries into the active table, adjusting
the tapped count around the publication. `Mixer.cpp:2449` calls `beginPeriod()` at the top of
`masterMix`, before any worker or master tap reads the tables, so every source in a period sees
one snapshot. `get()` and `tapped()` stay the UI/pending view (command replies see a tap
immediately); audio code uses `audioTapped()`, and `MultiTrackRecorder.cpp:125` does so.

## Regression test

`tests/src/core/ControlRecordingRoutesTest.cpp:463`
(`aTapChangeDoesNotFeedOneRouteFromTwoSourcesInAPeriod`) drives the real
`record.route_source` edit and the real `channelAt`/`master` entry points. It starts with a real
`masterMix` to activate the Channel tap, performs a mid-period Channel-to-Master edit that must
not push, then runs the next period's `masterMix`, which must push exactly once. There are no
sleeps, threads or timing dependencies: the two producer calls are forced in one period by
construction. The truly concurrent production case is two Channel taps on different mixer
workers; this deterministic Channel-to-Master probe exercises the same period-boundary defect,
because the workers run `channelAt` and then the render thread takes `master` after the join. The
one active snapshot prevents both variants.

## State sequence

`aTapChangeDoesNotFeedOneRouteFromTwoSourcesInAPeriod` drives and asserts exactly these five
ordered tap states, where one period is `frames` frames (256 here):

1. Route 0 `channel`/`post_fader`/`left` is requested through `record.route_source` and stays
   pending, because no period has published it yet (`ControlRecordingRoutesTest.cpp:498-503`).
2. After the first `masterMix` the channel tap is active and `framesPushed` is 256
   (`:507-510`).
3. The direct `recordertaps::channelAt` probe feeds the active channel tap and `framesPushed` is
   512 (`:513-514`).
4. `record.route_source` re-points the same route to `master`/`right` mid-period while the channel
   tap remains active; `recordertaps::master()` pushes nothing and `framesPushed` stays 512
   (`:519-525`).
5. After the next `masterMix` the master/right tap is active and `framesPushed` is 768
   (`:528-529`).

The initial default Input tap and the scope-guard reset to Input are cleanup: the test neither sets
nor asserts them before cleanup, so they are not part of the asserted sequence.

## Test matrix

| Case | Coverage |
|---|---|
| Normal | Existing route/tap and recording tests, 12/12 in `ControlRecordingRoutesTest`, 6/6 family tests |
| Mid-period reconfiguration | Channel tap active, edit to Master, same-period `master()` pushes nothing new |
| Next-period application | Next `masterMix` publishes the pending tap and pushes exactly once |
| Ring exhaustion unchanged | `RecordRingBufferTest` passes in the 6-test family run |
| Atomics-only audio path | `beginPeriod`/`audioTapped`/`channelAt`/`master` use atomic loads/stores only |
| `std::atomic` portability | New tables are `std::atomic<std::uint64_t>` with no new primitive or platform dependency |

## Commands

Logs are under `/home/kruzzzzy/.config/kads/zene/state/runs/5246.r1/logs`.

| Step | Log | Exit |
|---|---|---|
| build-1 | `build-1.log` | 0 |
| test-1-red | `test-1-red.log` | 8 |
| build-1b | `build-1b.log` | 0 |
| test-1b-red | `test-1b-red.log` | 8 |
| build-3-red | `build-3-red.log` | 0 |
| test-3-red (768 vs 512) | `test-3-red.log` | 8 |
| test-3-baseline-record-tools | `test-3-baseline-record-tools.log` | 8 |
| git apply restore | `apply-restore-check.log` | 0 |
| build-3-green | `build-3-green.log` | 0 |
| test-3-green | `test-3-green.log` | 0 |
| test-3-green-detail (12/12) | `test-3-green-detail.log` | 0 |
| test-3-green-record-tools | `test-3-green-record-tools.log` | 8 |
| build-4 | `build-4.log` | 0 |
| test-4-family (6/6) | `test-4-family.log` | 0 |
| test-4-record-tools | `test-4-record-tools.log` | 8 |
| feature_check | `gates-4.log` | 0 PASS |
| git diff --check | `diff-check.log` | 0 |

p4 changed only comments after the p3 behavioural proof; it introduced no behavioural change.

Round 2 re-ran the red/green proof; logs are under
`/home/kruzzzzy/.config/kads/zene/state/runs/5246.r2/logs`. Reverting the product change with
`git apply -R` exited 0, `build-1-red` exited 0, and `test-1-red` exited 8 with
`Actual (recorder.track(0).framesPushed()): 768` against
`Expected (base + expected): 512` at `ControlRecordingRoutesTest.cpp:525` (the `QCOMPARE` after
the `recordertaps::master` call at `:524`). Restoring the change with `git apply` exited 0, and
`build-1-green` exited 0, `test-1-green` exited 0, and `test-1-green-detail` exited 0 with
12/12 passed.

| Step | Log | Exit |
|---|---|---|
| build-1-red | `build-1-red.log` | 0 |
| test-1-red (768 vs 512 at line 525) | `test-1-red.log` | 8 |
| build-1-green | `build-1-green.log` | 0 |
| test-1-green | `test-1-green.log` | 0 |
| test-1-green-detail (12/12) | `test-1-green-detail.log` | 0 |
| build-3 | `build-3.log` | 0 |
| test-3-family (6/6) | `test-3-family.log` | 0 |
| feature_check | `gates-3.log` | 0 PASS |
| submit_check | `submit-3.log` | 0 PASS |

The 6-test family run excludes `RecordToolsTest` because of the already-baselined
environmental `theRemainingRecordTime` failure (see ## Other findings).


## Self-check

- The test fails with the product change reverted and passes with it restored: red runs exit 8
  with `framesPushed` 768 vs the expected 512 (`test-1-red.log`, `test-1b-red.log`,
  `test-3-red.log`); the restored green run exits 0 (`test-3-green.log`).
- The test is deterministic and forces both producers into one period: it calls the real
  `channelAt` and `master` in the same simulated period, with no sleeps, threads or timing.
- All callers and siblings were checked: `get()`/`tapped()` remain the pending UI view,
  `audioTapped()` is the audio view, and `MultiTrackRecorder.cpp:125` is the audio consumer.
- No collateral damage: the 6-test family run passes; `ControlRecordingRoutesTest` 12/12 covers
  the new test plus the route undo/journal tests, and the tap `get()`/`tapped()` assertions are
  proven by `RecordToolsTest::aRouteTakesTheMasterOrAChannel`, which PASSES in
  `test-4-record-tools.log` (26 passed, only the environmental `theRemainingRecordTime` failure).
- Nothing on the audio path allocates, locks, logs or waits; the changed path only reads or
  stores atomics.
- No new command and no undo change.
- Registration and gates pass: the test is registered as test #103 and `feature_check` reports
  PASS.
- Old projects load unchanged: no persisted state or project schema changed.
- Not ALREADY-FIXED or FALSE-POSITIVE: the verdict is REAL, so that item does not apply; the
  defect and its location are shown above (`src/core/RecorderTaps.cpp` publication before the
  period boundary, refuting the earlier SKIPPED-SCOPE claim).
- No command file changed, so no sibling command is affected: the whole diff is
  `include/RecorderTaps.h`, `src/core/RecorderTaps.cpp`, `src/core/Mixer.cpp`,
  `src/core/audio/MultiTrackRecorder.cpp`, `tests/src/core/ControlRecordingRoutesTest.cpp` and
  this report. The listed `app.*` and `arrangement.*` siblings live in files this fix does not
  touch; `record.route_source` keeps its existing reply semantics (`get()`/`tapped()` are the
  pending view).
- The state-sequence artifact is provided above in ## State sequence: five ordered tap states with
  the 256/512/768 frame counts that the test asserts.

## Other findings

`RecordToolsTest::theRemainingRecordTime` fails identically in the reverted and restored trees
because `/home/kruzzzzy/Documents/Zene Studio` is absent (the `groupByVolume` call refuses it),
so the failure is environmental and unrelated (`test-3-baseline-record-tools.log`,
`test-3-green-record-tools.log`, `test-4-record-tools.log`).
