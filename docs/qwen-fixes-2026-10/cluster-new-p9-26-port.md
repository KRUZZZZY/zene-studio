# Cluster new-p9-26 (port to 050/m0-train-5)

**Verdict: FIXED — gates RED (pre-existing; merge needs an owner decision).**

The remote-role promotion bug is still present on the newest line and is fixed here. The fix is the one accepted on `bugs/qwen-new-p9-26` (`815f4290f`, `22cdbcebd`), re-applied by hand. The product fix and both regressions pass. Two ratchet gates exit 1. Neither failure is caused by the fix (details below), so the commit is not mergeable until the owner decides how to clear them.

## Finding (historical line 395, now `src/core/CollabHost.cpp:411`)

`CollabHost::setPeerRole()` set `it->role`, sent the `role` message, and only then tried to build the snapshot for a Remote-to-Editor/Viewer promotion. A failed snapshot still returned `true`, so the peer was told it had a project role and never received a snapshot.

## Fix

- `include/CollabHost.h:121-122`: `bool setPeerRole(int peerId, CollabRole role, QString* error = nullptr);`. On failure, `error` is empty for an absent peer and holds the snapshot error for a failed promotion.
- `src/core/CollabHost.cpp:411-434`: the snapshot is built first. On failure the function emits the notice, sets `*error`, and returns `false` before the role or the `role` message change. On success the role is set, the `role` message is sent, and the snapshot is sent.
- `src/core/CollabSession.cpp:257-267`: `setRole()` maps an empty error to `not_found` (absent peer) and a non-empty error to `refused` with the snapshot reason.

## Regression tests (`tests/src/core/CollabTransportTest.cpp`)

- `failedPromotionKeepsPeerRemote` (host level): a snapshot provider that always fails; `setPeerRole` must return `false` and the peer must stay `remote`, with no snapshot received.
- `sessionReportsFailedPromotionAndKeepsPeerRemote` (session level): a partial-load project makes serialization refuse; `CollabSession::setRole` must return `refused` naming the section, an absent peer must still be `not_found`, and the peer stays Remote.
- `cleanup()` now also stops a hosting session. This is teardown only; no assertion changed.

No new files, so nothing to register in `tests/fork-sources.txt`. `CollabHost.h`/`CollabHost.cpp` already have entries in `tests/upstream-modifications.txt`; `CollabSession.cpp` is fork-new (added in `009f591b2`) and is already in `tests/fork-sources.txt`.

Only `CollabTransportTest.cpp` includes `CollabHost.h` besides `CollabSession.cpp`, `CollabHost.cpp`, and `CollabSessionSync.cpp`. The `zene` target and `CollabTransportTest` were rebuilt after the header change.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-26-port.r1/logs`. Commands are run from the worktree root unless marked `build/tests`. Every build from build 2 onward ran with `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9-26` (ccache's default temp dir under `/run/user/1000` is not writable in this session; this sets only the temp location, not the build).

| Step | Command | Exit | Result |
|---|---|---:|---|
| Build 1 (failed, environment) | `cmake --build build --target zene CollabTransportTest -j2 > LOGS/build-1.log 2>&1; echo EXIT=$?` | 2 | ccache could not write `/run/user/1000/ccache-tmp` (Permission denied); no compile error |
| Build 2 (RED build, original product code) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9-26 cmake --build build --target zene CollabTransportTest -j2 > LOGS/build-2.log 2>&1; echo EXIT=$?` | 0 | built |
| **RED** | `ctest -R '^CollabTransportTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | 8 | 22 passed, 3 failed: `failedPromotionKeepsPeerRemote` ("`!m_host->setPeerRole(...)` returned FALSE", `test-1.log:31`), `sessionReportsFailedPromotionAndKeepsPeerRemote` ("`!refused.ok` returned FALSE", `test-1.log:34`), and `throttledUntrackedEditIsRetried` (pre-existing; see below) |
| Build 3 (GREEN build, fix applied) | `CCACHE_TEMPDIR=... cmake --build build --target zene CollabTransportTest -j2 > LOGS/build-3.log 2>&1; echo EXIT=$?` | 0 | built |
| **GREEN** | `ctest -R '^CollabTransportTest$' --output-on-failure > LOGS/test-2.log 2>&1; echo EXIT=$?` (from `build/tests`) | 8 | 24 passed, 1 failed: only `throttledUntrackedEditIsRetried` (`test-2.log:25`) |
| Proof, step 1: save patch and reverse it | `git diff -- include/CollabHost.h src/core/CollabHost.cpp src/core/CollabSession.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch; echo EXIT=$?` | 0 | product files reverted |
| Build 4 (reversed) | `CCACHE_TEMPDIR=... cmake --build build --target zene CollabTransportTest -j2 > LOGS/build-4.log 2>&1; echo EXIT=$?` | 0 | built |
| **Proof RED** (fix reverted) | `ctest -R '^CollabTransportTest$' --output-on-failure > LOGS/test-3.log 2>&1; echo EXIT=$?` (from `build/tests`) | 8 | 22 passed, 3 failed: both new regressions fail again (`test-3.log:31`, `:34`) |
| Proof, step 2: restore | `git apply LOGS/fix.patch; echo APPLY_EXIT=$?` | 0 | product fix restored |
| Build 5 (restored) | `CCACHE_TEMPDIR=... cmake --build build --target zene CollabTransportTest -j2 > LOGS/build-5.log 2>&1; echo BUILD_EXIT=$?` | 0 | built |
| **Proof GREEN** (restored) | `ctest -R '^CollabTransportTest$' --output-on-failure > LOGS/test-4.log 2>&1; echo TEST_EXIT=$?` (from `build/tests`) | 8 | 24 passed, 1 failed (`throttledUntrackedEditIsRetried` only) |
| Complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1; echo COMPLEXITY_EXIT=$?` | 1 | see below |
| File-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1; echo FILELENGTH_EXIT=$?` | 1 | see below |
| Whitespace | `git diff --check; echo DIFFCHECK_EXIT=$?` | 0 | clean |

**Affected family:** `CollabTransportTest` executed 25 test functions (ctest reports 1/1 test; the totals line gives 24 passed + 1 failed in the GREEN run).

**The family is not fully green.** The only failure in every run, with or without the fix, is `throttledUntrackedEditIsRetried`, at `tests/src/core/CollabTransportTest.cpp:589`: after a join, the local tempo (163) is never replaced by the host's snapshot tempo (140) within 10 s. It fails identically in the RED run before any product change (`test-1.log:25`), so it is on this line already. This fix does not touch the throttled path, and the test was not modified. It needs a separate look.

## Gate status (both RED, pre-existing)

- **Complexity gate (exit 1):** the regressions reported are in files this commit does not touch (for example `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`). `setPeerRole`/`setRole` do not appear. `git diff --stat HEAD` lists only the four files above.
- **File-length gate (exit 1):** lists 13 files, including `tests/src/core/CollabTransportTest.cpp (613)`. That file was already over the 500-code-line limit at HEAD: a code-line count of `git show HEAD:...` gives 541 under the gate's rules (blank and comment-only lines excluded), and it has no baseline entry. My change adds 72 code lines (541 → 613), and the file is not in the baseline. Clearing it means moving existing tests out of this file, which is a refactor outside this bug. Re-anchoring is a decision for the owner, and I did not change the baseline or the limit.

## Files committed

- `include/CollabHost.h`
- `src/core/CollabHost.cpp`
- `src/core/CollabSession.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-26-port.md`
