FIXED

# Cluster new-p14-386

new-p14-386: FIXED

`src/core/AbletonLinkBridge.cpp:211` - `AbletonLinkBridge::commitTempo()` read
`LinkAudioFollower::audioRunning()` (`src/core/LinkAudioFollower.cpp:156-160`, a 200 ms reading of the last
period, `:48`) and, in its next statement, committed the tempo on the application thread with
`AbletonLinkSession::appSetTempo()`. The audio thread can start a period between the two, and Link's own
contract forbids those two commits to overlap. The application commit is now made under a claim the audio
thread honours: `LinkAudioFollower::beginAppCommit()` waits for the period in flight and keeps every later
period out of the session until `endAppCommit()`.

## Verdict: REAL

The finding is a point-in-time check guarding a call that Link documents as exclusive with the audio thread.
The contract, in the vendored library's own words
(`src/3rdparty/ableton-link/link/include/ableton/Link.hpp:66-71`):

> The ability to modify the session state from application threads should only be used in cases where an
> application's audio thread is not actively running [...] Modifying the Link session state from both the
> audio thread and an application thread concurrently is not advised and will potentially lead to unexpected
> behavior.

and the wrapper this repository builds on it restates it at `include/AbletonLinkSession.h:13-19`
("appSetTempo is for when the audio thread is NOT running - Link advises against modifying the session from
an application thread while audio runs"). Before the fix the code was exactly the shape the finding
describes (`src/core/AbletonLinkBridge.cpp:197-202`):

```cpp
	LinkAudioFollower& follower = LinkAudioFollower::instance();
	if (m_enabled && follower.audioRunning(m_session->clockMicros())) { follower.requestTempo(bpm); }
	else { m_session->appSetTempo(bpm); }
```

`audioRunning()` is `m_lastPeriodUs != 0 && nowUs - m_lastPeriodUs < 200000`
(`src/core/LinkAudioFollower.cpp:156-160`). It is false for the whole time before a first period, for up to
200 ms after a device restart, and for the moment the audio callback starts - so the `else` branch can run
while the audio thread is inside `AbletonLinkSession::audioPeriod()`, which is the audio-thread half of the
same pair (`src/core/AbletonLinkSession.cpp:172-185`; `captureAudioSessionState` / `commitAudioSessionState`).

What the overlap costs, from the library's implementation in this tree: the application commit is a
read-modify-write of the whole client state (`AbletonLinkSession::appSetTempo`,
`src/core/AbletonLinkSession.cpp:165-170`), and the audio commit is the same read-modify-write from the other
side (`AbletonLinkSession::audioPeriod`, `:172-185`). Both end in `Controller::setClientState` /
`setClientStateRtSafe` (`src/3rdparty/ableton-link/link/include/ableton/link/Controller.hpp:222-262`, and
`clientStateRtSafe` at `:245-247` is documented "NOT thread-safe [...] must not be called concurrently with
setClientStateRtSafe"). One of the two changes is lost. For this bridge a lost tempo is not self-healing:
the mailbox entry is exchanged away (`LinkAudioFollower::takeChange`,
`src/core/LinkAudioFollower.cpp:283-286`) and `m_committedTempo` has already advanced
(`src/core/AbletonLinkBridge.cpp:211-212`), so the tempo the user set is never pushed again and the peers
keep the old one. That is the finding's scenario: Link is in use while the audio device is stopped (no
period has reported, so `audioRunning()` says "not running"), the device starts, and a local tempo change
commits into the same session from the application thread.

## The fix

`LinkAudioFollower` gained the claim the bridge was missing (`include/LinkAudioFollower.h:137-146`):

- `beginAppCommit()` (`src/core/LinkAudioFollower.cpp:138-149`) stores the claim, then waits for the period
  in flight, the order `attach()` already uses (`:108-123`): a period raises `m_busy` *before* it reads the
  claim (`process`, `:175-190`), so once `m_busy` reads clear after the store no period is inside the
  session, and a period that starts later reads the claim and stays out of it.
- `endAppCommit()` (`:151-154`) clears it. Both stores/loads are `seq_cst` (`:147`, `:153`, `:185`): the
  claim and the `m_busy` handshake form a Dekker pattern, and only a total order over the four operations
  makes "the claim was not seen, so the period raised `m_busy` first" sound. No local TSan on this box, so
  that is the design argument, not a tool result.
- The audio thread never waits: `process()` returns `LinkPeriod::Following` (or `Off` before the first
  engaged period) for a claimed period (`:180-189`). Nothing of the follower's state moves, so the rate and
  play head of the last period still stand and the block renders exactly as it would have; a period that
  skipped a transport edge sees it next period, because `m_seenEdges` is not advanced here either.

`AbletonLinkBridge` routes every application-thread session commit through one path
(`src/core/AbletonLinkBridge.cpp:221-231`, `AppCommitScope` at `:74-86`):

```cpp
void AbletonLinkBridge::commitTempo(double bpm)
{
	m_committedTempo = bpm;
	LinkAudioFollower& follower = LinkAudioFollower::instance();
	if (m_enabled && follower.audioRunning(m_session->clockMicros())) { follower.requestTempo(bpm); return; }
	appCommitTempo(bpm);
}

void AbletonLinkBridge::appCommitTempo(double bpm)
{
	const AppCommitScope claim(LinkAudioFollower::instance());   // beginAppCommit / ~ -> endAppCommit
	m_session->appSetTempo(bpm);
}
```

The `audioRunning()` fast path is unchanged: while the audio thread runs, the commit still goes through the
mailbox and no claim is taken (`src/core/LinkAudioFollower.cpp:286`). `AppCommitScope` is RAII so a throwing
commit cannot leave the claim set (it would mute Link sync for the rest of the session).

## Callers and siblings checked

- `commitTempo()` callers: `declareTempo()` (`link.set_session_tempo`,
  `src/core/AbletonLinkBridge.cpp:157-166`) and `pushLocalTempo()` (`service()`,
  `src/core/AbletonLinkBridge.cpp:183-190`). Both run on the control thread: `LinkSyncEngine` drives
  `service()` from a `QTimer` (`src/core/LinkSync.cpp:157-164`, `:263-266`), and `declareTempo` comes from
  `LinkSyncEngine::setSessionTempo` (`src/core/LinkSync.cpp:234-243`).
- The other application-thread session write, `setEnabled()`'s initial tempo
  (`src/core/AbletonLinkBridge.cpp:129`), now goes through the same helper. It cannot actually contend
  today (the follower is disengaged until `:141`, so no period can be inside the session), so the wait is
  free there; routing it keeps one path for "the application thread modifies the session".
- Application-thread *captures* (`fill`, `sessionTempo`, `service`) are unchanged: `appCapture` is
  documented thread-safe (`src/3rdparty/ableton-link/link/include/ableton/Link.hpp:197-212`).
- The wait loop is the existing `pauseAttachWait()` (`src/core/LinkAudioFollower.cpp:125-136`), also used by
  `attach()`; the test seam's `attachWait` doc now says it serves either control-thread wait
  (`include/LinkAudioFollower.h:97-101`). The seam itself is untouched and still unset in the product.
- Nothing else writes the session from the control side, and no control command was added or changed.

## Test matrix

| Path | Case | Where | Result |
| --- | --- | --- | --- |
| failure (the finding) | a period is inside the session while `audioRunning()` says "not running"; the application commit must not land until the period is out | `anApplicationTempoCommitWaitsForThePeriodInFlight` (new, `tests/src/core/LinkPhaseLockTest.cpp:359`) | fails without the fix (ctest exit 8), passes with it (exit 0) |
| normal (the claim's other side) | a period that starts while the claim is held skips the session and moves no follower state; the next period carries on | `anApplicationCommitKeepsTheNextPeriodOutOfTheSession` (new, `:440`) | passes with the fix; fails with it reverted (claimed periods 70 vs 69) |
| normal (stopped device) | no period ever runs: the commit must reach the session at once at the exact tempo, not become a mailbox request only a period could deliver | `anApplicationTempoCommitLandsWhileNoPeriodRuns` (new, `:407`) | passes before and after (guards the path the finding names) |
| normal | the follower's whole period path still runs 64 periods with a tempo mailbox, a start edge, a hold and a jump, 0 allocations | `theFollowerPeriodDoesNotAllocate` (existing) | passes, `allocations=0 over 64 periods; periods 64, holds 0, jumps 1` |
| exhaustion | 64 periods of every branch with the claim check in the path | same test | passes, allocation count unchanged at 0 |
| reconfiguration | enable/disable Link, destroy the bridge, re-attach a session | `replacingASessionWaitsForThePeriodUsingIt`, `attachingTheSameSessionAgainDoesNotWait` (existing) | pass |
| audio thread | one `seq_cst` load and a branch per engaged period (`src/core/LinkAudioFollower.cpp:185-189`): no allocation, free, lock, wait or log | `theFollowerPeriodDoesNotAllocate` | passes, 0 allocations |
| platform | the new code is `<atomic>` and plain C++ only, no POSIX, no Qt 6-only API, no new session API; the test uses the QTest + `std::thread` harness the file already had | this host's build logs (Link build, QT 6.12) | passes; the `WANT_ABLETON_LINK=OFF` stub build was not rebuilt here (argument, not a measurement: `AbletonLinkSessionStub.cpp:75` implements the same `appSetTempo`, and no new session symbol is called) |

## Commands and exit codes

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-386`; logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-386.r1/logs`. Every build and test was started from the
worktree, tests from `build/tests`.

| Command | Exit | Log |
| --- | --- | --- |
| `cmake --build build --target LinkPhaseLockTest -j4` (baseline, before any edit) | 0 | build-1-baseline.log |
| `ctest -R '^LinkPhaseLockTest$' --output-on-failure` (baseline) | 0 | test-1-baseline.log |
| `cmake --build build --target LinkPhaseLockTest -j4` (tests added, fix absent) | 0 | build-2-red.log |
| `ctest -R '^LinkPhaseLockTest$' --output-on-failure` (RED) | **8** | test-2-red.log |
| `cmake --build build --target LinkPhaseLockTest -j4` (fix in) | 0 | build-3-green.log |
| `ctest -R '^LinkPhaseLockTest$' --output-on-failure` (GREEN) | **0** | test-3-green.log |
| `./LinkPhaseLockTest` five times in a row (determinism) | 0, 0, 0, 0, 0 | test-repeat-1..5.log |
| `git add <the 4 product files>; git diff --cached -- <4> > logs/fix-full.patch` (the whole product change) | 0 | fix-full.patch |
| `git diff -- <4> > logs/fix.patch` (the behavioural hunks, base = the staged fix) | 0 | fix.patch |
| `cmake --build build --target LinkPhaseLockTest -j4` (fix reverted) | 0 | build-4-red-final.log |
| `ctest -R '^LinkPhaseLockTest$' --output-on-failure` (fix reverted) | **8** | test-5-red-final.log |
| `git apply -R logs/fix.patch` | 0 | - |
| `cmake --build build --target LinkPhaseLockTest -j4` (fix back) | 0 | build-5-green-final.log |
| `ctest -R '^LinkPhaseLockTest$' --output-on-failure` (fix back) | **0** | test-6-green-final.log |
| `cmake --build build --target zene -j4` | 0 | build-6-zene.log |
| `cmake --build build --target AbletonLinkSessionTest AbletonLinkInteropTest AbletonLinkAllocatorTest AbletonLinkWorkDispatcherTest AbletonLinkEsp32RunnerTest AbletonLinkContextStopTest LinkAudioCodecTest LinkAudioStreamTest ControlLinkCommandsTest -j4` | 0 | build-7-linktests.log |
| `ctest -R '^(LinkPhaseLockTest\|AbletonLink.*\|LinkAudio.*\|ControlLinkCommandsTest)$' --output-on-failure` | 8 (9 of 10 pass; `AbletonLinkSessionTest` SEGFAULT) | test-7-linkfamily.log |
| `git apply logs/fix.patch` (fix reverted again, for the SEGFAULT question below) | 0 | - |
| `cmake --build build --target AbletonLinkSessionTest -j4` | 0 | build-8-linksession-red.log |
| `ctest -R '^AbletonLinkSessionTest$' --output-on-failure` (this fix reverted) | **8** (same SEGFAULT) | test-9-linksession-red.log |
| `git apply -R logs/fix.patch; cmake --build build --target zene LinkPhaseLockTest AbletonLinkSessionTest -j4` | 0 / 0 | build-9-restore.log |
| `ctest -R '^LinkPhaseLockTest$' --output-on-failure` (restored) | **0** | test-10-restore.log |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (fix + tests) | **0** | gates-1.log |
| same, after this report was written (6 changed files) | **0** | gates-2.log |

The RED output (test-5-red-final.log), the product change reverted:

```
FAIL!  : LinkPhaseLockTest::anApplicationTempoCommitWaitsForThePeriodInFlight() '!periodStillRunning' returned FALSE.
(declareTempo() committed to the session from the application thread while the audio thread was inside it)
FAIL!  : LinkPhaseLockTest::anApplicationCommitKeepsTheNextPeriodOutOfTheSession() Compared values are not the same
   Actual   (claimed.periods): 70
   Expected (before.periods) : 69
Totals: 12 passed, 2 failed, 0 skipped, 0 blacklisted, 849ms
```

Gates (`gates-2.log`, the final state):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Note on `fix.patch`. It carries the two behavioural hunks only - `commitTempo()` back to its
`if (...) { requestTempo } else { appSetTempo }` form, and `process()` without the claim check - and it was
produced with the fixed product files staged, so the patch transforms the fixed worktree into the pre-fix
behaviour. The claim's API (`beginAppCommit`/`endAppCommit`, `m_appCommit`, the RAII scope) is present in
both the red and the green build, because the regression test does not compile without it; reversing it
would fail the build instead of failing the test. This is the same shape cluster-new-p9-220 used for its
seam. One command in the table is recorded because it failed: reversing `fix.patch` a second time
(`git apply -R`) exits 1 with "patch does not apply" when the worktree is already fixed, which is why the
revert for the SEGFAULT check above used `git apply` (forward).

## Self-check

- **The regression test fails with the product change reverted and passes with it.** Reverted:
  `git apply -R logs/fix.patch` (exit 0), rebuild (exit 0), `ctest -R '^LinkPhaseLockTest$'` -> exit **8**,
  `!periodStillRunning` false and `claimed.periods 70 != 69`. Restored: `git apply logs/fix.patch` (exit 0),
  rebuild (exit 0), same test -> exit **0**, 14 passed. Logs build-4/test-5 and build-5/test-6.
- **The test is deterministic.** No wall clock, no sleep, no `QTest::qWait`, no retry. Three seams decide the
  order: `LinkFollowerSeam::inPeriod` parks one period inside the follower on its own thread,
  `LinkFollowerSeam::attachWait` runs on the control thread inside the claim's wait loop, and the two
  threads exchange only atomics (`AttachGate`, `tests/src/core/LinkPhaseLockTest.cpp:93-119`). A follower
  that does not wait leaves the parked period in place and the assertion sees it; a follower that does wait
  is released only by its own wait loop. The second test's period is joined, so its outcome is also a
  program order. Five consecutive runs: exit 0 each (test-repeat-1..5.log).
- **The fix is at the cause and complete.** The cause is that a reading (`audioRunning()`) was used as a
  lock; the fix replaces the reading with a claim the audio thread honours, in the one place both commits
  meet (`LinkAudioFollower::process`, `src/core/LinkAudioFollower.cpp:175-190`) and in the one helper every
  application-thread session write now goes through (`AbletonLinkBridge::appCommitTempo`,
  `src/core/AbletonLinkBridge.cpp:221-231`). Every caller is listed above; the mailbox fast path, the
  `attach()` wait and the seam are unchanged.
- **No collateral damage.** Nothing on the save/load path is touched: the claim is runtime state only, no
  model, file format, undo step or command changed, so old projects load and render as before. No limit or
  check was added that other callers could exceed - the claim is taken for the length of one
  `appSetTempo()` call, and the path that legitimately has nothing to wait for (no period in flight, the
  audio device stopped) returns at once: `anApplicationTempoCommitLandsWhileNoPeriodRuns` pins exactly that
  and passes before and after the fix. Behaviour outside the bug is unchanged: the enabled/audio-running
  path still posts to the mailbox (`src/core/AbletonLinkBridge.cpp:217`) and the follower's `Hold`,
  `Released`, re-lock and envelope behaviour are untouched.
- **Realtime.** The only new audio-thread code is `src/core/LinkAudioFollower.cpp:180-189`: one `seq_cst`
  load and a branch per engaged period, returning `Following`/`Off`. No allocation, free, lock, wait, log
  or Qt call; `theFollowerPeriodDoesNotAllocate` still reports `allocations=0 over 64 periods`
  (test-repeat-1.log). `beginAppCommit`'s wait and the RAII scope run on the control thread only.
- **Undo and commands.** No command added, no undo step: the claim wraps a session write that was already
  outside the undo system (a declared session tempo is not a Song edit), so undo is unchanged and the
  `ControlCommandsSnapshot` canary does not move.
- **Sibling commands.** This commit changes no command file: `git show --stat HEAD` lists
  `docs/qwen-fixes-2026-10/cluster-new-p14-386.md`, `include/AbletonLinkBridge.h`,
  `include/LinkAudioFollower.h`, `src/core/AbletonLinkBridge.cpp`, `src/core/LinkAudioFollower.cpp` and
  `tests/src/core/LinkPhaseLockTest.cpp`, and nothing under `src/core/ControlCommands*`,
  `ControlReversibilityTable*` or `ControlRegistry*`. `link.set_session_tempo` still reaches the same
  handler and returns the same values; only the thread hand-off inside the bridge changed.
- **Registration.** All five changed files are already listed: `tests/fork-sources.txt:815`
  (`include/AbletonLinkBridge.h`), `:1210` (`include/LinkAudioFollower.h`), `:1642`
  (`src/core/AbletonLinkBridge.cpp`), `:2109` (`src/core/LinkAudioFollower.cpp`), `:2869`
  (`tests/src/core/LinkPhaseLockTest.cpp`); `tests/all-sources.txt` carries the same five. No file was
  added, so no list changed, and `fork-sources` / `all-sources-reproduce` pass.
- **The `ALREADY-FIXED` / `FALSE-POSITIVE` question** - not claimed. The verdict is FIXED; the pre-fix code
  and the library contract are quoted above with file:line.
- **State sequence / GUI screenshot** - not applicable: no changed file matches `src/core/Song*`,
  `src/core/*Record*`, `src/core/*Undo*`, `src/core/*Transport*`, `src/core/Take*` or
  `src/core/AudioEngine*`, and nothing under `src/gui` changed.

## Other findings

- `AbletonLinkSessionTest` SEGFAULTs on this host, and it is not from this change: `git`-reverted and
  rebuilt with the pre-fix behaviour it still exits 8 with the same SIGSEGV in
  `AbletonLinkSession::numPeers()` (`test-9-linksession-red.log`; with this change in,
  `test-7-linkfamily.log`). `twoParticipantsFormOneSession` QSKIPs because this host delivers no multicast
  datagram on 224.76.78.75:20808, and a later test then dereferences the never-created session. Earlier
  reports on this box record the same failure (`docs/qwen-fixes-2026-10/cluster-new-p9-220.md`,
  `cluster-new-p15-195.md`, `cluster-4926-port.md`).
- `tests/rt-safety-scope.txt:37` declares `LinkAudioFollower::instance`, not `LinkAudioFollower::process`,
  so the whole-tree sweep does not measure the per-period code this fix touches (nor the code that was
  already there). The allocation count in `LinkPhaseLockTest` is what covers it. Widening the declared
  scope is a gate change, so it is recorded here instead of made.
- `AbletonLinkBridge` still has no path that re-publishes a session to the follower, so two live bridges
  would leave the follower on the newer session while the older bridge's destructor withdraws it
  (`src/core/AbletonLinkBridge.cpp:102`). A question for whoever adds a second bridge; unchanged here.
- `LinkAudioFollower::beginAppCommit()` is documented control-thread-only and non-nesting. It is not
  enforced (an audio-thread call would wait on itself); the only caller is one control-thread helper.

## Files committed

- `src/core/AbletonLinkBridge.cpp` - `appCommitTempo()`, `AppCommitScope`, `commitTempo()` takes the claim,
  `setEnabled()` uses the same path.
- `include/AbletonLinkBridge.h` - the declaration and the class comment.
- `src/core/LinkAudioFollower.cpp` - `beginAppCommit()`, `endAppCommit()`, the claim check in `process()`.
- `include/LinkAudioFollower.h` - the claim's contract, `m_appCommit`, the updated wait/seam comments.
- `tests/src/core/LinkPhaseLockTest.cpp` - `anApplicationTempoCommitWaitsForThePeriodInFlight`,
  `anApplicationCommitKeepsTheNextPeriodOutOfTheSession`,
  `anApplicationTempoCommitLandsWhileNoPeriodRuns`; the seam helper's comment now covers both wait loops.
- `docs/qwen-fixes-2026-10/cluster-new-p14-386.md` - this report.
