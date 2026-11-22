ALREADY-FIXED

# Cluster 3418: Remote processing blocks the audio callback on a mutex and child reply

3418: ALREADY-FIXED

## Finding
`RemotePlugin::process()` locked a mutex, sent `IdStartProcessing`, and waited without a bound for
`IdProcessingDone`. A slow or hung client, or another thread holding the mutex, stalled the audio callback.

## Why it is already fixed on this branch
Earlier commits that name this cluster already made the fix the finding asks for: a bounded, nonblocking
handoff that renders silence when the reply is late.
- `3152a763ea` fix(hosting): bound the host's processing wait and move audio-thread client work off the period (cluster 3417, 3418, new-p8-197)
- `20db042a07` fix(hosting): audio-thread sends never block; the period wait is budgeted from the render start (cluster 3418 review B5)
- `7711cacb85` cluster new-p12-267: keep the audio-thread reply wait inside the render period

Code on this branch (HEAD e695f84149):
- `src/core/RemotePlugin.cpp:521`: `process()` uses `m_commMutex.tryLock()`. If the mutex is busy, the period is filled with silence and returns `false`. It never waits on the mutex.
- `src/core/RemotePlugin.cpp:573`: a partly written earlier request is flushed without blocking (`flushPendingSend()` try-locks the send mutex, `src/common/RemotePluginBase.cpp:286-293`). If it cannot be flushed, the period is skipped.
- `src/core/RemotePlugin.cpp:580-589`: a reply still owed from an earlier period is drained without waiting. While it is outstanding, no new request is sent.
- `src/core/RemotePlugin.cpp:611`: the request goes out through `sendFrameNonBlocking()` (`src/common/RemotePluginBase.cpp:162`: try-lock, room check, MSG_DONTWAIT on the socket path; try-locks on the SHM FIFO path). No room means the period is skipped.
- `src/core/RemotePlugin.cpp:627`: the reply wait is bounded by `periodDeadline(start)`, which is what remains of the render period (`src/core/RemotePlugin.cpp:492-504`).
- `src/core/RemotePlugin.cpp:681`: when the deadline passes, the wait returns `ReplyWait::Late`. The period stays silent, because `m_hostOutputs` was zeroed at line 549. The overrun is counted, and the reply stays owed for the next period to drain.
- `src/common/RemotePluginBase.cpp:627-690`: `receiveFrameBefore()` waits only until the deadline on both transports.

The wait is bounded and stays inside the period. That is the finding's own suggested fix ("a bounded
nonblocking handoff that outputs silence when a reply is late"). No product change is needed.

Callers checked: `plugins/ZynAddSubFx/ZynAddSubFx.cpp:367`, `plugins/VstEffect/VstEffect.cpp:93` and
`plugins/Vestige/Vestige.cpp:422`. Each calls `process()` directly and does not take the mutex itself.

## Existing regression tests (tests/src/core/RemotePluginClientE2ETest.cpp)
- `stalledClientCannotHoldTheAudioPeriod` (line 613): a client that never replies. `process()` returns quickly with silence, and the next period sends no new request.
- `lateClientCannotReachTheInstrumentOutput` (line 739): a late reply never reaches the instrument output.
- `clientStoppingMidMessageCannotHoldThePeriod` (line 779): a client that stops mid-message cannot hold the period.
- `lateReplyIsDrainedAndNeverUsedForTheNextPeriod` (line 863): an owed reply is drained and is not used for the next period.
- `uiQueryDuringRenderingDoesNotBlockThePeriod` (line 956): the main thread holds the communication mutex for queries while periods render. Those periods are skipped, not blocked.

## Commands run (logs in /home/kruzzzzy/.config/kads/zene/state/runs/3418.r1/logs)
- `cmake --build build --target zene RemotePluginClientE2ETest FakeRemotePluginClient -j4 > logs/build-1.log 2>&1` gave EXIT=0
- `ctest -R RemotePlugin --output-on-failure > logs/test-1.log` (from build/tests) gave EXIT=8. RemotePluginAudioPortsTest and RemotePluginShmFifoTest were "Not Run" because their executables had not been built yet. RemotePluginClientE2ETest passed.
- `cmake --build build --target RemotePluginAudioPortsTest RemotePluginShmFifoTest -j4 > logs/build-2.log 2>&1` gave EXIT=0
- `ctest -R RemotePlugin --output-on-failure > logs/test-2.log` gave EXIT=0, with 4/4 passed.
- `ctest -R RemotePluginClientE2ETest -V > logs/test-3.log` gave EXIT=0, with "Totals: 33 passed, 0 failed, 0 skipped". All five tests listed above show PASS.
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log` gave EXIT=0, RESULT: PASS. Every gate passed.

No product code was changed, so there is no new regression test and no red/green run. Earlier commits already
added the regression tests listed above (see `858a910e07` for the RED test of the period deadline).

## Self-check
- Verdict evidence: the file:line references above were read this session at HEAD e695f84149.
- Realtime: no code changed. The existing path takes no blocking lock and allocates nothing (frames are stack-encoded, lines 609-610). It waits only until the period deadline.
- Concurrency: local TSan does not run on this box. This verdict rests on the design reading above (only try-locks are taken on the audio thread) and on the E2E tests.
- Registration: no product file was added or changed.

## Other findings
- None.
