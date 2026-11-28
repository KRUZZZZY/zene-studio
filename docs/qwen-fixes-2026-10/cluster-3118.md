FIXED

# Cluster 3118 — re-listening after a Windows accept-thread failure terminates the process

BUG 15. `src/core/ControlServerWin32.cpp` (historical lines 268 and 361).

## Verdict: REAL

The chain is present in this tree, in four steps.

1. The acceptor can return on its own. `win32AcceptLoop()` breaks out of its loop
   when `CreateNamedPipeW` fails (`src/core/ControlServerWin32.cpp:417-433`, the
   `break` at `:433`) and ends with `m_win32Listening.store(false);`
   (`src/core/ControlServerWin32.cpp:491`). The thread object itself is untouched,
   so it stays joinable.
2. That flag is exactly what `listenWin32()` consults first: `if (isListening())
   { return refuse(... "already listening on %1") }`, where `isListening()` is
   `m_win32Listening.load()` (`include/ControlServer.h:115`). After a failed
   acceptor the guard passes.
3. `ControlServer::listen()` hands the Windows case to `listenWin32()` and on a
   refusal returns `false` without closing
   (`src/core/ControlServerSocket.cpp:372-375`); on success it returns before any
   pipe is created, so a pipe-creation failure cannot surface there. Nothing tears
   the previous listener down in between.
4. Before the fix the retry ran
   `m_acceptThread = std::thread(&ControlServer::win32AcceptLoop, this);` over the
   still-joinable thread. `std::thread`'s move assignment calls `std::terminate()`
   on a joinable target, which is a process abort: not an exception, nothing a
   launcher or the shutdown path can observe. The same retry also re-registered
   the shutdown hook over `m_shutdownHookId` without removing the old id, leaving
   a hook that captures `this` and calls `close()` on a destroyed server — the
   leak `close()`'s own comment describes
   (`src/core/ControlServerSocket.cpp:461-470`).

## Fix

At the cause, in three pieces (product files unchanged since round 1):

- `include/RestartableThread.h` (new, fork code): `RestartableThread` owns one
  `std::thread` whose body may finish by itself. `start()` joins the body it ran
  last before it starts another one, `join()` is idempotent, the destructor calls
  it. A joinable thread is therefore never assigned over.
- `include/ControlServer.h:266`: `m_acceptThread` is a `RestartableThread`
  (inside the existing `Q_OS_WIN` block; the header is included from that block).
- `src/core/ControlServerWin32.cpp:354`: a retry first retires what the stopped
  listener left behind — `if (m_acceptThread.joinable()) { close(); }`. `close()`
  is the ordinary teardown: it removes the shutdown hook, notifies the event hub,
  and `closeWin32()` joins the finished acceptor plus the client threads of the
  old transport and closes its two events. The join cannot block, because the
  loop clears `m_win32Listening` only on its way out.
- `src/core/ControlServerWin32.cpp:377` starts the acceptor through the seam, and
  `:690` joins it in `closeWin32()`.

Callers and siblings checked: `listenWin32()` is reached only from `listen()`
(`ControlServerSocket.cpp:373`); `close()`/`closeWin32()` are the only other users
of `m_acceptThread` (`:690`); the client-worker threads keep their own
`std::thread` members and are already joined in `closeWin32()` and reaped by the
acceptor, so they are untouched. No control command, undo step, model or render
path is involved: this code runs on `listen()`/`close()` on the server's thread.

## Round 2: the send-back, and what changed

The external check (`kads_submit_extra.py`) failed round 1 on one item:
`WEAKENING tests/src/core/ControlShutdownHookTestParts2.h: added a skip/expected-failure:
QSKIP("RestartableThread is absent: ...")`. That was a second regression slot whose
`#else` branch skipped on the tree without the seam. It is gone: the lifecycle
assertions it held (join before any start, 64 restarts with no join in between, a
double join, the destructor) now live in
`aFinishedAcceptThreadIsJoinedBeforeTheNextStart`, so the file adds **no** skip and
**no** expected-failure line, and every assertion runs with the fix in place. The
only `#else` left is the pre-fix shape in that one case, and it ends in
`QFAIL`/`std::terminate`, not a skip. Verified locally with the same script the
loop runs: `extra-check-1.log`, EXIT=0,
`kads_submit_extra: weakening, registration and ratchet checks pass against base 3436a808b6`.

## Test matrix

All of it is one QTest slot,
`ControlShutdownHookTest::aFinishedAcceptThreadIsJoinedBeforeTheNextStart`
(declared in `tests/src/core/ControlShutdownHookTest.cpp`, defined in
`ControlShutdownHookTestParts2.h`):

| path | assertion |
|---|---|
| normal | `start()` runs one body; `join()` before any start is a no-op; `join()` retires it |
| failure (the bug) | a body returns on its own (still joinable), then a second `start()` with no join in between — the retried `listen()` |
| exhaustion / reconfiguration | 64 consecutive restarts, each over a body that already returned, then a double `join()` and `QCOMPARE(bodies, 64)` |
| teardown | a destructor retires a body that already returned (`QCOMPARE(shortLivedBodies, 1)`) |
| audio thread | not involved: `start()` allocates and `join()` waits, and both run from `listen()`/`close()` only, never from a render period |
| platform builds | Linux (local): the seam is compiled and driven. Windows (CI only): the TU is `#if defined(Q_OS_WIN)`, so MSVC/MinGW is the platform verdict for the transport half, as `docs/CONTROL-NAMED-PIPE.md` §4 records |

The regression lives in the existing control-transport test file because a lane
build tree may not be reconfigured (`bunny-guard` refuses `cmake -S`, exit 97,
`runs/3118.r1/logs/configure-1.log`), so no new test target could be added.

## Commands and exit codes (round 2, current tree)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/3118.r2/logs`.
Every line below is copied from those logs; no pipe is reported.

| # | command (cwd) | fragment of the output | EXIT |
|---|---|---|---:|
| 1 | `cmake --build build --target ControlShutdownHookTest -j4` (worktree) | `Built target ControlShutdownHookTest` | 0 (`build-1-green.log`) |
| 2 | `cd build/tests && ctest -R '^ControlShutdownHookTest$' --output-on-failure` | `100% tests passed out of 1` | 0 (`test-1-green.log`) |
| 3 | `cd build/tests && ./ControlShutdownHookTest -v1` | `PASS : ...aFinishedAcceptThreadIsJoinedBeforeTheNextStart()`, `Totals: 16 passed, 0 failed, 1 skipped` (the skip is the pre-existing Windows-only `windowsCapsActivePipeClientsAndReusesFreedSlot`) | 0 (`qt-test-1-green.log`) |
| 4 | `python3 .../kads_submit_extra.py --base 3436a808b6 --worktree <worktree>` | `weakening, registration and ratchet checks pass against base 3436a808b6` | 0 (`extra-check-1.log`) |
| 5 | `git diff 3436a808b6..HEAD -- include/RestartableThread.h include/ControlServer.h src/core/ControlServerWin32.cpp > logs/fix.patch` | patch written | 0 (`summary.txt`, `DIFF_EXIT=0`) |
| 6 | `git apply -R logs/fix.patch` | product hunks reversed; test source untouched | 0 (`REVERSE_APPLY_EXIT=0`) |
| 7 | `cmake --build build --target ControlShutdownHookTest -j4` (reversed) | `Built target ControlShutdownHookTest` | 0 (`REVERSED_BUILD_EXIT=0`) |
| 8 | `cd build/tests && ctest -R '^ControlShutdownHookTest$' --output-on-failure` (reversed) | `terminate called without an active exception`, `Received signal 6 (SIGABRT)`, `#11 ... in ControlShutdownHookTest::aFinishedAcceptThreadIsJoinedBeforeTheNextStart()`, `0% tests passed` | **8** (`REVERSED_TEST_EXIT=8`, `test-2-reversed.log`) |
| 9 | `git apply logs/fix.patch` | product hunks restored | 0 (`RESTORE_APPLY_EXIT=0`) |
| 10 | `cmake --build build --target zene ControlShutdownHookTest -j4` (restored) | `Built target zene`, `Built target ControlShutdownHookTest` | 0 (`RESTORED_BUILD_EXIT=0`) |
| 11 | `cd build/tests && ctest -R '^ControlShutdownHookTest$' --output-on-failure` (restored) | `100% tests passed out of 1` | 0 (`RESTORED_TEST_EXIT=0`, `test-3-restored.log`) |
| 12 | `git status --short` after the round trip | nothing (clean) | `GIT_STATUS_BEGIN` / `GIT_STATUS_END` empty in `summary.txt` |
| 13 | `FEATURE_CHECK_LOG_DIR=logs/feature-check python3 .../feature_check.py <worktree>` | 8 rows, every one PASS (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`) | 0 (`gates-1.log`) |

Rows 5–12 are the required 4b proof on the current tree: with the product change
reversed the regression aborts (`terminate called without an active exception`,
ctest exit 8, frame #11 inside the case); restored it passes (exit 0), and the
worktree is clean afterwards.

The same case also ran red before the fix existed at all (round 1, product change
not yet written): `runs/3118.r1/logs/test-1-red.log`, ctest exit 8, `terminate
called without an active exception`, `aFinishedAcceptThreadIsJoinedBeforeTheNextStart`.

## Collateral damage

- POSIX is untouched by construction and by measurement: the whole edit in
  `include/ControlServer.h` sits inside `#if defined(Q_OS_WIN)`, and the POSIX
  token stream of that header is identical before and after — 172195 lines each,
  empty diff, `runs/3118.r1/logs/posix-token-check/` (`README.txt`, `before.pre`,
  `after.pre`, `flags.sh`). The product files have not changed since that
  measurement (round 2 touched tests only).
- No saved data, project file, model, undo step, command or render path is
  touched; the changed code runs on `listen()`/`close()` only.
- `zene` was rebuilt with the final tree (row 10), so the header change compiles
  in the product target, not only in the test.
- `tests/prove-posix-unchanged.sh` is red on this train for reasons that predate
  this cluster: it compares against `release/0.3.0` and the branch has since added
  `MaxClients`, `ControlEventQueue`, `QDeadlineTimer`-related members and the
  idle-deadline fields. Run here it reports `POSIX-PROOF EXIT=1`, and neither
  `RestartableThread` nor `m_acceptThread` occurs anywhere in its output (0
  matches). I did not change that script or its baseline.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  Reverted: rows 5–8, ctest exit 8, `terminate called without an active exception`
  with the abort inside `aFinishedAcceptThreadIsJoinedBeforeTheNextStart()`.
  Restored: rows 9–11, ctest exit 0. Before the fix existed it also failed the
  same way (round 1 `test-1-red.log`, exit 8).
- **No test was weakened.** The round-1 skip is removed (round-2 section above);
  `kads_submit_extra.py` now reports `weakening, registration and ratchet checks
  pass` (row 4). The only platform-dependent code left in the new lines is the
  pre-fix shape in the one case's `#else`, which fails (`QFAIL` after a
  `std::terminate`), not skips.
- **The test is deterministic.** No sleeps and no wall-clock decision: every body
  signals a `QSemaphore` as its last statement and the case waits for that signal
  (`tryAcquire(1, 30000)` is a hang guard, not a timeout that can make a result
  pass). No ordering assumption beyond `join()`'s own guarantee. The seam is
  `RestartableThread::start()`.
- **Fix at the cause and complete.** The cause is a joinable `std::thread`
  receiving a new thread, plus the remains (hook, two events, client threads) of a
  listener that stopped by itself. `RestartableThread` makes the first
  unrepresentable and `close()` in `listenWin32()` retires the second. All three
  uses of `m_acceptThread` (start, join, the reset guard) go through the seam; the
  client-worker threads and the POSIX listener are untouched.
- **No collateral damage.** See above: identical POSIX token stream, `zene`
  rebuilt, no data, command, undo or realtime surface involved, and no limit or
  check was added or changed.
- **ALREADY-FIXED / FALSE-POSITIVE item:** not applicable — the verdict is FIXED;
  the pre-fix assignment was present in the base commit and was executed by the
  reverted build (row 8).

## Not verified here (stated, not hidden)

- The Windows half cannot be compiled or executed on this box: no MSVC, no MinGW,
  no Qt for Windows. `src/core/ControlServerWin32.cpp`, `include/ControlServer.h`'s
  `Q_OS_WIN` block and the `RestartableThread` usage inside them are therefore
  CI-only evidence, exactly as `docs/CONTROL-NAMED-PIPE.md` §4 records for this
  transport. The seam those edits use is compiled and driven locally, and the
  edits are four lines plus comments.
- No local TSan on this box, so the concurrency argument is a design argument: the
  acceptor's join happens after the loop stored `m_win32Listening=false` (`:491`),
  which is the last statement the loop executes, so `join()` returns promptly;
  `close()` is called from the same thread that owns `listen()` and `close()`.

## Other findings

- `tests/prove-posix-unchanged.sh` compares against `release/0.3.0`, which is now
  several clusters behind this branch, so it reports `DIFFERS`/`CHANGED` rows for
  changes already committed on the train. Its exit code cannot be used as evidence
  on this branch without rebasing its baseline. I did not touch it.
- `build` cannot be reconfigured from a lane session (`bunny-guard`, exit 97,
  `runs/3118.r1/logs/configure-1.log`), so a regression test for this area has to
  go into an existing registered test target rather than a new one.
