# Cluster new-p10-211-port — FIXED

**Verdict: FIXED.**

Re-applied on top of the committed fix (33dc244bc) after Luna's round-1 QA review, which asked for a deterministic stale-timestamp regression. Production code is unchanged from the committed fix; only the test and this report changed.

Two product changes (already committed in 33dc244bc):

1. `src/core/LinkSyncWire.cpp` `readPacketFields`: `sent_us` must be a positive integer strictly less than 2^63 before conversion to `quint64`. Rejects fractional stamps (which truncated) and out-of-range stamps (which overflowed the signed transit subtraction). Added `std::trunc(sentUs) == sentUs`.
2. `src/core/LinkSync.cpp` `observePacket`: sender timestamp must be no later than the receiver's monotone clock and no more than `link::PeerTimeoutMs` (1,500 ms) old. Older/future announcements are dropped before peer storage or adoption.

`tests/src/core/ControlLinkCommandsTest.cpp` changes in this round:

- `tooOldIntegralTimestampIsIgnoredBeforeRememberingPeer` now injects explicit sender/receive clocks (`receivedUs = 10000000`, `sentUs = receivedUs - (PeerTimeoutMs*1000 + 1)`) so the age exceeds the limit regardless of process uptime.
- `packetsRoundTripAndForeignOnesAreIgnored` rejects `sent_us: 0.5` and `9223372036854775808.0`.

Affected-family run: 1 passed, 2 skipped.

## Evidence

Working directory: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-211-port`. Logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-211-port.r3/logs`.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Build (fix present) | `cmake --build build --target zene ControlLinkCommandsTest > .../logs/build-1.log 2>&1` | 0 | Rebuilt `zene` and the affected test target. |
| GREEN (fix present) | `ctest -R ControlLinkCommandsTest --output-on-failure > .../logs/test-1.log 2>&1` (from `build/tests`) | 0 | 1/1 test passed. |
| Product-only patch written | `git diff ded355440..33dc244 -- src/core/LinkSync.cpp src/core/LinkSyncWire.cpp > .../logs/fix.patch` | 0 | 62 lines. |
| Inverse patch applied (fix removed) | `git apply -R .../logs/fix.patch` | 0 | Product fix reversed. |
| Rebuild without fix | `cmake --build build --target zene ControlLinkCommandsTest > .../logs/build-2.log 2>&1` | 0 | Rebuilt both targets. |
| RED (fix removed) | `ctest -R ControlLinkCommandsTest --output-on-failure > .../logs/test-2.log 2>&1` (from `build/tests`) | 8 | 2 assertions failed. |
| Restore product fix | `git apply .../logs/fix.patch` | 0 | Exact product patch restored. |
| Rebuild restored | `cmake --build build --target zene ControlLinkCommandsTest > .../logs/build-3.log 2>&1` | 0 | Rebuilt both targets. |
| GREEN restored | `ctest -R ControlLinkCommandsTest --output-on-failure > .../logs/test-3.log 2>&1` (from `build/tests`) | 0 | 1/1 test passed. |
| Affected-family tests | `ctest -R ControlLink --output-on-failure > .../logs/test-4.log 2>&1` (from `build/tests`) | 0 | 1 passed, 2 skipped. |
| Complexity/file-length gate | `python3 .../feature_check.py <worktree>` | 0 | RESULT: PASS. |

## Files committed

- `tests/src/core/ControlLinkCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-211-port.md`

(Production files `src/core/LinkSync.cpp` and `src/core/LinkSyncWire.cpp` were committed in 33dc244bc.)

## Self-check

- The regression test FAILS with the product change reverted (exit 8, 2 assertions failed, see test-2.log) and PASSES with it (exit 0, see test-3.log).
- The test is deterministic: the stale case injects `receivedUs = 10000000` and `sentUs = receivedUs - (PeerTimeoutMs*1000 + 1)` instead of `LinkSyncEngine::monotonicMicros()`; no sleeps or races.
- The fix is at the cause and complete: `readPacketFields` validates `sent_us` before conversion, and `observePacket` validates before peer storage/adoption; every caller (`decodePacket`, `handleDatagram`, `observePacket`, `followNewestRevision`) routes through these checks. Sibling `adoptionPlacesTheSharedBeatPhase` remains within the 1.5s limit.
- No collateral damage: old projects/saved data are unaffected (no persistence format change); the injected 10s receive clock is test-only and does not alter production limits; legitimate peers within 1.5s still pass.
- Verdict is FIXED (not ALREADY-FIXED/FALSE-POSITIVE); evidence is the code changes in 33dc244bc.
- Affected family: 1 passed, 2 skipped (see test-4.log).
- feature_check.py: exit 0, RESULT: PASS (see feature_check.log).
- `git status` clean after commit.
filePath">
/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-211-port/docs/qwen-fixes-2026-10/cluster-new-p10-211-port.md