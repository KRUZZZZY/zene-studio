# Cluster new-p10-11-port: FIXED (port of 324d2ab9d)

## Finding and evidence

**Verdict: FIXED.** This is a port of `324d2ab9d` ("Fix cluster new-p10-11 inflight request loss") from `bugs/qwen-new-p10-11` onto the newest line (`050/m0-train-5`, CachyOS toolchain). The bug was still present on this branch before the port.

- Bug on this branch: [src/core/CollabClient.cpp:314](../../src/core/CollabClient.cpp:314) reads `if (m_inflight.size() >= MaxInflight) { m_inflight.clear(); }`. At the 256-entry limit it erases every ID-to-command association, so a later error reply for an earlier request reaches `onReply()` with an empty command name.
- Fix applied (same change as the original commit): line 314 is now `if (m_inflight.size() >= MaxInflight) { return 0; }`. At capacity `request()` refuses the new request (returns `0`, which callers already treat as "not sent", matching the no-link case at line 313) and keeps the pending associations until replies arrive.
- Regression test: [tests/src/core/CollabTransportTest.cpp:535](../../tests/src/core/CollabTransportTest.cpp:535) `reachingInflightLimitPreservesPendingRequestCommands`, using a real TLS transport. It queues 256 requests, sends one overflow request, and checks that the first delayed rejection names the original command and that the overflow returns `0`. The constant `InflightRequestLimit` is at [tests/src/core/CollabTransportTest.cpp:94](../../tests/src/core/CollabTransportTest.cpp:94).
- Registration: no new files. `src/core/CollabClient.cpp` is already in `tests/fork-sources.txt:1285`. `tests/src/core/CollabTransportTest.cpp` is in `tests/fork-sources.txt:2229` and already has an `upstream-modifications.txt` row (line 2000, from new-p8-306), so no new row was added. The original fix did not add one either.

## Verification evidence

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-11-port.r1/logs/`. All ctest runs were from `build/tests`. Each run executed the single `CollabTransportTest` ctest entry (0 tests run in none of them).

ccache note: the first build (`build-1.log`, EXIT=2) failed because ccache could not write temp files under `/run/user/1000/ccache-tmp` (permission denied). It was not a compile error. Every build after it used `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p10-11`.

| Step | Exact command (run from the worktree root unless noted) | Exit | Result |
|---|---|---:|---|
| Build, first try | `cmake --build build --target zene CollabTransportTest -j2 > $L/build-1.log 2>&1` | 2 | ccache permission error; no compile |
| Build, pre-fix (RED) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p10-11 cmake --build build --target zene CollabTransportTest -j2 > $L/build-2.log 2>&1` | 0 | built with test added, fix not applied |
| **RED** | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > $L/test-1.log 2>&1` | **8** | 22 passed, 2 failed. New test: `FAIL! reachingInflightLimitPreservesPendingRequestCommands() 'notices.first().at(0).toString().contains(firstCommand)' returned FALSE. (the host refused '': delayed rejection)` at test line 563. Second failure: `throttledUntrackedEditIsRetried` (see below). |
| Build, fixed | `CCACHE_TEMPDIR=... cmake --build build --target zene CollabTransportTest -j2 > $L/build-3.log 2>&1` | 0 | built with fix |
| **GREEN** | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > $L/test-2.log 2>&1` | 8 | 23 passed, 1 failed. New test `PASS`; only `throttledUntrackedEditIsRetried` fails (see below). |
| Isolation check of the unrelated failure (fixed build) | `cd build/tests && ./CollabTransportTest throttledUntrackedEditIsRetried > $L/test-3.log 2>&1` | 1 | 2 passed, 1 failed; same failure with the new test not run first |
| Patch proof, fix reversed | `git diff -- src/core/CollabClient.cpp > $L/fix.patch && git apply -R $L/fix.patch` | 0 | — |
| Build, reversed (RED proof) | `CCACHE_TEMPDIR=... cmake --build build --target zene CollabTransportTest -j2 > $L/build-4.log 2>&1` | 0 | — |
| **RED, proof** | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > $L/test-4.log 2>&1` | **8** | 22 passed, 2 failed. The new test fails on the same assertion as in test-1. |
| Re-apply fix | `git apply $L/fix.patch` | 0 | — |
| Build, re-applied | `CCACHE_TEMPDIR=... cmake --build build --target zene CollabTransportTest -j2 > $L/build-5.log 2>&1` | 0 | — |
| **GREEN, proof** | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > $L/test-5.log 2>&1` | **8** | 23 passed, 1 failed. New test `PASS`. Only `throttledUntrackedEditIsRetried` fails. |
| Complexity gate | `bash tests/complexity-gate.sh --check > $L/gate-1.log 2>&1` | **1** | FAIL: regressions in untouched files and functions (see below) |
| File-length gate | `bash tests/file-length-gate.sh --check > $L/gate-2.log 2>&1` | **1** | FAIL: regressions in untouched files, plus `tests/src/core/CollabTransportTest.cpp` (see below) |

Affected family: `CollabTransportTest`, 1 ctest entry, 24 test entries executed per run (`Totals` line: 23 passed, 1 failed, 0 skipped in test-2 and test-5).

### Unrelated failure: `throttledUntrackedEditIsRetried`

It fails in every run: with the fix, without the fix (test-1 and test-4), and when run alone (test-3). Its assertion is at [tests/src/core/CollabTransportTest.cpp:623](../../tests/src/core/CollabTransportTest.cpp:623). The song tempo is 163 (the local edit) where the host snapshot should have restored 140. It exercises the session resync path and never reaches the 256-request limit that this fix changes. I did not investigate its cause. It is not caused by this change and is not part of this cluster.

### Gate failures (pre-existing, not caused by this diff)

Both gates exit 1 on this branch. The regressions are in files and functions this diff does not touch. Examples:
- Complexity: `tests/src/gui/XyPadPairTest.cpp` `dotCentreAlong` (CCN 16), `src/core/DawProjectReadTracks.cpp` `parseNotes` (CCN 13), `src/core/ControlCommandsMixerTools.cpp` `adjust` (CCN 12), and others (`gate-1.log:97-102`).
- File length: `src/core/SmfInterchangeReader.cpp` (504), `src/core/TakeLane.cpp` (520), `tests/src/core/DeviceDspTest.cpp` (752), `tests/src/gui/ArrangerTimelineTest.cpp` (689), and about 15 others (`gate-2.log:24-37`).

None of these files or functions is in the diff, and none of them appears in `tests/complexity-baseline.tsv` or `tests/file-length-baseline.tsv`. The project's own status note (`CLAUDE.md`, 2026-10-15) records gates 4 and 7 as red on purpose on the newest line.

One file in the diff is among the length regressions: `tests/src/core/CollabTransportTest.cpp`. It was already over 500 code lines at HEAD (570 by `grep -vcE '^\s*(//|$)'`; the gate counts 573 with this diff, so the true HEAD figure is a little lower but still over the limit). It is not in the baseline, so the gate flags it regardless of this diff. This diff adds 35 lines to it. I did not split the file, because that is a restructure outside a one-bug port, and I did not re-anchor or edit the gates or baselines.

The complexity gate does not flag the new test function or `CollabClient::request`.

## Files committed

- `src/core/CollabClient.cpp` (line 314)
- `tests/src/core/CollabTransportTest.cpp` (lines 94, 535-566)
- `docs/qwen-fixes-2026-10/cluster-new-p10-11-port.md` (this file)
