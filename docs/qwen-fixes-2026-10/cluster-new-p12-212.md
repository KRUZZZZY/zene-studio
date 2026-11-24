FIXED

# cluster new-p12-212 — a refused channel count left the client and the host in different states

Verdict: **REAL, FIXED**. The refusal of an out-of-range `IdChangeInputOutputCount` was only a
log line; it now fails the host instance, so the two sides can no longer disagree about whether a
period was processed.

## The defect (REAL — evidence in the code as it stood on this branch)

The two halves of the protocol decide independently what a refused channel count means:

| Side | Code | What it did |
|---|---|---|
| client | `include/RemotePluginClient.h:97-104` — `setInputOutputCount()` stores `m_inputCount`/`m_outputCount` **before** it sends the message | the client's counts are the refused ones |
| client | `include/RemotePluginClient.h:340-361` — `doProcessing()` refuses to render when `in*bufferSize + out*bufferSize > m_audioBuffer.size()` | the client skips every period, it does not render |
| client | `include/RemotePluginClient.h:293-298` — `case IdStartProcessing:` calls `doProcessing()`, then replies `IdProcessingDone` unconditionally | the skipped period still looks like a processed one to the host |
| host | `src/core/RemotePlugin.cpp:1177` (the branch; before this change it stored `m_refusedInCount`/`m_refusedOutCount`/`m_refusalPending` and `break`) | `m_failed` was **not** set, so `process()` kept asking for periods |

`RemotePlugin::process()` only silences the output when it is failed or the client is not running
(`src/core/RemotePlugin.cpp:513-517`, `std::ranges::fill(m_hostOutputs, 0.f)`); a successful reply
means the host publishes the shared output planes of that period. The refused-count case therefore
produced exactly the state in the finding: the client writes no planes, answers `IdProcessingDone`,
and the host treats the answer as a rendered period and copies the stale planes.

Same family, for comparison — every other host-side protocol refusal already failed the instance:
retired pre-#589 count ids (`src/core/RemotePlugin.cpp:1159-1162`), a message with too few
arguments (`src/core/RemotePlugin.cpp:1092`), a lost connection
(`src/core/RemotePlugin.cpp:710-714`). The out-of-range count was the one refusal that did not.

## The fix (smallest change at the cause)

- `src/core/RemotePlugin.cpp:1192` — `m_failed = true;` in the out-of-range branch, with the reason
  in the comment above it: the client stops processing with the old block while still answering
  `IdProcessingDone`, so failing the instance is what makes `process()` render silence instead of
  the stale planes. This is the "fail and silence the host instance" outcome the finding allows,
  and it is the same outcome the retired ids and the arity refusal already produce.
- `src/core/RemotePlugin.cpp:1184` — `m_deferredWork.store(true)`: the main thread now learns about
  the refusal on its next timer tick instead of only at `processFinished()`/destructor (a failed
  plugin never resizes its buffer again, so `updateAudioBuffer()` is not called either).
- `src/core/RemotePlugin.cpp:709` — `applyDeferredClientRequests()` calls
  `reportRefusedChannelCounts()` before `reportDeferredClientRequests()`, the same order the
  destructor uses (`:213-214`).
- `src/core/RemotePlugin.cpp:1064-1072` — the warning says what now happens:
  `... out - refusing the request and marking the plugin as failed`.
- `include/RemotePlugin.h:176-181` — the `reportRefusedChannelCounts()` contract: the refusal fails
  the instance, and `applyDeferredClientRequests()` is one of its callers.

Realtime: the audio-thread half is unchanged in kind — it still only stores atomics and logs
nothing (`m_inProcessPeriod` is not even consulted on this path, so both threads take the same
branch). The new call site is `applyDeferredClientRequests()`, main thread, next to the logging
that is already there.

Callers and siblings checked: `IdChangeInputCount` / `IdChangeOutputCount` (retired, already
fail), `refuseIncompleteMessage()` (already fails), the in-range path
(`setChannelCounts()` on the ports model, `:1205-1206`), and the pending-change path
(`m_countChangePending`, applied only `if (!m_failed)`, `:717-724` — a refused count can no longer
be followed by applying a stale pending change, because the instance is failed). No new limit was
added: `MaxChannelsPerAudioBuffer` and the `ch_cnt_t` cast are untouched, so a client inside the
range is unaffected.

## Regression test

`tests/src/core/RemotePluginAudioPortsTest.cpp:834` —
`refusedOutOfRangeChannelCountFailsTheHostInstance()`, run from the existing test binary for the
area. It puts the host in the state a completed handshake leaves (running, not failed) through the
`LMMS_TESTING` friend seams — `CachedRunningState<RemotePlugin>::steer(plugin, true)` and
`plugin.m_failed = false` — so no client process is started and nothing is timed. Then:

1. a marker is written into the host's output plane (`std::fill`, 1.0f);
2. `processMessage(IdChangeInputOutputCount(-1, 2))`;
3. `plugin.failed()` must be true (**this is the assertion that fails without the fix**);
4. the counts and the shared block must not have moved;
5. `plugin.process()` must report false and the marker must be gone — silence instead of the stale
   planes;
6. `plugin.applyDeferredClientRequests()` must write the warning exactly once, and a second
   `reportRefusedChannelCounts()` must report nothing.

Test matrix (normal / failure / exhaustion / reconfiguration / audio thread / platform):

| Path | Covered by |
|---|---|
| normal, in-range count still applies | `clientChannelCountMessageResizesSharedBuffer` (unchanged, passes) |
| failure: out-of-range count on a live host | new `refusedOutOfRangeChannelCountFailsTheHostInstance` |
| failure: main-thread report, exactly once | new test + `refusedChannelCountIsReportedOnceFromTheMainThread` (unchanged, passes) |
| reconfiguration: negative -1/-8, oversized 100000 in either argument | `outOfRangeChannelCountMessagesAreRefused` (unchanged, passes) |
| exhaustion: a pending count change is never applied to a failed plugin | `applyDeferredClientRequests()` `if (!m_failed)` guard (read, unchanged) |
| audio thread: the refusal allocates and logs nothing | `refusedChannelCountsAllocateNothing` (unchanged, passes) |
| platform: `SYNC_WITH_SHM_FIFO` builds of the same TU | `RemotePluginAudioPortsShmCompileCheck`, `RemotePluginShmFifoCompileCheck`, `RemotePluginShmFifoTest` all built and passing |

## Evidence — commands and EXIT codes as observed in this session

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-212.r1/logs`.
Builds and tests were run from the worktree and from `build/tests` respectively, never piped.

| Step | Command | EXIT | Log |
|---|---|---|---|
| RED test-target build (product unchanged) | `cmake --build build --target RemotePluginAudioPortsTest -- -j4` | 0 | `build-1-red.log` |
| **RED** | `ctest -R "^RemotePluginAudioPortsTest$" --output-on-failure` | **8** | `test-1-red.log` |
| RED failure line | `FAIL! : ...refusedOutOfRangeChannelCountFailsTheHostInstance() 'plugin.failed()' returned FALSE. Loc: [...RemotePluginAudioPortsTest.cpp(863)]` | — | `test-1-red.log` |
| GREEN build (fix applied) | `cmake --build build --target RemotePluginAudioPortsTest -- -j4` | 0 | `build-2-green.log` |
| **GREEN** | `ctest -R "^RemotePluginAudioPortsTest$" --output-on-failure` | **0** | `test-2-green.log` |
| GREEN, slots one by one | `./RemotePluginAudioPortsTest refusedOutOfRangeChannelCountFailsTheHostInstance refusedChannelCountIsReportedOnceFromTheMainThread outOfRangeChannelCountMessagesAreRefused refusedChannelCountsAllocateNothing clientChannelCountMessageResizesSharedBuffer` (from `build/tests`) | 0 | `test-3-slots.log` (all five `PASS`) |
| Product + plugin targets build (header changed) | `cmake --build build --target zene RemotePluginClientE2ETest RemotePluginAudioPortsShmCompileCheck RemotePluginShmFifoCompileCheck -- -j4` | 0 | `build-3-zene-and-tests.log` |
| Family ctest, first attempt | `ctest -R "RemotePlugin" --output-on-failure` | 8 | `test-4-family.log` — `RemotePluginShmFifoTest` was **Not Run**: that binary had never been built in this build tree (stale tree, not a regression: no test failed) |
| Build the missing test binary | `cmake --build build --target RemotePluginShmFifoTest -- -j4` | 0 | `build-4-shmfifo.log` |
| Family ctest | `ctest -R "RemotePlugin" --output-on-failure` | **0** | `test-5-family.log` (4/4 passed: AudioPorts, ClientE2E, ShmFifo, ShmFifoQtUsageCheck) |
| 4b: save product diff and reverse it | `git diff -- include/RemotePlugin.h src/core/RemotePlugin.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | — |
| rebuild without the fix | `cmake --build build --target RemotePluginAudioPortsTest -- -j4` | 0 | `build-5-red-revert.log` |
| **RED again**, product reversed | `ctest -R "^RemotePluginAudioPortsTest$" --output-on-failure` | **8** | `test-6-red-revert.log` — same `'plugin.failed()' returned FALSE` at line 863 |
| 4b: restore the fix | `git apply .../logs/fix.patch` | 0 | — |
| rebuild with the fix | `cmake --build build --target RemotePluginAudioPortsTest -- -j4` | 0 | `build-6-green-restored.log` |
| **GREEN again** | `ctest -R "^RemotePluginAudioPortsTest$" --output-on-failure` | **0** | `test-7-green-restored.log` |
| Application rebuild (header consumer) | `cmake --build build --target zene -- -j4` | 0 | `build-7-zene-final.log` |
| Family ctest on the committed tree | `ctest -R "RemotePlugin" --output-on-failure` | **0** | `test-8-final-family.log` (4/4 passed) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `gates-2.log` (8 × PASS, `RESULT: PASS`) |
| Gates, before the registration lines were appended | the same command | 0 | `gates-1.log` |

`gates-2.log` also covers the registration lines appended for this change: two reasons in
`tests/upstream-modifications.txt` (both files were already listed; the per-change reason is
appended as every other cluster does).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reversed: `git apply -R fix.patch` EXIT 0, rebuild EXIT 0, `ctest` EXIT **8** with
  `FAIL! ... 'plugin.failed()' returned FALSE` (`test-6-red-revert.log`). Restored:
  `git apply fix.patch` EXIT 0, rebuild EXIT 0, `ctest` EXIT **0** (`test-7-green-restored.log`).
  Logs: `fix.patch` in the log folder holds exactly the two product files
  (`include/RemotePlugin.h`, `src/core/RemotePlugin.cpp`), the test hunk was never reverted.
- **The test is deterministic.** No wall clock, no sleeps, no threads, no client process. The
  "running, not failed" precondition is injected through the `LMMS_TESTING` friend seams
  (`CachedRunningState<RemotePlugin>::steer()`, `plugin.m_failed = false`), the main-thread half is
  driven by calling `applyDeferredClientRequests()` directly, and the message is handed to
  `processMessage()` directly, exactly as the existing arity/retired-id regressions do.
- **The fix is at the cause and complete.** The refusal branch is shared by the audio-thread and
  main-thread paths (it does not branch on `m_inProcessPeriod`), so both are covered. Siblings that
  can refuse a protocol message already failed the instance and are unchanged
  (`src/core/RemotePlugin.cpp:1092`, `:1159-1162`, `:710-714`). The client half is untouched: it
  already refuses to render a period with counts that do not fit, which is the state the host now
  agrees with.
- **No collateral damage.** In-range counts are unaffected (`clientChannelCountMessageResizesSharedBuffer`
  passes; no limit or range was added or changed). `MaxChannelsPerAudioBuffer` and the `ch_cnt_t`
  cast are untouched, so no other caller that legitimately exceeds a smaller limit is rejected.
  Saved data is not involved at all: the message is wire protocol only, and the refusal changes no
  project, model or save behaviour. A plugin that fails this way is silenced exactly like one that
  sends a retired count id — the destructor's deliberate-shutdown path is the same.
- **Realtime.** The audio-thread path adds two atomic stores and one atomic bool store; no
  allocation, no lock, no wait, no log, no I/O, no signal. `refusedChannelCountsAllocateNothing`
  still reports 0 allocations and 0 warnings for the refusal. The new log call lives in
  `applyDeferredClientRequests()`, which is main-thread only (called from the deferred timer and
  from the tests).
- **Undo and commands.** Nothing touches undo, models or the control-command surface; no command
  was added, so `commands_snapshot.json` cannot move.
- **Sibling commands.** This change touches no control-command file at all: the diff is
  `src/core/RemotePlugin.cpp`, `include/RemotePlugin.h`, the test, the registration list and this
  report. The `app.*`, `arrangement.*` and other groups the pre-submit check lists as siblings come
  from other commits on this branch between the merge base and `HEAD`, not from this cluster, so no
  command's behaviour or reversibility class moves with this fix.
- **Verdict evidence.** REAL, not ALREADY-FIXED: on this branch the out-of-range branch at
  `src/core/RemotePlugin.cpp:1177-1193` set no failure, and the RED run above (EXIT 8,
  `plugin.failed()` false) shows the behaviour on the code as it was.

## Other findings (not fixed here)

- `RemotePluginShmFifoTest` had never been built in this build tree, so the first
  `ctest -R RemotePlugin` run reported it `Not Run` (EXIT 8). It builds and passes after
  `cmake --build build --target RemotePluginShmFifoTest`; a fresh build tree will not show this.
- The client's `doProcessing()` refusal is reported to the host only as a debug text
  (`include/RemotePluginClient.h:358-359`). With the host now failing the instance as soon as it
  refuses the count, that text is a duplicate diagnostic; it was left alone because it is the
  client's own guard against overrunning the block, which must stay.
