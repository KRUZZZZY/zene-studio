# Cluster new-p9-21-port — FIXED (port of `ceed38eb8`)

**Verdict: FIXED.** This is a port of the 2026-10 fix (`ceed38eb8`, branch `bugs/qwen-new-p9-21`) onto the newest line, written by hand against the current code. The bug was still present on this branch before the change.

**Gate status: both ratchet gates exit 1.** The failures are pre-existing on this branch and are not caused by this change (see "Gates" below). This is stated plainly so the owner can decide whether to keep the commit.

## The finding

The host's request backlog (`CollabHost::onMessage`, queued by `drainRequests`) was capped by line count only (`MaxBacklog = 512`). Peer lines can be up to `MaxPeerLineBytes = 1 MiB` (`include/CollabLink.h:43`), so 512 maximum-size lines can retain about 512 MiB while a nested event loop holds the host.

## Evidence (pre-fix code, this branch)

- `src/core/CollabHost.cpp:307` (pre-fix numbering): `if (m_backlog.size() >= MaxBacklog) { ... close ... }` — the only limit; no byte total exists.
- `include/CollabHost.h:65` (pre-fix and post-fix): `constexpr int MaxBacklog = 512;` — count cap only.
- `src/core/CollabHost.cpp` had no other `m_backlog` appends; `m_backlog` is only appended at `:308` (pre-fix) and cleared in `stop()` at `:147`.

## The fix

- `include/CollabHost.h:66-67` — `MaxBacklogBytes = 8 * 1024 * 1024` (`qint64`), next to `MaxBacklog`.
- `include/CollabHost.h:170` — member `qint64 m_backlogBytes = 0;` beside `m_backlog`.
- `src/core/CollabHost.cpp:148` — `stop()` resets `m_backlogBytes` with `m_backlog.clear()`.
- `src/core/CollabHost.cpp:307-311` — enqueue refuses a line that would exceed either the count or the byte budget (same close message as before).
- `src/core/CollabHost.cpp:315` — enqueue adds the line size.
- `src/core/CollabHost.cpp:324` — dequeue subtracts the line size before the peer lookup, so the total stays correct when the peer has gone.

Queued lines of a closed peer are not purged by `onPeerClosed` (unchanged); `drainRequests` drops them when the peer lookup fails, and the byte total is already decremented for them.

## Regression test

`tests/src/core/CollabTransportTest.cpp:495` — `peerRequestBacklogHasByteBudget`. Two joined peers. The holder's `hold` request keeps the host in a nested event loop; the flooder sends 10 valid lines of about 0.92 MB each (about 9.2 MB total, under the 1 MiB per-line cap and above the 8 MiB budget). The test asserts the flooder is dropped, the holder stays, and only the holder's own request reached the handler.

It is registered in the existing `CollabTransportTest` family (`tests/fork-sources.txt`, already present). No new files, so no new registration is needed.

## Commands and exit codes

`LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-21-port.r1/logs`. Build and test runs are from the worktree root, and ctest runs from `build/tests`.

| Step | Command | EXIT |
|---|---|---:|
| Build (first try) | `cmake --build build --target zene CollabTransportTest > $LOGS/build-1.log 2>&1; echo EXIT=$?` | 2 (environment: ccache could not create a temp file under `/run/user/1000/ccache-tmp`; no compile error) |
| Build (ccache temp in /tmp) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9-21 cmake --build build --target zene CollabTransportTest > $LOGS/build-2.log 2>&1; echo EXIT=$?` | 0 |
| **RED** (fix absent) | `ctest -R '^CollabTransportTest$' --output-on-failure > $LOGS/test-red-1.log 2>&1; echo EXIT=$?` | **8** |
| RED detail | `FAIL! : CollabTransportTest::peerRequestBacklogHasByteBudget()`, `Actual (m_host->peerCount()): 2`, `Expected (1): 1`, `Loc: CollabTransportTest.cpp(554)`; totals 22 passed, 2 failed | — |
| Pre-existing failure, solo | `./CollabTransportTest throttledUntrackedEditIsRetried > $LOGS/throttled-solo-1.log 2>&1; echo EXIT=$?` (run before the fix, with the new test present in the file but not executed) | 1 (`CollabTransportTest.cpp:653`, actual tempo 163, expected 140) |
| Build after fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9-21 cmake --build build --target zene CollabTransportTest > $LOGS/build-3.log 2>&1; echo EXIT=$?` (rebuilt `CollabHost.cpp` and `CollabSession*.cpp`, which include the changed header) | 0 |
| **GREEN** family | `ctest -R '^CollabTransportTest$' --output-on-failure > $LOGS/test-green-1.log 2>&1; echo EXIT=$?` | 8 (only `throttledUntrackedEditIsRetried` fails; see below) |
| GREEN detail | totals 23 passed, 1 failed (of 24 Qt test functions; RED had 22 passed, 2 failed of the same 24) | — |
| **GREEN** new test | `./CollabTransportTest peerRequestBacklogHasByteBudget > $LOGS/green-new-test-1.log 2>&1; echo EXIT=$?` | **0** (PASS, totals 3 passed) |
| Proof: product patch saved | `git diff -- include/CollabHost.h src/core/CollabHost.cpp > $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Proof: reverse fix | `git apply -R $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Proof: build without fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9-21 cmake --build build --target zene CollabTransportTest > $LOGS/build-4.log 2>&1; echo EXIT=$?` | 0 |
| **Proof: test without fix** | `./CollabTransportTest peerRequestBacklogHasByteBudget > $LOGS/proof-unfixed-1.log 2>&1; echo EXIT=$?` | **1** (FAIL at `CollabTransportTest.cpp(554)`, peerCount 2) |
| Proof: reapply fix | `git apply $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Proof: build with fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9-21 cmake --build build --target zene CollabTransportTest > $LOGS/build-5.log 2>&1; echo EXIT=$?` | 0 |
| **Proof: test with fix** | `./CollabTransportTest peerRequestBacklogHasByteBudget > $LOGS/proof-fixed-1.log 2>&1; echo EXIT=$?` | **0** (PASS) |
| Complexity gate | `bash tests/complexity-gate.sh --check > $LOGS/gate-complexity-1.log 2>&1; echo EXIT=$?` | **1** (pre-existing; see below) |
| File-length gate | `bash tests/file-length-gate.sh --check > $LOGS/gate-filelength-1.log 2>&1; echo EXIT=$?` | **1** (pre-existing; see below) |

Affected-family test count: `CollabTransportTest` ran 24 Qt test functions (ctest registers it as one test). 23 pass with the fix; the one failure, `throttledUntrackedEditIsRetried`, also fails when run alone and is not touched by this change.

## Pre-existing failures (not caused by this change)

- **`throttledUntrackedEditIsRetried`** fails before and after the fix, and solo (`throttled-solo-1.log`, EXIT=1). It does not touch the backlog. It is a timing-window test (1.6 s grace, 3 s throttle) about tempo after a resync, and the failure is a tempo mismatch (163 vs 140). It was not investigated further because it is outside this finding. The family result stays red until that test is fixed in its own lane.
- **Complexity gate** (exit 1): the `FAIL` lines are `REGRESSION: new function over target` for `XyPadPairTest.cpp:dotCentreAlong`, `DawProjectReadTracks.cpp:parseNotes`, `ControlCommandsMixerTools.cpp:adjust`, `ControlCommandsMixerStrip.cpp:setStrip`, `ProjectRevisions.cpp:rotateProjectRevision`, `ControlCommandsShortcuts.cpp:importedOverrides`. None of these files is in this diff. No function in `CollabHost` or `CollabTransportTest` is listed.
- **File-length gate** (exit 1): the `REGRESSION: new file over 500 lines` lines include `src/core/SmfInterchangeReader.cpp` (504 code lines), `src/core/TakeLane.cpp` (520), and others not in this diff. `tests/src/core/CollabTransportTest.cpp` is also flagged (602 code lines now). It was already 541 code lines at HEAD and is not in `tests/file-length-baseline.tsv`, so it was already a violation before this change. The new test adds 61 code lines. The gate's code-line count was reproduced with an awk approximation (602 for the worktree file, matching the gate; 504 and 520 for the two untouched files).

Fixing these would mean splitting or re-anchoring files and functions outside this bug. Neither was done: the baseline and limits were not changed.

## Files committed

- `include/CollabHost.h`
- `src/core/CollabHost.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-21-port.md`

Registration: all three product and test files are already in `tests/fork-sources.txt` (lines 738, 1287, 2229). They are fork-owned, so `tests/upstream-modifications.txt` needed no new entry. No new files were added.

## Round-0 review

The brief for this round named no specific review points beyond the port itself; no review text was attached to the prompt. The previous attempt's commit `ceed38eb8` was read as the source of the fix.

## Not done

- Not pushed (per rules).
- No control commands added (so the `ControlCommandsSnapshot` canary is untouched).
- No gate limits, baselines, or test assertions were changed.
