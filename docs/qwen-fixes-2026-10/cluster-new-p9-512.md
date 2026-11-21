FIXED - cluster new-p9-512: moving a work dispatcher read its own unconstructed base

Verdict: FIXED (REAL). Fix commit `91260338d1`; regression rework commit `474bc5c263` (branch `bugs/qwen-new-p9-512`, base `37eaf07557`).

## Finding and decision

The finding points at `src/3rdparty/ableton-link/asio/include/asio/detail/work_dispatcher.h`, line 0, and
`work_dispatcher.hpp:273`. The file on this branch is `.hpp`, and the line has moved to **283** (the finding's
line numbers are historical). The defect is real and is at the cause:

- Before the fix, `src/3rdparty/ableton-link/asio/include/asio/detail/work_dispatcher.hpp:283` read
  `: work_dispatcher_function<Function>(static_cast<work_dispatcher_function<Function>&&>(*this))` in the
  legacy (TS) executor specialisation's move constructor. `*this` is the object being constructed, so its
  function base is read before it exists. That is undefined behaviour and can lose or corrupt the wrapped function.
- The sibling move constructor for the non-TS specialisation, `work_dispatcher.hpp:233`, correctly moves from
  `other`. The fix makes line 283 match it.
- Callers: `initiate_post.hpp:151,202` and `initiate_dispatch.hpp:137,188` construct `work_dispatcher`. Every
  caller goes through this one template, so one edit covers them all. The other move constructors in the file
  (`work_dispatcher.hpp:94,127,162,193,231`) all move from `other`. The `*this` reads elsewhere in the vendored
  asio were checked: swap in `execution/any_executor.hpp:664` (both sides are constructed) and `operator()`
  calls in `impl/*.hpp` are correct.
- Not verified: whether Link's own call paths in this tree ever pass a legacy (TS) executor. The regression
  test instantiates the specialisation directly, so the bug is proven without that answer.

## Regression test

`tests/src/core/AbletonLinkWorkDispatcherTest.cpp`, registered in `tests/CMakeLists.txt` (inside
`IF(LMMS_HAVE_ABLETON_LINK)`, with `target_link_libraries(... PRIVATE ableton_link)` for the asio include paths
and `ASIO_STANDALONE`), and in `tests/fork-sources.txt` and `tests/all-sources.txt`.

Two test functions, both deterministic (no timing, no sleeps, no threads):

| Test | What it pins |
| --- | --- |
| `movedDispatcherRunsItsFunction` | The function's move must not be a self-move: its source is the dispatcher it is moved from, not the destination under construction. After the move, running the dispatcher delivers 42 to its handler. |
| `movedDispatcherTransfersItsWorkOnce` | The same self-move assertion, and the move transfers one unit of outstanding work (count stays 1); running releases it (count 0); the moved-from source releases nothing. |

The seam, `Payload`'s move constructor (round-2 rework). It compares its source's address with `this` before it reads
anything. A correct move has two distinct objects and reads `other.value`. The buggy move's source is the destination
itself, so the value is replaced by the sentinel `Payload::unreadValue` (-1) without being read, and
`Payload::movedFromSelf` records that it happened. The round-1 version read `other.value` unconditionally, which was
undefined behaviour on the RED path; that is what the round-1 review rejected.

The legacy executor is hand-written and satisfies asio's `is_executor` trait.

Upstream provenance: `tests/upstream-modifications.txt` now records this file's one local deviation from the
vendored asio 8806a68 (the row for `work_dispatcher.hpp`). `MANIFEST.tsv` still records the upstream blob sha,
which is correct for upstream.

## Review round 1 and the rework

The review (`qa/reviews/new-p9-512.r1.md`) asked for one change: detect the self-source before reading the
payload value, and assert against self-source in both regression cases. A QA commit `ca645917` had made that change,
but no branch ref pointed at it: it was a child of this branch's `681e4228eb`, and the branch never advanced to it.
Its QA log (`runs/new-p9-512.r1/qa-logs/qa-fix/test.log`) shows `AbletonLinkWorkDispatcherTest` and
`AbletonLinkInteropTest` both Passed; the rc 8 there comes only from the pre-existing `AbletonLinkSessionTest`
segfault (below). I did not cherry-pick that commit. I made the same change on this branch as `474bc5c263`, with a
named sentinel and comments adjusted to match.

## Evidence (exact commands, EXIT codes)

Round 2 logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-512.r2/logs` (prefix `r2-`).
Round 1 logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-512.r1/logs`, cited with `r1/` below.

### Round 2 (the rework)

| Step | Command (run from the stated directory) | EXIT | Log |
| --- | --- | --- | --- |
| GREEN build | `CCACHE_TEMPDIR=<disk tmp> cmake --build build --target AbletonLinkWorkDispatcherTest -j4` | 0 | `r2-build-1-green.log` |
| **GREEN run** | `cd build/tests && ctest -R AbletonLinkWorkDispatcherTest --output-on-failure` | **0** | `r2-test-1-green.log` |
| GREEN direct | `cd build/tests && ./AbletonLinkWorkDispatcherTest` | 0 (`Totals: 4 passed`: init, 2 tests, cleanup) | `r2-test-2-green-direct.log` |
| 4b: product patch | `git diff 91260338d1^ 91260338d1 -- src/3rdparty/ableton-link/asio/include/asio/detail/work_dispatcher.hpp > r2-fix.patch` | 0 | `r2-fix.patch` |
| 4b: reverse product hunk only | `git apply -R r2-fix.patch` | 0 | - |
| 4b: rebuild reverted | `CCACHE_TEMPDIR=... cmake --build build --target AbletonLinkWorkDispatcherTest -j4` | 0 | `r2-build-2-reverted.log` |
| **4b: RED run (product hunk reversed)** | `cd build/tests && ctest -R AbletonLinkWorkDispatcherTest --output-on-failure` | **8** | `r2-test-3-red-proof.log` |
| 4b: restore product hunk | `git apply r2-fix.patch` | 0 | - |
| 4b: rebuild restored | `CCACHE_TEMPDIR=... cmake --build build --target AbletonLinkWorkDispatcherTest -j4` | 0 | `r2-build-3-restored.log` |
| 4b: GREEN run (restored) | `cd build/tests && ctest -R AbletonLinkWorkDispatcherTest --output-on-failure` | **0** | `r2-test-4-restored.log` |
| Family | `cd build/tests && ctest -R AbletonLink --output-on-failure` | **8** (1 pre-existing failure, below) | `r2-test-5-family.log` |
| Gates (committed tree) | `python3 -I <r1>/logs/gates-driver.py <worktree> <r2>/logs/gates-r2` | **0** | `r2-gates-1.log` |

RED text with the product hunk reversed (`r2-test-3-red-proof.log`): both regression cases FAIL on
`!Payload::movedFromSelf` (test lines 126 and 144), `Totals: 2 passed, 2 failed`.

Family, fixed tree (`r2-test-5-family.log`):

| Test | Result |
| --- | --- |
| AbletonLinkWorkDispatcherTest | Passed (new) |
| AbletonLinkInteropTest | Passed (19.6 s) |
| AbletonLinkSessionTest | **SegFault, pre-existing, not caused by this change** (A/B below) |

Gate table (`r2-gates-1.log`, EXIT 0):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              ratchet exit 1; no new item in your files
  PASS  file-length             ratchet exit 1; no new item in your files
RESULT: PASS
```

The complexity and file-length ratchets exit 1 on the train as expected; the only items they list are not new in
files this change touched.

### Round 1 (the original fix, for the record)

The RED with the unfixed header was `r1/test-1-red.log` (EXIT 8), built by `r1/build-3-red.log` (EXIT 0). The GREEN
was `r1/test-2-green.log` (EXIT 0). The round-1 4b proof was `r1/test-3-red-proof.log` (EXIT 8) and
`r1/test-4-regreen.log` (EXIT 0). The round-1 seam compared addresses but still read `other.value` unconditionally,
which the review rejected as undefined behaviour on the RED path.

### The pre-existing `AbletonLinkSessionTest` segfault

A/B (round 1): with the header fix reverted, rebuilt `AbletonLinkSessionTest` and `zene_ableton_link`
(`r1/build-8-ab-reverted.log` EXIT 0), `ctest -R '^AbletonLinkSessionTest$'` gives EXIT 8 with the same segfault
(`r1/test-7-ab-reverted.log`). The test file changed in round 2 does not touch that test, so the result stands.

Cause (read, not fixed; outside this cluster): `tests/src/core/AbletonLinkSessionTest.cpp:215` calls `QSKIP` inside
the helper `joinBoth()` when this host delivers no multicast. `QSKIP` returns only from that helper, so
`twoParticipantsFormOneSession()` (line 124) goes on to `m_a->numPeers()` with `m_a` still null, which segfaults in
`AbletonLinkSession::numPeers()`. The fix belongs in the test harness; it edits every caller of `joinBoth()`, so it
needs its own cluster.

### Environment notes

- `qwen/feature_check.py` cannot run as written in this lane: it writes its logs to `<worktree>-feature-check`, a
  sibling directory outside the write jail (`r1/gates-1.log`, EXIT 1, `PermissionError`). I ran the same gate
  commands through `gates-driver.py`, which imports `feature_check`'s own gate list and comparison and writes logs
  to the run folder. Its first run, before commit, failed the two source-list gates (`r1/gates-2.log`), because those
  lists are derived from `HEAD`; after commit they pass.
- ccache: the shared `~/.cache/ccache-bunny/ccache.conf` sets `temporary_dir` to a path this sandbox cannot write
  (`dsh/home/ccache-tmp-r6`). I did not edit the shared config. Builds ran with
  `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p9-512`.
- `cmake build` (re-configure) is refused in a lane tree; `cmake --build build --target cmake_check_build_system`
  regenerates the Makefiles.
- The test needs asio's TS executors. The Link build never defines `ASIO_NO_TS_EXECUTORS`; without them there is
  no TS specialisation to move, and the test would not compile (a loud failure, not a silent 0-test run).

## Realtime, undo, commands, data

- Realtime: the change is in a header move constructor on the setup/post path, not on an audio thread. It adds
  no allocation, no lock, no log.
- Undo: not involved.
- Commands: no new control command; `ControlCommandsSnapshot` untouched.
- Saved data: not involved. The bug was undefined behaviour in a move; no caller depended on it.

## Self-check

Answers to the `submit_check.py` items, with the evidence above and in the logs.

- **Gates row: FAIL in `submit_check.py`, for an environment reason, not a gate failure.** `submit_check` runs
  `qwen/feature_check.py`, which writes its logs to `<worktree>-feature-check`, a sibling directory outside this
  lane's write jail. It fails with `PermissionError`. The same gate commands with the same ratchet comparison,
  logged to the run folder, all PASS (`r2-gates-1.log`, EXIT 0). Re-running `feature_check.py` outside the jail is
  needed to turn that row green; I did not work around the jail.
- **The regression test FAILS with the product change reverted and PASSES with it (exit codes):** RED
  `r2-test-3-red-proof.log` EXIT 8 (product hunk reversed, test unchanged); GREEN `r2-test-1-green.log` EXIT 0 and
  `r2-test-4-restored.log` EXIT 0 (after restoring the hunk).
- **Deterministic, with the seam named:** no wall clock, sleep, thread or race. The moved-to dispatcher is
  placement-new'd into a test-owned `alignas` buffer. The seam is `Payload`'s move constructor: it compares
  addresses before it reads anything, and on the self-source path it takes the sentinel `Payload::unreadValue`
  instead of reading the unconstructed value. The assertions check the boolean `movedFromSelf`, never the value the
  bug would read. The outstanding-work count is a plain `int` owned by the test; the legacy executor is hand-written.
- **Fix at the cause, and complete:** the cause is `work_dispatcher.hpp:283`. Callers are `initiate_post.hpp:151,202`
  and `initiate_dispatch.hpp:137,188`, all of which go through this one template. Siblings: the other move
  constructors in the file (`:94,127,162,193,231`) all move from `other`, and the fix matches the non-TS sibling at
  `:233`. The other `*this` reads in the vendored asio are correct (swap in `execution/any_executor.hpp:664`, and the
  `operator()` calls in `impl/*.hpp`).
- **No collateral damage:** the change alters only which object the move reads, which removes undefined behaviour.
  It adds no limit or check, touches no saved data, and changes no defined behaviour. The Link family: with the fix,
  `AbletonLinkInteropTest` and `AbletonLinkWorkDispatcherTest` pass; `AbletonLinkSessionTest` segfaults identically
  with the fix reverted, so its crash predates this change (the `QSKIP` in the helper `joinBoth()`,
  `AbletonLinkSessionTest.cpp:215`, reported above and not changed here).
- **Verdict is FIXED,** so the ALREADY-FIXED / FALSE-POSITIVE evidence rule does not apply. The finding's historical
  line numbers were checked against the file (`.hpp:283` now, not `.h:273`).
- **Working tree:** clean after the rework commit (`git status --porcelain` empty before this report commit).
- **Registration:** `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt` and
  `tests/upstream-modifications.txt` are in the fix commit `91260338d1`.
- **Realtime, undo, commands:** nothing new on an audio path, no command added, no undo involved.
- **Not checked:** whether Link's own paths ever reach the legacy (TS) specialisation; whether upstream asio has since
  fixed this line (this tree's copy has it).
