FIXED

# Cluster new-p9-220

new-p9-220: FIXED

`src/core/LinkAudioFollower.cpp:108` - `LinkAudioFollower::attach()` published a new session and returned
at once when the pointer was not null, so the caller's previous session could still be inside an audio
period. It now waits, for a replacement exactly as it already did for a withdrawal.

## Cause

Before the fix (`src/core/LinkAudioFollower.cpp:108-114`):

```cpp
void LinkAudioFollower::attach(AbletonLinkSession* session)
{
	m_session.store(session);
	if (session != nullptr) { return; }
	while (m_busy.load()) { std::this_thread::yield(); }
}
```

The store publishes the new pointer. A period that loaded the OLD pointer before that store is still
using it (the pointer load and every use after it happen while `m_busy` is raised, `BusyScope` at
`src/core/LinkAudioFollower.cpp:57-59`, entered at `:160-161`). For `nullptr` the code waited for that
period to let go; for a different session it returned immediately and gave the caller no point at which
the session it replaced was free to destroy - `attach()` is the only place that tells the caller when a
session is out of the audio thread, and the header's promise was tied to the withdrawal case only
(`include/LinkAudioFollower.h:95-97` before this change).

So `attach(A)`, then `attach(B)`, then destroying A is legal API use that frees memory a period may still
dereference. Today's one in-tree caller cannot hit it: `src/core/AbletonLinkBridge.cpp:78` publishes in
its constructor and `:87` withdraws with `attach(nullptr)` before `m_session` is destroyed (`:88-89`), so
the two calls are ordered. The defect is the hole in the exported API contract, and the fix closes it
rather than relying on that ordering forever.

## Fix

`src/core/LinkAudioFollower.cpp:108-123`:

- `:116` - `AbletonLinkSession* const previous = m_session.exchange(session);` publishes first.
- `:117-121` - nothing to wait for when no session was published before, or when the same session is
  published again (behaviour unchanged).
- `:122` - otherwise `while (m_busy.load()) { pauseAttachWait(); }`, the wait a withdrawal already made,
  so a replacement returns only once no period can still hold the session it replaced.

Why the wait is sound (local TSan does not run on this box, so this is the design argument, not a tool
result): the audio thread raises `m_busy` *before* it loads `m_session` (`src/core/LinkAudioFollower.cpp:160-162`).
Control stores the pointer first and polls the flag afterwards. A poll that reads `false` therefore means
every period that raised the flag before the store has already ended - and a period that raises it after
the store loads the new pointer. That is the invariant `attach(nullptr)` already relied on; the fix
applies the same invariant to replacement. `:153` and `:156` are the two other conditions of that
argument (the engaged test is a release/acquire load, `BusyScope` clears the flag with a release store).

Siblings and callers checked:

- `src/core/AbletonLinkBridge.cpp:78` (publish) and `:87` (withdraw) - the only in-tree callers; the
  destructor's ordering is unchanged and its wait still happens. `:88-89` destroys the session after it.
- `AbletonLinkBridge::setEnabled(false)` (`src/core/AbletonLinkBridge.cpp:95-101`) disengages without
  withdrawing the session - no replacement, so it is unaffected, and it must stay that way.
- No control command attaches a session: `rg -n 'attach\(' src include tests` finds only the two bridge
  lines, the follower itself and the test below. Nothing is added to the command surface.
- Re-attach of the same pointer, and a first publish with nothing before it, still return without a wait.

## Test matrix

| Path | Case | Where | Result |
| --- | --- | --- | --- |
| normal | publish the first session, no period running | `LinkPhaseLockTest::theFollowerPeriodDoesNotAllocate` (existing) | pass |
| normal | re-publish the SAME session while a period holds it | `attachingTheSameSessionAgainDoesNotWait` (new) | pass |
| failure (the finding) | replace a session while a period is inside it | `replacingASessionWaitsForThePeriodUsingIt` (new) | fails without the fix (exit 8), passes with it (exit 0) |
| withdrawal | `attach(nullptr)` while a period is in flight | existing smoke in `theFollowerPeriodDoesNotAllocate`, unchanged code path | pass |
| exhaustion | 64 periods, tempo mailbox, seek, hold, jump: 0 allocations | `theFollowerPeriodDoesNotAllocate` (existing) | pass, `allocations=0` |
| reconfiguration | disable/enable Ableton Link, destroy the bridge | not driven by a test; the call ordering in the bridge is unchanged and the withdraw path is the pre-existing one | reviewed |
| audio thread | the only new audio-thread code is one acquire load of the seam plus two null tests per engaged period (`src/core/LinkAudioFollower.cpp:163-166`): no allocation, lock, wait, log or Qt call | `theFollowerPeriodDoesNotAllocate` | pass |
| platform | `std::atomic` and a plain function-pointer seam, no POSIX API, no Qt 6-only API; builds with the same flags as the rest of the fork (`std::thread`/`std::atomic` already used in this file) | build logs below | pass |

## The seam the test needs

The follower gained an inert test seam (`include/LinkAudioFollower.h:88-102`, `:140`, `:165`), the same
shape the wasm sandbox already uses for its seams (`src/wasm/WasmWorker.h:137-158`: a function pointer and
a context, notified at a named point, a no-op when unset):

- `inPeriod` runs on the audio thread inside an engaged period, after the pointer is loaded and while it
  is held, so a test can keep one period in flight at a point the test chooses;
- `attachWait` runs on the control thread once per poll of `attach()`'s wait loop, in place of the yield,
  so a test can see the wait and release the period it is waiting for.

The product never sets it. It adds one acquire load per engaged period and nothing else; allocation
count over 64 periods is still 0.

## Regression test

`tests/src/core/LinkPhaseLockTest.cpp:250` - `replacingASessionWaitsForThePeriodUsingIt()`:
the follower is engaged with session `first`; a period runs on its own thread and the `inPeriod` seam
parks it inside the follower, holding `first`; the test then calls `attach(&second)`. Only `attach()`'s
own wait loop releases the parked period (`attachWait`), and the test asserts, after `attach()` returns,
that the period was no longer running. A follower that does not wait returns into the live period and
the first assertion fails. No sleeps and no clock: the two threads only exchange atomics, and the order
of events is decided by which code the follower runs.

`tests/src/core/LinkPhaseLockTest.cpp:298` - `attachingTheSameSessionAgainDoesNotWait()`: the same parked
period, `attach()` with the session that is already published, and the period must still be in flight
when it returns (this holds before and after the fix; it guards the wait from spreading to a re-attach).

## Commands and exit codes

All from the worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-220`;
logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-220.r1/logs`.

| Command | Exit | Log |
| --- | --- | --- |
| `cmake --build build --target LinkPhaseLockTest -j4` (baseline, before any edit) | 0 | build-1-baseline.log |
| `cd build/tests && ctest -R '^LinkPhaseLockTest$' --output-on-failure` (baseline) | 0 | test-1-baseline.log |
| `git diff -R -- src/core/LinkAudioFollower.cpp include/LinkAudioFollower.h > logs/fix.patch` (the fix hunk, see the note below) | 0 | fix.patch |
| `git apply -R logs/fix.patch` | 0 | - |
| `cmake --build build --target LinkPhaseLockTest -j4` (fix absent) | 0 | build-13-red-final.log |
| `cd build/tests && ctest -R '^LinkPhaseLockTest$' --output-on-failure` (fix absent) | **8** | test-13-red-final.log |
| `git apply logs/fix.patch` | 0 | - |
| `cmake --build build --target LinkPhaseLockTest -j4` (fix present) | 0 | build-14-green-final.log |
| `cd build/tests && ctest -R '^LinkPhaseLockTest$' --output-on-failure` (fix present) | **0** | test-14-green-final.log |
| `./LinkPhaseLockTest` five times in a row (determinism) | 0, 0, 0, 0, 0 | test-repeat-1..5.log |
| `cmake --build build --target zene -j4` | 0 | build-7-zene.log |
| `cmake --build build --target AbletonLinkSessionTest AbletonLinkInteropTest AbletonLinkAllocatorTest AbletonLinkWorkDispatcherTest AbletonLinkEsp32RunnerTest LinkAudioCodecTest LinkAudioStreamTest ControlLinkCommandsTest -j4` | 0 | build-8-linktests.log |
| `cd build/tests && ctest -R '^(LinkPhaseLockTest\|AbletonLinkSessionTest\|AbletonLinkInteropTest\|AbletonLinkAllocatorTest\|AbletonLinkWorkDispatcherTest\|AbletonLinkEsp32RunnerTest\|LinkAudioCodecTest\|LinkAudioStreamTest\|ControlLinkCommandsTest)$' --output-on-failure` | 8 (8 of 9 pass; `AbletonLinkSessionTest` SEGFAULT, see below) | test-8-linkfamily.log |
| `git apply -R logs/fix.patch` then rebuild `AbletonLinkSessionTest` then `ctest -R '^AbletonLinkSessionTest$'` (is that segfault mine?) | 0 / 0 / **8** - same SEGFAULT without the fix | test-9-reverse-linksession.log |
| `git apply logs/fix.patch` then `cmake --build build --target zene LinkPhaseLockTest AbletonLinkSessionTest -j4` then `ctest -R '^LinkPhaseLockTest$'` | 0 / 0 / 0 | build-10-restore.log, test-10-restore.log |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (final state) | **0** | gates-2.log |

The RED run's output (test-13-red-final.log):

```
FAIL!  : LinkPhaseLockTest::replacingASessionWaitsForThePeriodUsingIt() '!periodStillRunning' returned FALSE.
(attach(&second) returned while the audio thread was still inside the session it replaced)
Totals: 10 passed, 1 failed, 0 skipped, 0 blacklisted, 656ms
```

Note on `fix.patch`: it carries only the `attach()` hunk, not the whole product diff. The seam
(`include/LinkAudioFollower.h`, `pauseAttachWait`, the `inPeriod` notification) is product code too, but it
is inert and the test does not compile without it, so reversing it would fail the build instead of failing
the test. The patch was produced with the fixed files staged and the `attach()` body reverted in the
worktree, i.e. `git diff -R` of exactly the behavioural hunk; the seam is present in both the red and the
green build. `git apply -R` / `git apply` of that patch are the two commands above.

Gates (`gates-2.log`):

```
  PASS  fork-sources              PASS  all-sources-reproduce   PASS  no-upstream-regression
  PASS  check-namespace           PASS  no-tautology            PASS  doc-figures
  PASS  complexity    (no new item in your files)   PASS  file-length   (no new item in your files)
RESULT: PASS
```

## Self-check

- **The regression test fails with the product change reverted and passes with it.** Reverted:
  `git apply -R logs/fix.patch` (exit 0), rebuild (exit 0), `ctest -R '^LinkPhaseLockTest$'` -> exit **8**,
  `!periodStillRunning` false. Restored: `git apply logs/fix.patch` (exit 0), rebuild (exit 0), same test ->
  exit **0**, 11 passed. Logs build-13/test-13 and build-14/test-14.
- **The test is deterministic.** No wall-clock, no sleep, no timeout, no `QTest::qWait`. The seam
  `LinkFollowerSeam` (`include/LinkAudioFollower.h:88-102`) parks one period and is released only from
  `attach()`'s wait loop; the two threads exchange only atomics, and the asserted order is decided by
  which code ran (a period that is still parked when `attach()` returned means no wait happened at all -
  nothing else in the test can release it). Five consecutive runs: exit 0 each (test-repeat-1..5.log).
- **The fix is at the cause and complete.** The wait is in `attach()` itself
  (`src/core/LinkAudioFollower.cpp:108-123`), the one place that publishes or withdraws a session, so every
  future caller is covered; the two in-tree callers (`src/core/AbletonLinkBridge.cpp:78`, `:87`) still get
  exactly the behaviour they relied on; a re-attach of the same pointer and a first publish still return
  without waiting (guard test above); no command surface changed, so the `ControlCommandsSnapshot` canary
  is untouched.
- **No collateral damage.** No file-format, model or save/load code is touched - the session pointer is
  runtime state only, so old projects load and render as before, and nothing new is saved. The wait is
  bounded by one audio period and only happens when a session is actually replaced or withdrawn; the
  paths that legitimately have nothing to wait for (first publish, same pointer) are not delayed.
- **Realtime.** The only new audio-thread code is `src/core/LinkAudioFollower.cpp:163-166`: one
  `m_seam` acquire load and two null tests per engaged period. No allocation, no free, no lock, no wait,
  no log on that thread - `theFollowerPeriodDoesNotAllocate` still reports `allocations=0 over 64
  periods` (test-12-functions.log). `pauseAttachWait()` runs on the control thread only, from `attach()`.
  Nothing is added to `tests/rt-safety-scope.txt` because no new symbol on the audio path is introduced.
- **Undo and commands.** No command added, no undo step: `attach()` is not reachable from the undo
  system or from automation.
- **Real user path and agent path.** User path: enabling Ableton Link constructs the bridge and publishes
  its session (`AbletonLinkBridge::AbletonLinkBridge`), disabling keeps it published but disengaged, and
  destroying the bridge/Song withdraws it with `attach(nullptr)` before the session is destroyed - all
  unchanged, and the withdraw is the case the wait already covered, now shared with replacement. Agent
  path: `link.set_enabled` -> `LinkSyncEngine::setEnabled` -> `AbletonLinkBridge::setEnabled`; it never
  attaches a session, so nothing on the agent surface changes. Edge cases tried: nothing published yet
  (no wait), the same session again (no wait), withdrawal with a period in flight (pre-existing wait),
  replacement with a period in flight (the new wait).
- **ALREADY-FIXED / FALSE-POSITIVE evidence** - not claimed; the verdict is FIXED.
- **Sibling commands** - this cluster's commit changed no command file: `git show --stat HEAD` lists
  `docs/qwen-fixes-2026-10/cluster-new-p9-220.md`, `include/LinkAudioFollower.h`,
  `src/core/LinkAudioFollower.cpp` and `tests/src/core/LinkPhaseLockTest.cpp`, and nothing under
  `src/core/ControlCommands*`, `ControlReversibilityTable*` or `ControlRegistry*`. The sibling ids the
  pre-submit check prints (`app.about` ... `arrangement.get_state`) come from the branch-wide
  `base..HEAD` diff, which carries 432 files from the train this branch was cut from, not from this
  commit; no command was added, changed or reclassified here, so no sibling command is affected and the
  `ControlCommandsSnapshot` canary does not move.

## Other findings

- `AbletonLinkSessionTest` SEGFAULTs on this host (`test-8-linkfamily.log`, and identically with this fix
  reversed in `test-9-reverse-linksession.log`): `twoParticipantsFormOneSession` QSKIPs because this host
  delivers no multicast datagram on 224.76.78.75:20808, and the run then dies in
  `AbletonLinkSession::numPeers()` with `this == nullptr`, so a test that runs after the skip dereferences
  the never-created session. Not touched here (the test does not include or call `LinkAudioFollower` at
  all); earlier reports on this box record the same pre-existing failure
  (`docs/qwen-fixes-2026-10/cluster-new-p15-195.md:115`, `cluster-4926-port.md:215`).
- `AbletonLinkBridge` has no path that re-publishes a session, so two live bridges would leave the
  follower pointing at the newer session while the older bridge's destructor withdraws it
  (`src/core/AbletonLinkBridge.cpp:87`). That is a logic question for whoever adds a second bridge, not
  this memory-safety finding; recorded, not changed.

## Files committed

- `src/core/LinkAudioFollower.cpp` - `attach()` waits for the session it replaces; `pauseAttachWait()`; the
  `inPeriod` seam notification.
- `include/LinkAudioFollower.h` - the `attach()` contract, `LinkFollowerSeam`, `setTestSeam()`,
  `pauseAttachWait()`, `m_seam`.
- `tests/src/core/LinkPhaseLockTest.cpp` - `replacingASessionWaitsForThePeriodUsingIt()`,
  `attachingTheSameSessionAgainDoesNotWait()`, the `AttachGate` seam handles.
- `docs/qwen-fixes-2026-10/cluster-new-p9-220.md` - this report.

Both changed product files are already registered (`tests/fork-sources.txt:1199`, `:2097`) and the test file
too (`tests/fork-sources.txt:2852`), so no manifest changed.
