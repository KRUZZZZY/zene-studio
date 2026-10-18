# Cluster new-p10-211-port — FIXED

**Verdict: FIXED.**

Re-applied by hand onto the newest line (050/m0-train-5). Two product changes:

1. `src/core/LinkSyncWire.cpp` `readPacketFields`: `sent_us` must now be a positive
   integer strictly less than 2^63 before conversion to `quint64`. This rejects
   fractional stamps (which truncated to a different timestamp) and out-of-range
   stamps (which overflowed the signed transit subtraction). Added
   `std::trunc(sentUs) == sentUs`.

2. `src/core/LinkSync.cpp` `observePacket`: the sender timestamp must be no later
   than the receiver's monotone clock and no more than `link::PeerTimeoutMs`
   (1,500 ms) old. Older/future announcements are dropped before peer storage or
   adoption, so a stale integral timestamp cannot count most of local uptime as
   network transit. Documented in `docs/LINK-SYNC.md`.

`tests/src/core/ControlLinkCommandsTest.cpp` gains two regressions:
`packetsRoundTripAndForeignOnesAreIgnored` now rejects `sent_us: 0.5`, and a new
`tooOldIntegralTimestampIsIgnoredBeforeRememberingPeer` drops an integral stamp of
`1` against the current clock.

Both changed C++ files are already registered in `tests/fork-sources.txt`; no
source-registration edit was needed. No new files were added.

## Evidence

Working directory: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-211-port`. Logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-211-port.r1/logs`.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline build (fix present) | `cmake --build build --target zene ControlLinkCommandsTest > .../logs/build-1.log 2>&1` | 0 | Rebuilt `zene` and the affected test target. |
| GREEN (fix present) | `ctest -R ControlLinkCommandsTest --output-on-failure > .../logs/test-1.log 2>&1` (from `build/tests`) | 0 | 1/1 test passed. |
| Product-only patch written | `git diff -- src/core/LinkSync.cpp src/core/LinkSyncWire.cpp > .../logs/fix.patch` | 0 | 62 lines. |
| Inverse patch applied (fix removed) | `git apply -R .../logs/fix.patch` | 0 | Product fix reversed. |
| Rebuild without fix | `cmake --build build --target zene ControlLinkCommandsTest > .../logs/build-2.log 2>&1` | 0 | Rebuilt both targets. |
| RED (fix removed) | `ctest -R ControlLinkCommandsTest --output-on-failure > .../logs/test-2.log 2>&1` (from `build/tests`) | 8 | 2 assertions failed: fractional timestamp accepted; stale peer remembered. |
| Restore product fix | `git apply .../logs/fix.patch` | 0 | Exact product patch restored. |
| Rebuild restored | `cmake --build build --target zene ControlLinkCommandsTest > .../logs/build-3.log 2>&1` | 0 | Rebuilt both targets. |
| GREEN restored | `ctest -R ControlLinkCommandsTest --output-on-failure > .../logs/test-3.log 2>&1` (from `build/tests`) | 0 | 1/1 test passed. |
| Affected-family tests | `ctest -R ControlLink --output-on-failure > .../logs/test-4.log 2>&1` (from `build/tests`) | 0 | 3/3 tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/complexity-1.log 2>&1` | 1 | Pre-existing regressions unrelated to this change (6 over-target functions not in the stale baseline); no new regression introduced by this change. |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/file-length-1.log 2>&1` | 1 | Pre-existing regressions unrelated to this change (files over 500 lines not in the stale baseline); no new regression introduced by this change. |

## Files committed

- `src/core/LinkSync.cpp`
- `src/core/LinkSyncWire.cpp`
- `tests/src/core/ControlLinkCommandsTest.cpp`
- `docs/LINK-SYNC.md`
- `docs/qwen-fixes-2026-10/cluster-new-p10-211-port.md`
