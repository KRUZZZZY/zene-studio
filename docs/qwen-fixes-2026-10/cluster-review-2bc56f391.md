# Cluster review-2bc56f391: a cancelled hardware insert ping can be revived by the audio thread

Verdict: **FIXED**

## The finding

> `cancelPing` performs Idle store outside engine change guard and races audio
> Requested->Waiting. Undo cancel can be overwritten by stale audio transition and
> measurement revived. Need bounded realtime-safe state-machine CAS or ownership
> coordination and deterministic raced transition regression; no audio
> allocation/locking/unbounded growth.

Two claims, and they have different answers on this branch:

1. *"`cancelPing` performs Idle store outside engine change guard"* - **already
   addressed** before this session, by commit `cdb96259f` ("fix(rework
   2bc56f391): the ping undo called the cancel outside the change lock"). The undo
   path now takes the change lock around the snapshot, the cancel and the settings
   restore (`src/core/ControlCommandsHardwareInsert.cpp:65-76`, the cancel at
   `src/core/ControlCommandsHardwareInsert.cpp:73`). That rework commit says so
   itself: *"Known gap: no failing-first regression test exists for this."*
2. *"races audio Requested->Waiting ... the undo cancel can be overwritten by stale
   audio transition and measurement revived"* - **still true of the state machine
   itself**, and that is what this commit fixes. The ping's state was plain
   stores: `cancelPing()` stored `Idle` and the audio thread's period stored
   `Waiting`/`Done` blind, so nothing but the caller's lock discipline kept a
   period that had already read the request from putting the ping back. The
   public API did not ask for that discipline (`include/HardwareInsert.h:104-113`
   said only "Interface thread"), and the sibling of the cancel, `requestPing()`,
   is called **without** the change lock
   (`src/core/ControlCommandsHardwareInsert.cpp:146`).

The verdict is therefore FIXED, not ALREADY-FIXED: the reviewed defect (a ping
that comes back after it was called off, and the measurement it lands) was still
reachable in the state machine, and the reviewer's own requirement - a bounded
realtime-safe compare-and-swap **and** a deterministic raced-transition
regression - was not met.

## The fix

`src/core/HardwareInsert.cpp` and `include/HardwareInsert.h`:

* The ping's state and the figure it measured live in **one** atomic word
  (`include/HardwareInsert.h:137-157`: `PingWord`, `packPing`, `pingStateOf`,
  `measuredOf`; the member at `include/HardwareInsert.h:188-189`). One word means
  a state change and the figure it carries are published together, so a ping that
  was called off cannot publish its answer either - that was the "measurement
  revived" half of the finding.
* Every move on that word is a compare-and-swap against the word the mover read:
  * `movePingTo()` (`src/core/HardwareInsert.cpp:117-132`) - the interface
    thread's moves: `requestPing()` -> `Requested`
    (`src/core/HardwareInsert.cpp:107-110`), `cancelPing()` -> `Idle`
    (`src/core/HardwareInsert.cpp:112-115`), and the "switched off" move in
    `setSettings()` (`src/core/HardwareInsert.cpp:97`).
  * `claimPing()` (`src/core/HardwareInsert.cpp:226-244`) - the audio thread
    claims the request (`Requested` -> `Waiting`) and the click goes out.
  * `publishPingResult()` (`src/core/HardwareInsert.cpp:246-261`) - the audio
    thread hears the click and publishes the figure (`Waiting` -> `Done`).
  * `failPing()` (`src/core/HardwareInsert.cpp:263-278`) - the audio thread gives
    up (`Waiting` -> `Failed`).
  * `setMeasured()` (`src/core/HardwareInsert.cpp:134-148`) - the interface
    thread's figure (the settings the project carries), the state untouched.
* A move whose word is stale - the ping was called off in the meantime - does
  not happen: the swap fails and the state stays where the cancel put it.
* The loops are bounded: the audio thread moves the ping on twice a period at
  most (a claim and an answer), so its loops run at most two swaps
  (`src/core/HardwareInsert.cpp:231`, `251`, `267`) and the interface thread's
  at most eight (`src/core/HardwareInsert.cpp:124`, `140`). Nothing on the audio
  path allocates, frees, locks, waits, logs or emits a signal: the only
  operations are `std::atomic<int64_t>` loads and compare-and-swaps, and the word
  is asserted lock-free (`src/core/HardwareInsert.cpp:41-43`).
* The undo path is unchanged: still one honest undo step, still under the change
  lock, no new control command.

Behaviour outside the bug is unchanged: the same five states, the same figure,
the same settings, the same saved project. The only difference in the race
window is that the cancel now wins - which is the point.

## The regression

`tests/src/core/HardwareIoTest.cpp:603`
`HardwareIoTest::aCancelledPingIsNotRevivedByAPeriodThatHadReadTheRequest`.

The test is deterministic: no thread, no clock, no sleep. It drives the race
through the state machine's own seam - the audio thread's moves take the word
the period read, so the test can play the exact interleaving the finding
describes:

1. the period reads the request (`insert.pingWord()`),
2. the interface thread cancels it (`insert.cancelPing()`),
3. the period makes its move anyway (`insert.claimPing(requested)`),
4. and the same for the answer: the period claims the request, the interface
   thread cancels, the period hears the click and publishes a figure
   (`insert.publishPingResult(waiting, 1234)`).

It asserts that neither move happens, that the ping stays `Idle` and that no
figure is published.

## Evidence

All output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-2bc56f391.r2/logs`.
Commands run from the worktree root unless a different working directory is
stated.

| Phase | Exact command | Exit | Log / evidence |
|---|---|---:|---|
| RED - the seam and the test, before the fix | `cmake --build build --target HardwareIoTest -j"$(nproc)"` | 0 | `build-1.log` |
| **RED** - the test on the state machine before the fix | From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure` | **8** | `test-1-red.log`; `FAIL! ...aCancelledPingIsNotRevivedByAPeriodThatHadReadTheRequest() '!insert.claimPing(requested)' returned FALSE. (a period that had read the request put the ping back after the cancel)`; Totals 24 passed, 1 failed |
| Build with the fix | `cmake --build build --target zene HardwareIoTest -j"$(nproc)"` | 0 | `build-2.log` |
| GREEN with the fix | From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure` | **0** | `test-2-green.log`; 100% tests passed out of 1 |
| Qt case count | From `build/tests`: `./HardwareIoTest` | 0 | 25 passed, 0 failed, 0 skipped (`test-2-green.log`) |
| Save the product patch | `git diff HEAD~1 HEAD -- include/HardwareInsert.h src/core/HardwareInsert.cpp > .../logs/fix.patch` | 0 | `fix.patch`, 276 lines, the product half only |
| Reverse-apply the product fix | `git apply -R .../logs/fix.patch` | 0 | tree back to the pre-fix state machine |
| Rebuild without the fix | `cmake --build build --target HardwareIoTest -j"$(nproc)"` | 0 | `build-3-red.log` |
| **RED without the fix** (4b) | From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure` | **8** | `test-3-red.log`; the same assertion fails: `'!insert.claimPing(requested)' returned FALSE.`; Totals 24 passed, 1 failed |
| Restore the fix | `git apply .../logs/fix.patch` | 0 | tree back to the fixed state machine |
| Rebuild with the fix | `cmake --build build --target zene HardwareIoTest -j"$(nproc)"` | 0 | `build-4-green.log` |
| **GREEN with the fix** (4b) | From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure` | **0** | `test-4-green.log`; 100% tests passed out of 1 |
| The other consumer of the insert | `cmake --build build --target ImportTrackIoTest -j"$(nproc)"` | 0 | `build-5.log` |
| The insert's test family | From `build/tests`: `ctest -R '^(HardwareIoTest|ImportTrackIoTest)$' --output-on-failure` | 0 | `test-5-family.log`; 2 of 2 CTest suites passed |
| The wider latency/mixer family | `cmake --build build --target PdcMixerTest MixerConcurrencyTest InstrumentLatencyPdcTest DeviceToolsChainTest -j"$(nproc)"` | 0 | `build-6-family.log` |
| The wider latency/mixer family, run | From `build/tests`: `ctest -R '^(HardwareIoTest\|ImportTrackIoTest\|PdcMixerTest\|MixerConcurrencyTest\|InstrumentLatencyPdcTest\|DeviceToolsChainTest)$' --output-on-failure` | 0 | `test-6-family.log`; 6 of 6 CTest suites passed (MixerConcurrencyTest, InstrumentLatencyPdcTest, PdcMixerTest, HardwareIoTest, ImportTrackIoTest, DeviceToolsChainTest) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-review-2bc56f391` | **0** | `gates-1.log`; RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity "no new item in your files", file-length "no new item in your files") |
| Gates, after this report and its registration | the same command | **0** | `gates-2.log`; RESULT: PASS (base b61e5f965, 5 changed files) |
| Pre-submit check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-review-2bc56f391 review-2bc56f391` | **0** | `submit-check.log`; RESULT: PASS - submit (5 PASS, no FAIL; the five ASK items are answered below) |

The gates were run twice: once with the fix (`gates-1.log`, exit 0) and once
after this report and its registration were added (`gates-2.log`, exit 0).

## Files committed

- `include/HardwareInsert.h` - the ping's state machine in one atomic word, the
  declared moves, the test friend.
- `src/core/HardwareInsert.cpp` - the compare-and-swap moves, the figure
  published with the state, the lock-free assertion.
- `tests/src/core/HardwareIoTest.cpp` - the deterministic raced-transition
  regression.
- `docs/qwen-fixes-2026-10/cluster-review-2bc56f391.md` - this report.
- `tests/fork-sources.txt` - the report is a fork-new file, so it is registered.

Commits:

- `182aaa234` "test(hardware-insert): name the ping's state transitions, pin the
  raced cancel" - the seam (behaviour-preserving) and the test, RED.
- `5fc50053e` "fix(mixer): the ping's state machine is a compare-and-swap, so a
  cancel wins" - the fix, GREEN.
- the report commit, naming the cluster.

The seam and the test are separate from the fix on purpose: the fix's patch
(product half only) can then be reverse-applied to a tree that still compiles
the test, which is what makes the 4b proof a failing assertion rather than a
build error.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** Reverted (`git apply -R .../logs/fix.patch`): build exit 0,
  `ctest -R '^HardwareIoTest$'` exit **8**, `FAIL! ... '!insert.claimPing(requested)'
  returned FALSE.` (24 passed, 1 failed) - `test-3-red.log`. Restored
  (`git apply .../logs/fix.patch`): build exit 0, ctest exit **0**, 25 passed,
  0 failed - `test-4-green.log`.
* **The test is deterministic: no wall-clock timing, sleeps or races.** It uses
  the state machine's own seam: the audio thread's moves take the word the
  period read (`claimPing(PingWord expected)`,
  `src/core/HardwareInsert.cpp:226`), so the test reads the word, cancels, and
  then lets the period make the move it had read. No thread, no clock, no sleep,
  no timeout - the whole test is a sequence of calls.
* **The fix is at the cause and complete: every caller and sibling of what was
  changed was checked and stays consistent.** The callers of the ping state are
  `requestPing()` (`src/core/ControlCommandsHardwareInsert.cpp:146`),
  `cancelPing()` (`src/core/ControlCommandsHardwareInsert.cpp:73`),
  `setSettings()` (`src/core/HardwareInsert.cpp:97-98`), `load()`
  (`src/core/HardwareInsert.cpp:331`) and the audio thread's own moves in
  `process()` / `listenForPing()` (`src/core/HardwareInsert.cpp:153-172`,
  `212-224`). All of them now go through the compare-and-swap moves; the two
  interface-thread siblings of the cancel (`requestPing`, the "switched off"
  move) were changed with it, because a blind store of `Requested` was the same
  defect as a blind store of `Idle`. The readers of the state and the figure -
  `pingState()`, `settings()`, `latencyFrames()` and the control surface's
  `mixer.hardware_get` (`src/core/ControlHardwareIoSupport.cpp:98`) - read the
  one word now, so they cannot see a state and a figure that disagree.
* **No collateral damage: old projects and saved data still load as before, and
  any limit or check added does not hit other callers that legitimately exceed
  it.** The saved form is untouched: `<hardwareinsert on send return mix latency
  measured>` (`src/core/HardwareInsert.cpp:298-310`), and `load()` reads the
  same attributes (`src/core/HardwareInsert.cpp:312-331`). The figure is still
  a plain `int` in the settings and in the file - it is only *carried* inside
  the state word at runtime, so a project with any measured value (including a
  hand-edited one) loads exactly as before; the word holds a full 32-bit
  figure (`packPing`, `include/HardwareInsert.h:145-148`), so nothing is
  truncated or clamped. `HardwareIoTest::everythingSurvivesASaveAndLoad` and
  `HardwareIoTest::malformedInsertMeasurementKeepsUnknownSentinel` both pass
  (`test-2-green.log`). No limit or check was added to any public path.
* **Realtime, undo and commands.** Nothing new runs on the audio thread that
  allocates, frees, locks, waits, does I/O or emits a signal: the audio path
  (`process()`, `claimPing()`, `publishPingResult()`, `failPing()`,
  `listenForPing()`) only loads and compare-and-swaps a lock-free
  `std::atomic<int64_t>` (`src/core/HardwareInsert.cpp:41-43` asserts it is
  lock-free) - no allocation, no mutex, no logging, no allocating Qt call. The
  undo stays one honest step: the ping command's undo step is unchanged
  (`src/core/ControlCommandsHardwareInsert.cpp:147-151`), it still runs under
  the change lock, and no new control command was added (the command surface
  and its snapshot are untouched).
* **Portability.** Nothing Qt-version specific, no `windows.h` macro, no
  `std::filesystem`: the change uses `std::atomic<std::int64_t>` and C++20
  `std::bit_cast`. The packed measurement is shifted as `std::uint64_t`, then
  decoded from `std::uint32_t` back to `int` without shifting a negative value
  or relying on an out-of-range signed conversion.
* **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
  the intended contract that shows it."** Not applicable: the verdict is FIXED
  and a product change was made. For the record, the clause of the finding that
  the branch had already answered is the change-guard one, and the code that
  shows it is `src/core/ControlCommandsHardwareInsert.cpp:65-76` (the undo's
  snapshot, cancel and settings restore in one change-lock section, the cancel
  at line 73) and `src/core/ControlCommandsHardwareInsert.cpp:150` (the undo
  step that calls it) - that part landed in commit `cdb96259f`. The rest of the
  finding - the stale transition and the revived measurement - is what this
  commit fixes.
