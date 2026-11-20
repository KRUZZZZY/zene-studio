FIXED

# cluster new-p14-114: a hardware-output route that does not fit the bus is dropped whole

## 1. Decision: REAL

`HardwareOutputBus::add()` checked the channel bound inside the loop, after the left plane had already been written:

- Original `src/core/HardwareOutputBus.cpp:59-68`: the loop wrote the plane for `channel = start` (s = 0), then at s = 1 hit `if (channel >= MaxChannels) { return; }`. For a stereo route starting at `MaxChannels - 1` the left half was added to the last channel and the right half was dropped. The bus kept a half-written route.
- Now guarded at `src/core/HardwareOutputBus.cpp:59-61`, before any channel is written: `if (start > MaxChannels - sides) { return; }`. The comparison is written as `MaxChannels - sides`, not `start + sides`, so it cannot overflow for a start near INT_MAX. The per-channel check inside the loop is removed.

## 2. Reachability

The defect is real but latent in the product. The callers pass routes that were parsed and bounded to `MaxChannels` before they reach the bus: `HardwareIo.cpp:121`, `HardwareIo.cpp:129`, `HardwareIo.cpp:278` (verified this session by grep) and `AudioIoSetup.cpp:187` (named in the packet, not re-checked this session). So this is a latent bus-API defect. The direct `add()` test is the reported surface.

## 3. Test matrix and reverse-patch proof

Four slots, all in `tests/src/core/HardwareOutputBusTest.cpp` (added lines only; 67 insertions):

| Slot | Test | Route | Expected after fix | Reverted code (log 07) |
|---|---|---|---|---|
| 1 | `stereoRouteAtChannelLimitAddsNothing` | width 2 at `MaxChannels - 1` | channel `MaxChannels - 1` stays 0 (no left half) | FAIL |
| 2 | `stereoRouteAtLastFullPairStillPlays` | width 2 at `MaxChannels - 2` | both halves written (0.25 / 0.75) | pass |
| 3 | `monoRouteAtLastChannelStillPlays` | width 1 at `MaxChannels - 1` | average 0.6 written | pass |
| 4 | `droppedRouteLeavesNoResidue` | dropped route at `MaxChannels - 1`, kept route at `MaxChannels - 2` | kept route's right half is 0.75, no residue | FAIL |

Reverse-patch proof: `fix.patch` was reverse-applied (log 05, EXIT=0), the bus target rebuilt (log 06, EXIT=0), and the focused test run. Log 07 shows `Totals: 5 passed, 2 failed` with the two FAIL lines for slots 1 and 4, and the test exits with EXIT=8. The fix was then re-applied (log 08, EXIT=0), rebuilt (log 09, EXIT=0), and the focused test passes (log 10, EXIT=0). Slots 2 and 3 pass both before and after, so they guard against an over-correction.

## 4. The fix and the header contract

- `src/core/HardwareOutputBus.cpp:59-61`: the route-level bound check, before the loop (see section 1).
- `include/HardwareOutputBus.h:48-49` (class comment) and `:74-76` (`add()` comment): a route that does not fit entirely inside the bus is dropped whole; neither channel is written. The old wording "channel number outside the bus is dropped" no longer described the behaviour.
- `add()` keeps its signature and its realtime properties: no allocation, no lock, no new container.

## 5. Commands and exit codes (from logs in `state/runs/new-p14-114.r1/logs/`)

- 01-build-red.log: build of HardwareOutputBusTest; the log has no EXIT line; it ends at "Built target HardwareOutputBusTest".
- 02-test-red.log: ctest on the unfixed test; the log has no EXIT line; it shows `265 - HardwareOutputBusTest (Failed)`.
- 03-build-green.log: build of the green tree (WidePinRoutingTest target shown); the log has no EXIT line.
- 04-test-green.log: focused test on the fixed tree, EXIT=0.
- 05-apply-reverse.log: reverse-apply of fix.patch, EXIT=0.
- 06-build-reversed.log: rebuild with the fix reverted, EXIT=0.
- 07-test-reversed.log: focused test with the fix reverted, EXIT=8 (2 of 7 failed, see section 3).
- 08-apply-back.log: re-apply of fix.patch, EXIT=0.
- 09-build-reapplied.log: rebuild with the fix, EXIT=0.
- 10-test-reapplied.log: focused test with the fix re-applied, EXIT=0.
- 11-build-family.log: build of the related bus-family targets, EXIT=0.
- 12-test-family.log: tests of the related bus family, EXIT=0 (7.76 s).
- 13-feature-check.log: feature_check before this report, EXIT=0, RESULT: PASS.

## Self-check

The judgement items of submit_check are answered here by topic. Each answer names its evidence.

1. Realtime safety: `add()` gains one integer comparison and loses one per-channel comparison. It still writes only to the storage that `init()` allocated; no allocation, lock or container change is added (`src/core/HardwareOutputBus.cpp:59-61`).
2. Test proves the change: slots 1 and 4 of the focused test fail on the reverted code (log 07, EXIT=8) and pass with the fix (log 10, EXIT=0). Slots 2 and 3 guard against over-dropping.
3. Sibling code: the diff touches only the bus `add()` path and its header comment. The callers (`HardwareIo.cpp:121,129,278`) pass unchanged arguments; their behaviour changes only for routes that previously half-wrote the bus. No command files, registrations or gates are changed.
4. Criterion met: the defect (half-written route past the last channel) is shown by a failing test, fixed, and the test passes again (sections 1-3).
5. Docs and manual: the header comments that promised "channel outside the bus is dropped" for `add()` are updated (section 4). `add_plane()` keeps its own per-channel contract, which is a different call.

## Other findings

none found in the diff. `add_plane()` (its own per-channel bound) was not reviewed in this packet.
