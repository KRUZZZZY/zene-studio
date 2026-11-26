FIXED

# cluster new-p12-348: a dropped note-off can leave a CLAP plug-in voice stuck

new-p12-348: FIXED

## Verdict: REAL

Before the fix, `NoteQueue::push` (`plugins/ClapEffect/ClapNoteQueue.h`, the `difference < 0` branch, then lines
157-160) returned false for **any** event once the 1024-slot ring was full. `HostedPlugin::pushNote`
(`plugins/ClapEffect/ClapHostNotes.cpp:140`) only counted the drop, and nothing ever re-sent it. The consumer
(`HostedPlugin::Impl::drainNoteQueue`, `ClapHostNotes.cpp:307`) pops at most `kMaxNoteEventsPerBlock` = 256
events per block. A backlog therefore builds whenever producers push more than 256 events per block, for
example per-note expressions and param mods from several MPE channels, or live input on top of a clip's notes.
At that point a note-off or choke is lost, and the voice its note-on started keeps sounding indefinitely.
The RED run below shows this: the dropped release is never popped.

## Fix (at the cause: the queue)

`plugins/ClapEffect/ClapNoteQueue.h`:
- `push` (line 163) never drops a release. When the ring refuses a note-off or choke, `deferRelease` (line 210)
  sets one flag for that kind, channel and key in a fixed `std::array<std::atomic<uint32_t>, 128>` (2 kinds x 16
  channels x 128 keys) and bumps `m_outstandingReleases`. A second release of the same key and kind merges with
  the first, because one release ends the voice.
- `pop` (line 174) hands a deferred release out (`popDeferredRelease`, line 230) only once the ring is empty,
  that is after every event queued before it. Its `frameOffset` is `kDeferredReleaseFrame` (INT32_MAX, line 107).
  The time sort in `drainNoteQueue` puts it after every note of its block, and `sliceNotes` (`ClapHost.cpp:340`)
  moves it to the last frame of the last chunk. This means a note-on popped in the same block can never
  sort after its own release.
- While any deferred release is outstanding, `push` refuses every non-release event, and further releases are
  deferred too. So nothing queued after the lost release can overtake it, and recovery is strictly bounded: the
  ring drains in at most 1024/256 = 4 blocks and the flags in at most 4096/256 = 16 more. Producers are
  accepted again in the first `pop` that returns false with no flags left. Because that pop ends the drain,
  nothing pushed after the settle can land in the same block as the last recovered release.
- `reset` clears the flags and the count.
- `plugins/ClapEffect/ClapHostNotes.cpp:48` statically asserts that the queue's `kNoteEventOff` / `kNoteEventChoke`
  equal `CLAP_EVENT_NOTE_OFF` / `CLAP_EVENT_NOTE_CHOKE`. The header stays free of clap.h, as its design comment
  requires.

Realtime: no allocation, lock, wait or log. The flags are a member array of the queue, which already lives in
`HostedPlugin::Impl`. On the normal path the only new work is one acquire load per `push`. Overflow and
recovery add a few atomic read-modify-writes and a scan of at most 128 words. `std::atomic<uint32_t>` is
asserted always lock-free.

Concurrency (local TSan does not run on this box; this is reasoned from the design):
- Producers count a release *before* setting its flag, and undo the count only when the flag was already set.
  The single consumer clears a flag before it counts that release as handed out, and subtracts what it handed
  out only in a pop that found the ring empty and no flags left. So the count can't reach zero while a release
  is between its two steps, and it can't underflow.
- Two producers on different threads pushing the same key at the same moment still have no defined order,
  exactly as before the fix.

## Regression test

`tests/src/plugins/ClapNoteQueueTest.cpp` (new, registered in `tests/CMakeLists.txt` next to `ClapBusMapTest`,
in `tests/fork-sources.txt`, and in `tests/all-sources.txt`; that last entry was added in a follow-up commit after
`tests/all-sources-reproduce.sh` flagged the tracked file. Its re-run printed "REPRODUCES" with EXIT 0, log 28, and
`fork-sources-gate.sh` gave EXIT 0, log 29). It is fixture-free and drives only `push`/`pop`/`reset`, so the same test
compiles before and after the fix.
- `droppedNoteOffIsDelivered`, `droppedChokeIsDelivered` (RED): note-on, then 1023 fillers so the ring is full,
  then a release. Draining must deliver that release after the note-on, exactly once.
- `recoveryIsBoundedUnderLoad` (RED): 300 filler pushes per block for 8 blocks. The release must be delivered
  within those 8 blocks, and pushes must be accepted again afterwards.
- `retriggerDuringRecoveryKeepsOrder`: a retrigger that the queue accepts ends up sounding, and one it refuses
  does not.
- `resetForgetsDeferredRelease`, `releasesWithRoomAreUnchanged`: behaviour outside the bug is unchanged.

## Commands and exit codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-348.r1/logs)

| step | command | EXIT |
|---|---|---|
| configure | `cmake -S . -B build` (03-configure.log) | 97: refused by bunny-guard ("configuring is the parent's job") |
| regen via build | `cmake --build build --target ClapBusMapTest -j4` (04-build-regen.log; the build re-ran CMake) | 0 |
| RED build | `cmake --build build --target ClapNoteQueueTest -j4` (05-build-red.log) | 0 |
| RED test | `cd build/tests && ctest -R ClapNoteQueueTest --output-on-failure` (06-test-red.log): `Totals: 5 passed, 3 failed` | 8 |
| GREEN build | `cmake --build build --target <21 Clap targets + zene> -j4` (15-build-green.log) | 0 |
| GREEN test | `ctest -R ClapNoteQueueTest --output-on-failure` (16-test-green.log): `100% tests passed out of 1` | 0 |
| family | `ctest -R Clap --output-on-failure` (17-test-clap-family.log): `100% tests passed out of 9` | 0 |
| 4b revert | `git diff -- plugins/ClapEffect/ClapNoteQueue.h plugins/ClapEffect/ClapHostNotes.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b build | `cmake --build build --target ClapNoteQueueTest -j4` (18-build-reverted.log) | 0 |
| 4b RED | `ctest -R ClapNoteQueueTest --output-on-failure` (19-test-reverted.log): `Totals: 5 passed, 3 failed` | 8 |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| 4b build | `cmake --build build --target <same targets> -j4` (20-build-reapplied.log) | 0 |
| 4b GREEN | `ctest -R ClapNoteQueueTest --output-on-failure` (21-test-reapplied.log): `100% tests passed out of 1` | 0 |
| gates | `python3 .../qwen/feature_check.py <worktree>` (22-feature-check.log) | 1: crashed, see below |
| complexity | `bash tests/complexity-gate.sh --check` (23-complexity.log) | 2: "lizard is not installed" |
| file-length | `bash tests/file-length-gate.sh --check` (24-file-length.log) | 0: "PASS (check mode: no regressions)" |

RED failures (06 and 19):
- `droppedNoteOffIsDelivered() 'off > on' returned FALSE. (the dropped release was never delivered)` at
  ClapNoteQueueTest.cpp:159
- the same for `droppedChokeIsDelivered()`
- `recoveryIsBoundedUnderLoad() 'countOf(delivered, kNoteOff, 0, 60) == 1' returned FALSE` at line 186

### Gates: partly BLOCKED by the environment

`feature_check.py` ran its simple gates and wrote their logs to `build/feature-check/` at 00:44. Each one passed:
- fork-sources "PASS: every tracked source in scope is registered"
- all-sources-reproduce "REPRODUCES"
- no-upstream-regression "PASS: every change to upstream-inherited code ... is declared"
- check-namespace "0 errors."
- no-tautology "PASS"
- doc-figures "RESULT: PASS"

The script then crashed while computing the ratchet baseline:
`bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d...' is not allowed in a Bunny lane`
(`CalledProcessError ... returned non-zero exit status 97`). I did not work around the guard. I ran the two
ratchet gates directly in the worktree instead:
- file-length passes.
- complexity can't run because `lizard` is not installed, and I did not install it into the shared environment.

CCN of the new functions, counted by hand (lizard counts `if`, `for`, `&&`, `||` and `?:`): `push` 5, `pop` 2,
`isRelease` 2, `deferRelease` 6, `popDeferredRelease` 5, `reset` 3. All are at most 10. No changed file is over
500 lines: `ClapNoteQueue.h` is about 330 and `ClapNoteQueueTest.cpp` 243.

## Self-check

- **The test fails with the product change reverted and passes with it.** It does: 4b revert, then
  `ctest -R ClapNoteQueueTest`, gave EXIT 8 (3 failed). Re-apply, then the same command, gave EXIT 0
  (logs 19 and 21). The first RED run before the fix existed also gave EXIT 8 (log 06).
- **The test is deterministic.** It is single-threaded, with no sleeps, timers or wall clock. The "blocks" are
  explicit loops of at most 256 `pop` calls, and every loop is bounded (at most 64 blocks), so a failure can't
  hang. The only inputs are the event values the test builds.
- **The fix is at the cause and complete.** The queue is where the drop happened.
  - Every producer passes through `HostedPlugin::pushNote` (`ClapHostNotes.cpp:140`): setNoteOn, setNoteOff,
    setNoteChoke, setNoteExpression and setNoteParamMod. Their return-value contract is unchanged: false means
    dropped and is counted.
  - The only consumer is `drainNoteQueue` (`ClapHostNotes.cpp:307`). It needs no change, because `pop` hands
    out deferred releases itself, and the sentinel frame is bounded by `sliceNotes` (`ClapHost.cpp:340-353`).
  - The only `reset` caller is `prepare()` (`ClapHostLifecycle.cpp:84`), and it now clears the deferred state.
  - Every target that includes the header was rebuilt (all 21 Clap targets plus zene), and the whole `Clap`
    ctest family passes (9/9).
  - The VST3 sibling queue has the same flaw; see Other findings.
- **No collateral damage.**
  - Nothing is saved or loaded, so projects are unaffected.
  - With room in the ring, behaviour is identical (`releasesWithRoomAreUnchanged`; ClapHostTest and the
    integration tests pass).
  - The only new refusal is of non-release events, and only while a release lost to an already-full ring is
    still outstanding. That lasts at most about 20 blocks. Those events would mostly have hit a full ring
    anyway, and they are counted as dropped, as before.
  - A deferred release is no longer counted in `NoteCounters::dropped`, because it is delivered. Two merged
    releases of one key are counted as pushed twice and delivered once.
  - No control command was added.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED. The RED runs show the bug
  on this branch.

Delegation:
- Sonnet wrote `ClapNoteQueueTest.cpp` with its CMake and fork-sources registration from my test design. Its
  configure attempt was refused by the guard.
- Haiku ran the GREEN build, the Clap ctest family and the step-4b revert/re-apply sequence. Its first attempt
  stopped because the guard refuses full builds; the second used named targets.
- I diagnosed the bug, designed the fix and wrote `ClapNoteQueue.h` myself (concurrency-sensitive, about 90
  lines). I also reviewed both diffs and wrote this report.
- DeepSeek was not used.

## Other findings

- `plugins/Vst3Effect/Vst3MidiQueue.h` is the same bounded MPSC ring design (the ClapNoteQueue.h comment names it
  as the model). It also drops note-offs when full, with no recovery. Not changed here.
- The `feature_check.py` ratchet step needs `git checkout` in `~/.local/share/zene-bunny-fix/qwen/gate-baselines/tree`,
  which the lane guard refuses. The complexity gate also needs `lizard`, which is not installed on this box.
