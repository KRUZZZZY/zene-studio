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

Round 2 (the submit check sent round 1 back): the test added two `QSKIP`s (no python3, client did not start). That is a
weakened test - a host without the peer would have turned this regression into a silent pass - so both are `QVERIFY2`
assertions now, and the missing capability fails the test loudly (`python3 is required ...`, `the client stand-in did not
start: this regression cannot pass without it`). `kads_submit_extra.py --base 3436a808b6 --worktree <worktree>` reports
`weakening, registration and ratchet checks pass against base 3436a808b6` EXIT=0 (`extra-check-1.log`); the added test
lines contain no skip and no assertion was removed (checked over the whole base..HEAD test diff: 72 added lines,
0 added skips, 0 removed assertions, 14 added assertions).

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p17-143.r1/logs)

Round 2 (final test, logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p17-143.r2/logs):

- RED with the final test - fix reversed in the working tree: `git diff 3436a808b6 -- src/core/RemotePlugin.cpp >
  fix.patch` EXIT=0, `git apply -R fix.patch` EXIT=0; `cmake --build build --target RemotePluginAudioPortsTest -j4 >
  build-1-red.log 2>&1` BUILD_EXIT=0; `cd build/tests && ./RemotePluginAudioPortsTest
  clientConnectingToAnExistingBlockIsToldItsKey > test-1-red.log 2>&1` EXIT=1 (`Totals: 2 passed, 1 failed, 0 skipped`,
  `FAIL! ... 'ids.contains(IdChangeSharedMemoryKey)' returned FALSE`).
- GREEN: `git apply fix.patch` EXIT=0; `cmake --build build --target RemotePluginAudioPortsTest -j4 >
  build-2-green.log 2>&1` BUILD_EXIT=0; same test command `> test-2-green.log 2>&1` EXIT=0
  (`Totals: 3 passed, 0 failed, 0 skipped`).
- Product and family: `cmake --build build --target zene -j4 > build-3-zene.log 2>&1` BUILD_EXIT=0;
  `cd build/tests && ctest -R 'RemotePlugin|ZynSeparateProcess' --output-on-failure > test-3-family.log 2>&1` EXIT=0
  (`100% tests passed, 0 tests failed out of 7`).
- shm-fifo compile check with the final test: `cmake --build build --target RemotePluginAudioPortsShmCompileCheck -j4 >
  build-4-shm-compile-check.log 2>&1` BUILD_EXIT=0.
- Weakening / registration / ratchet check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/kads_submit_extra.py
  --base 3436a808b6 --worktree <worktree> > extra-check-1.log 2>&1` EXIT=0.

Round 1 (same product fix; the test has since dropped its two skips):

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
- shm-fifo compile check of the test file: `cmake --build build --target RemotePluginAudioPortsShmCompileCheck -j4 >
  build-8-shm-compile-check.log 2>&1` EXIT=0.

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
  `SYNC_WITH_SHM_FIFO` by the `RemotePluginAudioPortsShmCompileCheck` target, where the slot is compiled out -
  `cmake --build build --target RemotePluginAudioPortsShmCompileCheck -j4 > build-8-shm-compile-check.log 2>&1`
  EXIT=0 on this tree, after the test file's last edit (object timestamp 13:34, edit 13:22).

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

The five ASK items of `submit_check.py` (log: `submit-1.log`), in its order:

1. "The regression test FAILS with your product change reverted and PASSES with it (exit codes)." - Reverted
   (`git diff -- src/core/RemotePlugin.cpp > fix.patch` EXIT=0, `git apply -R fix.patch` EXIT=0, build EXIT=0):
   `./RemotePluginAudioPortsTest clientConnectingToAnExistingBlockIsToldItsKey` EXIT=1
   (`test-5-reverse-red.log`: `Totals: 2 passed, 1 failed`). Re-applied (`git apply fix.patch` EXIT=0, build EXIT=0):
   same command EXIT=0 (`test-6-reapply-green.log`: `Totals: 3 passed, 0 failed, 0 skipped`). The whole-file red run
   `ctest -R RemotePluginAudioPortsTest` was EXIT=8 with exactly this function failing (`test-2-red.log`).
2. "The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses)." -
   No sleeps and no clock reads. Seams: (a) the plugin's socket is set to one end of a `::socketpair` (the fd seam
   `TestRemotePluginBase` already uses in this file, reachable because the test class is a friend of
   `RemotePluginBase`) so the pre-existing block's own key send has a connection; (b) the python peer logs each
   message with `buffering=1` and only then answers, and the test waits for that answer through `plugin.process()`'s
   existing bounded period exchange - a happens-before edge, not a timeout: a completed period proves every earlier
   message arrived. Values are fixed (2 in / 2 out, the engine's frames per period).
3. "The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays
   consistent." - Cause: the key was only ever sent from the resize path (`updateAudioBuffer()`, now `:1013`) that
   returns early at `:969-972`; the connection point (`init()`, `:256`) sent only `IdSyncKey`. Callers of `init()`:
   `plugins/VstBase/VstPlugin.cpp:195` and `plugins/ZynAddSubFx/ZynAddSubFx.cpp:83`; both get the key from the same
   place. Activation paths: `waitForInitDone()` (`:431`) and the deferred IdInitDone in `process()` (`:949`) both run
   after `init()` has sent it. The sibling send (create path) is untouched and still sends the new block's key when a
   rebuild happens, and a first connection (no block) still sends nothing extra - so no connection sees two keys.
4. "No collateral damage: old projects and saved data still load as before, and any limit or check you added does not
   hit other callers that legitimately exceed it." - The change touches no save/load path, adds no limit and no
   validation: one extra message on a connection that reuses a block, nothing else. Only that connection's message
   stream changes, and the client's handler for the id exists for exactly this (`include/RemotePluginClient.h:299-301`).
5. "If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it." -
   Not applicable: the verdict is FIXED, with the code evidence in "Verdict evidence" above and the red/green runs in
   item 1.

- Realtime: `init()` runs on the thread that loads the plugin; `process()`/`processPeriod()` and
  `updateAudioBuffer()`'s shortcut are untouched, so nothing new allocates, locks, waits or logs on the audio thread.
- Unverified: no real two-process reinitialisation was driven (see the watcher-loop note under Other findings), and the
  change was not built for Windows/macOS here; the send is not behind any transport `#ifdef`, and the test compiles out
  on the `SYNC_WITH_SHM_FIFO` build (`RemotePluginAudioPortsShmCompileCheck` target, built with the suite).
