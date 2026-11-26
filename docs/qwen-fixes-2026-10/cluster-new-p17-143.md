FIXED

# cluster new-p17-143: a replacement client is never told the shared audio block's key

## Verdict evidence (REAL)

- The key of the shared audio block used to be sent from exactly one place: `src/core/RemotePlugin.cpp:1013`
  (before this change, `:1003`), `RemotePlugin::updateAudioBuffer()`, immediately after it creates the block. That
  function returns before the send when the dimensions did not change: `src/core/RemotePlugin.cpp:969-972`
  (`if (channelsIn == m_channelsIn && channelsOut == m_channelsOut && frames == m_frames) { return m_hostBlock.data(); }`).
- A client can only learn the key through that create path. Activation reaches it through
  `RemotePlugin::waitForInitDone()` -> `m_audioPorts->activate(Engine::audioEngine()->framesPerPeriod())`
  (`src/core/RemotePlugin.cpp:431`) and through the deferred path in `process()` when the client's IdInitDone arrives
  (`:949`); `RemotePluginAudioPorts::activate()` (`include/RemotePluginAudioPorts.h:106-109`) calls
  `updateBuffers()` (`:149`), which calls `m_buffers->updateAudioBuffer(...)` (`:157`). `init()` itself sent only
  `IdSyncKey` (`src/core/RemotePlugin.cpp:384`) before this fix.
- Nothing detaches the block on failure, so a reused object keeps it: `init()`'s reuse branch
  (`src/core/RemotePlugin.cpp:262-278`) resets the per-connection state but not `m_audioBuffer`, and neither
  `invalidate()` (`include/RemotePluginBase.h:989-998`) nor `processFinished()` (`src/core/RemotePlugin.cpp:1022`)
  touches it. With the dimensions unchanged, activation therefore takes the shortcut, and the replacement client
  stays unattached: the client attaches only in `RemotePluginClient::setShmKey()` (`include/RemotePluginClient.h:299-301` and `:329`),
  and an unattached client reports `doProcessing(): have no shared memory!` and renders nothing
  (`include/RemotePluginClient.h:340-374`). That is the finding, confirmed by the red test below.
- Reachability and callers: a client process is started only from `RemotePlugin::init()` (`src/core/RemotePlugin.cpp:256`,
  started by `ProcessWatcher::run()` at `:107`). Its callers are `plugins/VstBase/VstPlugin.cpp:195` and
  `plugins/ZynAddSubFx/ZynAddSubFx.cpp:83`; the reuse branch at `:263` exists for the second call after a failure.

## Fix

`src/core/RemotePlugin.cpp:386-394` - `init()` sends `IdChangeSharedMemoryKey` with the current block's key whenever
the object already holds a block, next to the `IdSyncKey` it already sends on every connection. Nothing else moved:
`updateAudioBuffer()`'s create path is unchanged, and a first connection (no block yet) sends exactly what it sent
before, so no connection ever receives two keys.

## Test

`tests/src/core/RemotePluginAudioPortsTest.cpp` - `clientConnectingToAnExistingBlockIsToldItsKey()` (socket transport,
inside the file's existing `#ifndef SYNC_WITH_SHM_FIFO` region). It allocates the block for the previous client,
points the plugin's socket at a socketpair so the allocation's own key send has a connection to go to (the same fd
seam `TestRemotePluginBase` uses), then connects a fresh client through `init()` and drives one period. The python
peer logs every message before it answers the next one, so the completed period proves the peer read everything the
host sent; the test then requires `IdChangeSharedMemoryKey` in that log (exactly once) and requires the block to be
the same block (same plane pointer), i.e. the shortcut is what is in play.

The state is driven directly instead of through a client death: after a client that had connected dies, the watcher
thread's `while (!m_quit && m_plugin->messagesLeft())` loop (`src/core/RemotePlugin.cpp:134`) sees the peer's EOF as
readable through `messagesLeft()`'s `poll()` on the still-open socket (`include/RemotePluginBase.h:858-872`), so
`init()`'s `m_watcher.wait()` would not return and a second `init()` cannot be driven deterministically from a test.
The allocation-plus-new-connection state the test drives is the state a reinitialisation produces. See Other findings.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p17-143.r1/logs)

- Warm-up build: `cmake --build build --target RemotePluginAudioPortsTest -j4 > build-0-baseline.log 2>&1` EXIT=0.
- First version of the test (`build-1-red.log` EXIT=0, `test-1-red.log`): `ctest -R RemotePluginAudioPortsTest`
  EXIT=8, `FAIL! ... 'plugin.process()' returned FALSE` with `Error while writing.` - the test allocated the block
  with no socket (`m_socket == -1`), which invalidates the connection. The test was corrected to give the object a
  socketpair to allocate into; that red run was not accepted as evidence.
- RED (test present, fix absent): `cmake --build build --target RemotePluginAudioPortsTest -j4 > build-2-red.log 2>&1`
  EXIT=0; `cd build/tests && ctest -R RemotePluginAudioPortsTest --output-on-failure > test-2-red.log 2>&1` EXIT=8,
  `Totals: 33 passed, 1 failed`, `FAIL! : ...::clientConnectingToAnExistingBlockIsToldItsKey() 'ids.contains(IdChangeSharedMemoryKey)' returned FALSE.`
- GREEN: `cmake --build build --target RemotePluginAudioPortsTest -j4 > build-3-green.log 2>&1` EXIT=0;
  `ctest -R RemotePluginAudioPortsTest --output-on-failure > test-3-green.log 2>&1` EXIT=0 (`100% tests passed`, 31.17 s).
- GREEN, the one function: `./build/tests/RemotePluginAudioPortsTest clientConnectingToAnExistingBlockIsToldItsKey >
  test-4-green-function.log 2>&1` EXIT=0, `PASS : ...::clientConnectingToAnExistingBlockIsToldItsKey()`,
  `Totals: 3 passed, 0 failed, 0 skipped`.
- 4b revert: `git diff -- src/core/RemotePlugin.cpp > logs/fix.patch` DIFF_EXIT=0; `git apply -R logs/fix.patch`
  REVERSE_APPLY_EXIT=0; `cmake --build build --target RemotePluginAudioPortsTest -j4 > build-4-reverse.log 2>&1`
  BUILD_EXIT=0; `./build/tests/RemotePluginAudioPortsTest clientConnectingToAnExistingBlockIsToldItsKey >
  test-5-reverse-red.log 2>&1` EXIT=1 (`Totals: 2 passed, 1 failed`).
- 4b re-apply: `git apply logs/fix.patch` APPLY_EXIT=0; `cmake --build build --target RemotePluginAudioPortsTest -j4 >
  build-5-reapply.log 2>&1` BUILD_EXIT=0; same test command `> test-6-reapply-green.log 2>&1` EXIT=0
  (`Totals: 3 passed, 0 failed`).
- Product: `cmake --build build --target zene -j4 > build-6-zene.log 2>&1` BUILD_EXIT=0 (`[100%] Built target zene`).
- Family: `ctest -R 'RemotePlugin|ZynSeparateProcess' --output-on-failure > test-7-family.log 2>&1` EXIT=8 with three
  `Not Run` results (their executables were not built in this tree); after
  `cmake --build build --target RemotePluginClientE2ETest RemotePluginShmFifoTest ZynSeparateProcessTest -j4 >
  build-7-family-targets.log 2>&1` BUILD_EXIT=0, the same ctest command `> test-8-family.log 2>&1` EXIT=0,
  `100% tests passed, 0 tests failed out of 7`.
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log 2>&1`
  EXIT=0, `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
  doc-figures, complexity, file-length - all PASS; complexity and file-length report "no new item in your files").
- Registration: `src/core/RemotePlugin.cpp` entry added to `tests/upstream-modifications.txt` in the same commit.

## Test matrix

- Normal path (first connection): unchanged - no block exists, so `init()` sends only `IdSyncKey`; the block is created
  after the client reports its counts and named then (`updateAudioBuffer()` -> `:1013`). Existing coverage:
  `zeroOutputPluginNeverRequestsAPeriod` (asserts the key), `updateAudioBufferFollowsChannelCountChange`.
- Failure / replacement path: the new test - a client that connects on top of the existing block must be told its key,
  exactly once, and the block must be the same block.
- Exhaustion: unchanged - overflowing/oversized requests still detach and return null (`updateAudioBuffer()`,
  existing `oversized*`/refusal tests in the same file).
- Reconfiguration: unchanged - a channel-count change still re-creates the block and sends the new key from the same
  create path (existing `updateAudioBufferFollowsChannelCountChange`, and the family run above).
- Audio thread: the new send is in `init()`, on the calling (main) thread, beside the `IdSyncKey` send that was already
  there. `updateAudioBuffer()`'s early return is untouched, so no message, lock, allocation or log is added to
  `process()`, `processPeriod()` or any render path.
- Platforms: the send is outside every `SYNC_WITH_SHM_FIFO` conditional, so both transports get it. The test is
  socket-only and sits inside the file's existing `#ifndef SYNC_WITH_SHM_FIFO` region; the file is also compiled with
  `SYNC_WITH_SHM_FIFO` by the `RemotePluginAudioPortsShmCompileCheck` target, where the slot is compiled out.

## Other findings

- `tests/src/core/RemotePluginAudioPortsTest.cpp` allocation with no connection: calling `ports.audioPortsModel().setChannelCounts()`
  before a client exists writes the key to `m_socket == -1`, which runs `RemotePluginBase::write()`'s error path and
  calls `invalidate()`; the plugin then refuses every later write ("Error while writing.", `test-1-red.log`). Seen, not
  investigated - whether a real plugin can set port counts before its client connects is a separate question.
- `src/core/RemotePlugin.cpp:134-137` (`while (!m_quit && m_plugin->messagesLeft())`) plus the socket `messagesLeft()`
  (`include/RemotePluginBase.h:858-872`) look like they keep the watcher thread alive after a client that had connected
  dies (EOF polls as readable), which would make `init()`'s `m_watcher.wait()` (`:311`) block on a reuse. This is code
  reading, not measured; on the shm-fifo transport `messagesLeft()` reads the FIFO, which drains, so the reuse path
  completes there. Not fixed here (different bug, and the fix would change failure/teardown behaviour).

## Self-check

- Verdict is FIXED, so the ALREADY-FIXED/FALSE-POSITIVE item does not apply.
- Red proves the test catches the bug: with the product change reversed (`git apply -R fix.patch` EXIT=0) the function
  fails, EXIT=1 (`test-5-reverse-red.log`); with it re-applied (`git apply fix.patch` EXIT=0) it passes, EXIT=0
  (`test-6-reapply-green.log`). The ctest family run before the fix failed on exactly this function and nothing else
  (`test-2-red.log`: 33 passed, 1 failed).
- Determinism: no sleeps and no clocks in the test. The only wait is `plugin.process()`'s bounded wait for the peer's
  `IdProcessingDone`, which the peer sends after reading (and line-flushing) every earlier message; a completed period
  is a happens-before edge, not a timeout. The peer logs each message with `buffering=1` before it answers.
- Cause and completeness: the send lives where a connection is made (`init()`), not in the resize path that skips it;
  every caller reaches it through `RemotePlugin::init()`, and both activation paths (`waitForInitDone()` and the
  deferred IdInitDone in `process()`) now run after the client already has the key. A rebuilt block still sends its new
  key from the create path, so the two sends cannot disagree.
- Collateral: nothing is saved or loaded by this change, so old projects are unaffected; the only behavioural delta is
  one extra small message on a connection that reuses an existing block (previously that connection was silent about
  the block). No command, no undo step, no ControlCommandsSnapshot change, no audio-thread code.
- Realtime: `init()` runs on the thread that loads the plugin; `process()`/`processPeriod()` and `updateAudioBuffer()`'s
  shortcut are untouched.
- Unverified: no real two-process reinitialisation was driven (see the watcher-loop note above), and the change was not
  built for Windows/macOS here; the send is not behind any transport `#ifdef`, and the test compiles out on the
  `SYNC_WITH_SHM_FIFO` build (`RemotePluginAudioPortsShmCompileCheck` target, built with the suite).
