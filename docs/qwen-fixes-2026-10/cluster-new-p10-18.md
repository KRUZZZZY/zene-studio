FIXED: `CollabHost::start()` wrote through a null `error` pointer when the listen failed; the write is now guarded.

# Cluster new-p10-18 (BUG 17: failed host start dereferences a null error pointer)

## Finding (historical line 134, now `src/core/CollabHost.cpp:134-137`)

The listen-failure branch of `CollabHost::start()` wrote `*error` unconditionally. A caller that passed `nullptr` for the error sink and started on an occupied port crashed instead of getting `false`.

## Decision: REAL

- Before the fix, `src/core/CollabHost.cpp:136` (on the base commit `639e1d23e`) was `*error = QStringLiteral("could not listen on port %1: %2")...` with no null check. The test `occupiedPortRefusedWithoutErrorSink` reproduces the crash: SIGSEGV writing to address `0x0` (see the RED rows below).
- The header declares `start(address, port, QString* error)` with no default, and its comment said "False with \a error when the port cannot be bound". That comment does not say the sink may be null, so the API contract was ambiguous. The finding offered two options: null-check the output, or make the non-null requirement explicit. I took the null-check, because it keeps the refusal (`false`) for every caller and needs no call-site change.
- Siblings checked in the same family: `CollabHost::setPeerRole()` already uses the nullable pattern (`src/core/CollabHost.cpp:466`, `if (error != nullptr) { error->clear(); }`), and `src/core/CollabHost.cpp:477` guards `*error` the same way. `CollabHost::snapshotFrame(QString*)` and its callers (`:250`, `:313`, `:413`) always pass a live `QString`, so they cannot receive null. No other change needed.
- Every production caller passes a non-null sink: `src/core/CollabSession.cpp:183` (`m_host->start(QHostAddress::Any, port, &error)`, twice). The two `host.start()` calls at `tests/src/core/CollabTransportTest.cpp:1025` and `:1046` are `CraftedPeer::start()`, not `CollabHost::start()`.

## Fix

- `src/core/CollabHost.cpp:136`: `if (error != nullptr) { *error = ...; }`. The function still returns `false` and leaves `m_server` unset for both nullptr and non-null sinks. The non-null path is unchanged.
- `include/CollabHost.h:108`: the comment now says "False with \a error filled when the port cannot be bound; \a error may be nullptr." Signature unchanged, so no caller changes.

## Regression tests

Added to `tests/src/core/CollabTransportTest.cpp` (no new files, so no registration):

- `occupiedPortRefusedWithoutErrorSink()` (`:518`): a `QTcpServer` holds a port on `LocalHost`, `CollabHost::start(..., occupier.serverPort(), nullptr)` must return `false` and leave `isListening()` false. Deterministic: the port is held by a live listener, so the bind fails; no timing involved.
- `occupiedPortReportsErrorToSink()` (`:528`): the same setup with a non-null sink must return `false` and put the port number in the error. This passes on the unfixed code too; it guards the message path.

## Evidence

All commands were run from the worktree or from `build/tests` as noted. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-18.r1/logs/`.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| build (first try) | `cmake --build build --target CollabTransportTest -j2 > build-1.log 2>&1` | 2 | ccache could not create a temp file under a path the sandbox does not allow (`build-1.log:18`). Not a code error. |
| build (ccache off) | `CCACHE_DISABLE=1 cmake --build build --target CollabTransportTest -j2 > build-2.log 2>&1` | 0 | Built the unfixed test binary. |
| **RED** (unfixed, ctest) | `ctest -R '^CollabTransportTest$' --output-on-failure > test-1.log 2>&1` | 8 | `CollabTransportTest (SEGFAULT)`; `test-1.log:20` `Received signal 11 (SIGSEGV) ... accessing address 0x0000000000000000`, during `occupiedPortRefusedWithoutErrorSink`. |
| **RED** (unfixed, direct) | `QT_QPA_PLATFORM=offscreen ./CollabTransportTest occupiedPortRefusedWithoutErrorSink > test-3-red-unfixed.log 2>&1` | 139 | SIGSEGV at address `0x0`. Repeated on the rebuilt unfixed binary (`test-7-red-rebuilt.log`, EXIT 139). |
| control (unfixed, direct) | `QT_QPA_PLATFORM=offscreen ./CollabTransportTest occupiedPortReportsErrorToSink > test-2-control-unfixed.log 2>&1` | 0 | Passes without the fix, as expected. |
| fix patch | `git diff -- src/core/CollabHost.cpp include/CollabHost.h > logs/fix.patch` then `git apply -R logs/fix.patch` | 0 / 0 | Reverted for the unfixed run; then `git apply logs/fix.patch` restored it (exit 0). |
| rebuild (unfixed) | `CCACHE_DISABLE=1 cmake --build build --target zene CollabTransportTest CollabPolicyTest CollabPanelTest -j2 > build-4-unfixed.log 2>&1` | 0 | Header changed, so all four dependent targets rebuilt. |
| rebuild (fixed) | `CCACHE_DISABLE=1 cmake --build build --target zene CollabTransportTest CollabPolicyTest CollabPanelTest -j2 > build-5-fixed.log 2>&1` | 0 | |
| **GREEN** (fixed, ctest) | `ctest -R '^CollabTransportTest$' --output-on-failure > test-4-green.log 2>&1` | 8 | Both new tests `PASS` (`test-4-green.log:20-21`). `Totals: 42 passed, 1 failed`. The one failure is pre-existing (below). |
| **GREEN** family (ctest) | `ctest -R 'Collab' --output-on-failure > test-8-family-green.log 2>&1` | 8 | `Total Tests: 4`. `CollabPolicyTest`, `CollabPanelTest`, `ControlCollab` pass. `CollabTransportTest` fails only on `throttledUntrackedEditIsRetried` (pre-existing). |

### Pre-existing failure: `throttledUntrackedEditIsRetried`

It fails with the fix, with the fix reverted, and on repeated solo runs, with the same values:

| Run | Command | EXIT | Result |
| --- | --- | --- | --- |
| fixed, solo, run 1 | `QT_QPA_PLATFORM=offscreen ./CollabTransportTest throttledUntrackedEditIsRetried > test-5-solo-1.log 2>&1` | 1 | `Actual 163`, `Expected (hostTempo) 140` (`CollabTransportTest.cpp:993`) |
| fixed, solo, run 2 | same, `test-5-solo-2.log` | 1 | same |
| unfixed, solo | same, `test-6-unfixed-throttled.log` | 1 | same `163` vs `140` |

The function is a 10-second timing wait on a snapshot tempo and does not reach the `start()` failure branch (its host starts on port 0). `docs/qwen-fixes-2026-10/cluster-new-p9-26-port.md:50` records the same failure with and without the fix on an earlier port. I did not change or weaken it.

Unverified: this run is on Linux only. The bind-conflict test relies on a second listener on the same `127.0.0.1` port failing to bind, which holds on Linux; the macOS and Windows hosted runs have not been run here.

## Gates

`feature_check.py` could not run from this lane: it writes its per-gate logs to `<worktree>/../<name>-feature-check/`, which is `projects/zene-studio-program/`, outside this run's write-jail. It failed with `PermissionError` (`logs/gate-1-feature-check.log`, EXIT 1). I ran the same gate commands it lists, directly in the worktree:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh > gate-2-fork-sources.log 2>&1` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > gate-2-all-sources-reproduce.log 2>&1` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > gate-2-no-upstream-regression.log 2>&1` | 0 |
| check-namespace | `tests/scripted/check-namespace > gate-2-check-namespace.log 2>&1` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh > gate-2-no-tautology.log 2>&1` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py > gate-2-doc-figures.log 2>&1` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > gate-2-complexity.log 2>&1` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > gate-2-file-length.log 2>&1` | 1 |

Ratchet items in the files this change touches:

- Complexity: no `REGRESSION` line names `CollabHost.cpp`, `CollabHost.h` or `CollabTransportTest.cpp`. The new functions are straight-line `QVERIFY` checks.
- File length: the only item is `REGRESSION: new file over 500 lines: tests/src/core/CollabTransportTest.cpp (1008)` (`gate-2-file-length.log:29`). This is not new. The file is not in `tests/file-length-baseline.tsv`, but it already had 1019 counted (non-blank, non-comment) lines on the base commit (`git show 639e1d23e:tests/src/core/CollabTransportTest.cpp`, counted with the gate's rule). The base therefore reports the same key. My 22 added lines do not change which item is listed. `src/core/CollabHost.cpp` stays at 489 lines, under the limit.

## Self-check

- **Regression fails with the product change reverted and passes with it.** RED: `occupiedPortRefusedWithoutErrorSink` on the unfixed binary exits 139 (SIGSEGV at `0x0`, `test-3-red-unfixed.log`, rebuilt-binary repeat `test-7-red-rebuilt.log`); under ctest the binary exits 8 (SEGFAULT, `test-1.log:20`). GREEN: `./CollabTransportTest occupiedPortRefusedWithoutErrorSink` on the fixed binary exits 0 (`test-9-green-direct.log`, `PASS`). `occupiedPortReportsErrorToSink` exits 0 on both trees (`test-2-control-unfixed.log`, `test-10-green-direct.log`).
- **Deterministic.** The two new tests have no sleeps, waits, timers or races. The occupied port is a live `QTcpServer` listener on `LocalHost` that stays open for the whole test, so the bind in `CollabHost::start()` fails every time. Ports are chosen by the OS (`listen(..., 0)`).
- **Fix at the cause, siblings checked.** The one unguarded write was `CollabHost.cpp:136`. Siblings and callers were checked (see Decision): `setPeerRole` (`:466`) and `:477` already guard, `snapshotFrame` callers always pass a live `QString`, and the only production caller `CollabSession.cpp:183` passes `&error`. The header comment (`CollabHost.h:108`) now states the nullable contract, so the API matches the behaviour.
- **No collateral damage.** The only behaviour change is that a null sink on a failed bind no longer dereferences null. Success, a failed bind with a non-null sink, the `false` return, and the error text are unchanged (`occupiedPortReportsErrorToSink` pins the text). No persisted data, project format, limit or check is touched, so old projects and saved data load as before. No control command added.
- **Verdict is REAL (fixed), not ALREADY-FIXED or FALSE-POSITIVE:** see Decision, with `src/core/CollabHost.cpp:136` as the pre-fix evidence.
- **`feature_check.py` gate: not passing from this lane, for a sandbox reason.** It cannot create `<worktree>-feature-check/` outside the write-jail (`gate-1-feature-check.log`, `PermissionError`, EXIT 1). The same commands it runs were executed directly (table above): six PASS (exit 0), the two ratchets exit 1, and their only item in the touched files is pre-existing on the base (see Gates). The lane has no way to make the script's own table print PASS without writing outside the jail; this is reported as-is, not re-anchored or worked around in the gate.
