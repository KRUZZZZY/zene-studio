# Cluster new-p12-255 — Concurrent downloads overwrite the same partial file (round 3 rework)

**Verdict: FIXED.** Round 2's staging-file fix stands; this round adds the rename-loser
cleanup Luna flagged and tightens the regression test's timeout.

## Changes this round

- `src/core/StemModelStore.cpp` — in the rename-loser branch of `download()` (where a
  concurrent installer moved the pinned bytes into place between our `remove(finalPath)`
  and our `rename()`), remove this attempt's `partPath` before returning success, so a
  losing install no longer leaves a full-size duplicate model file behind.
- `tests/src/core/StemModelStoreTest.cpp` —
  - `clobbersThePartialInFlight()` now returns `-1` on timeout instead of `false`, and
    the slot fails unless an outcome was observed (truncation, or a second staging file).
  - Added `testConcurrentSuccessfulDownloadsLeaveNoStagingFile()`: 10 iterations of two
    concurrent successful downloads over a local HTTPS server, asserting both succeed,
    the final file verifies, and no staging files remain. (Coverage for the
    rename-loser leftover.)
  - Failed-download path also asserts no staging file is left behind.

## Evidence table

| Check | Command (cwd) | Exit | Result |
|---|---|---:|---|
| RED (shared part restored) | `ctest -R '^StemModelStoreTest$'` (build/tests), after reverting StemModelStore.cpp to HEAD~1 | 1 | FAIL: `outcome != 1` returned FALSE (clobber) |
| GREEN | `ctest -R '^StemModelStoreTest$'` (build/tests), fix restored | 0 | 4/4 Stem tests pass |
| Full suite | `ctest -R '^Stem'` | 0 | 4/4 |
| QtTest count | `StemModelStoreTest` binary | 0 | 21 passed, 0 failed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | pre-existing red (no file touched here) |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | pre-existing red (no file touched here) |

## Files committed

- `src/core/StemModelStore.cpp`
- `tests/src/core/StemModelStoreTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-255.md`
"""