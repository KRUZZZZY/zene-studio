# Cluster new-p9-23-port: missing collaboration sinks (port of bcdcb691e)

Verdict: **FIXED** — the accepted fix from `bugs/qwen-new-p9-23` is re-applied by hand on `bugs/qwen-new-p9-23-port` (based on `050/m0-train-5`, `b02a0ff2b`). The bug was still present on the newest line, so this is not ALREADY-FIXED.

Two gates (`complexity-gate.sh --check`, `file-length-gate.sh --check`) exit 1 on this line. Both failures are on code this change does not touch and predate it (details under "Gates"). They are reported as-is, not fixed.

## Bug

`CollabClient` skipped its failure branch when a sink was not installed and then reported success:

- snapshot: `m_haveSnapshot` set true and `snapshotApplied` emitted although nothing was applied;
- command: `commandApplied` emitted although nothing was applied.

## File evidence

- `src/core/CollabClient.cpp:245-249` (`onSnapshot`): `!m_snapshotSink ||` routes a missing sink into the existing failure branch with the notice `snapshot sink is not installed`; returns before `m_haveSnapshot = true` and `snapshotApplied`.
- `src/core/CollabClient.cpp:264-268` (`onCommand`): `!m_commandSink ||` routes a missing sink into the existing failure branch (`m_haveSnapshot = false`, notice, `requestResync`); returns before `commandApplied`.
- Before the fix (`git show b02a0ff2b:src/core/CollabClient.cpp`): `if (m_snapshotSink && !m_snapshotSink(...))` and `if (m_commandSink && !m_commandSink(...))` — a missing sink made the condition false and fell through to success.
- `tests/src/core/CollabTransportTest.cpp:292-303` `missingSnapshotSinkIsReportedAsAnApplicationFailure` and `:305-317` `missingCommandSinkIsReportedAsAnApplicationFailure` — regressions (same bodies as bcdcb691e).

## Evidence table

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-23-port.r1/logs/`. Commands run from the worktree root unless noted. The `ctest` commands ran from `build/tests`.

| Step | Command | Exit | Result |
|---|---|---:|---|
| Build attempt 1 (RED) | `cmake --build build --target zene CollabTransportTest -j2 > build-1-red.log 2>&1` | 2 | Environment failure, not a code error: ccache could not write temp files to `/run/user/1000/ccache-tmp` ("Permission denied"; outside the write-jail). |
| Build (RED) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-23 cmake --build build --target zene CollabTransportTest -j2 > build-2-red.log 2>&1` | 0 | Built against the unfixed production code, with the two new regressions. |
| **RED test** | `ctest -R '^CollabTransportTest$' --output-on-failure > test-1-red.log 2>&1` | 8 | Both new regressions FAIL: `missingSnapshotSinkIs…` `Actual (applied.size()): 1 / Expected (0): 0` at `CollabTransportTest.cpp:300`; `missingCommandSinkIs…` same at `:315`. Totals 22 passed, 3 failed (third is the pre-existing failure below). |
| Fix applied | hand edit of `src/core/CollabClient.cpp:245-249, 264-268` | — | 4 insertions, 2 deletions. |
| Build (GREEN) | `CCACHE_TEMPDIR=… cmake --build build --target zene CollabTransportTest -j2 > build-3-green.log 2>&1` | 0 | Built with the fix. |
| GREEN test | `ctest -R '^CollabTransportTest$' --output-on-failure > test-2-green.log 2>&1` | 8 | Both new regressions pass; totals 24 passed, 1 failed (pre-existing failure below). |
| Save fix patch | `git diff -- src/core/CollabClient.cpp > logs/fix.patch` | 0 | Product change only. |
| Reverse fix | `git apply -R logs/fix.patch` | 0 | Buggy production code restored temporarily. |
| Build (reversed) | `CCACHE_TEMPDIR=… cmake --build build --target zene CollabTransportTest -j2 > build-4-reversed.log 2>&1` | 0 | Built without the fix. |
| **Proof RED** | `ctest -R '^CollabTransportTest$' --output-on-failure > test-3-reversed.log 2>&1` | 8 | Both new regressions FAIL again (same assertions); totals 22 passed, 3 failed. |
| Reapply fix | `git apply logs/fix.patch` | 0 | Fix restored (`git diff --stat`: 1 file, 4 insertions, 2 deletions). |
| Build (restored) | `CCACHE_TEMPDIR=… cmake --build build --target zene CollabTransportTest -j2 > build-5-restored.log 2>&1` | 0 | Built with the fix restored. |
| **Proof GREEN** | `ctest -R '^CollabTransportTest$' --output-on-failure > test-4-restored.log 2>&1` | 8 | Both new regressions PASS; totals 24 passed, 1 failed (pre-existing failure only). |
| Isolated pre-existing failure, run 1 | `build/tests/CollabTransportTest throttledUntrackedEditIsRetried > throttled-iso-1.log 2>&1` | 1 | FAIL at `CollabTransportTest.cpp:615` (`Actual song->getTempo() 163`, `Expected hostTempo 140`). |
| Isolated pre-existing failure, run 2 | same, `throttled-iso-2.log` | 1 | Same failure. |
| Complexity gate | `bash tests/complexity-gate.sh --check > gate-1.log 2>&1` | 1 | FAIL: "complexity ratchet regressed" — 6 regressions, none in `CollabClient` (see Gates). |
| File-length gate | `bash tests/file-length-gate.sh --check > gate-2.log 2>&1` | 1 | FAIL: "REGRESSION: new file over 500 lines: tests/src/core/CollabTransportTest.cpp (564)" (see Gates). |

Tests executed: the affected family `CollabTransportTest` is 1 CTest target with 25 QtTest results (24 passed, 1 failed on the final run). Zero tests were not run.

Test environment: `QT_QPA_PLATFORM=offscreen` only; `LMMS_PLUGIN_DIR` not set.

## Pre-existing failure (not this bug)

`CollabTransportTest::throttledUntrackedEditIsRetried` (`tests/src/core/CollabTransportTest.cpp:581`) fails deterministically when run alone, both before and after the fix (see the isolated runs above). Its body is unchanged by this commit and it does not touch sinks; it asserts a tempo value after timing-based `QTest::qWait` windows and the tempo is at the local-edit value. Not investigated further, not weakened, not changed.

## Gates

Both gates exit 1. Neither failure comes from this change:

- **Complexity** (`gate-1.log`): 6 regressions — `XyPadPairTest.cpp` (`dotCentreAlong`, CCN 16), `DawProjectReadTracks.cpp` (`parseNotes`, 13), `ControlCommandsMixerTools.cpp` (`adjust`, 12), `ControlCommandsMixerStrip.cpp` (`setStrip`, 11), `ProjectRevisions.cpp` (`rotateProjectRevision`, 11), `ControlCommandsShortcuts.cpp` (`importedOverrides`, 11). `CollabClient` does not appear in the over-CCN-10 list, so `onSnapshot`/`onCommand` are at CCN ≤ 10.
- **File length** (`gate-2.log`): `tests/src/core/CollabTransportTest.cpp` is not in `tests/file-length-baseline.tsv`, so the gate treats it as a new file over 500 code lines. On `b02a0ff2b` it was already 541 code lines (`git show b02a0ff2b:tests/src/core/CollabTransportTest.cpp`, counted with the gate's rule: blank and comment-only lines excluded; the same count method gives 564 on this tree, matching the gate). This change adds 23 code lines to a file already over the limit. The gate's fail condition is unchanged by this commit.

The gate limits and baselines were not changed. Fixing either gate failure would mean reworking unrelated files, or splitting the test file, which is outside this bug.

## Files committed

- `src/core/CollabClient.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-23-port.md`

Registration: no new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged. Both files are already registered as fork-scope sources.

Notes:
- ccache needed `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-23` (the default temp dir is outside the write-jail); the compiler and sources were not changed by this.
- `fff` tools were not used for log searches; the logs were searched with `grep` in the shell.
- No Luna QA round-0 points were included in the brief beyond the port job, so none were addressed separately.
