ALREADY-FIXED

# cluster new-p8-197: Dynamic remote audio-port changes can allocate during processing

The finding is real in its own terms and is already fixed on `bugs/qwen-new-p8-197`. A client's
channel-count change is no longer applied while the host is inside a period: `processMessage()` records
it in fixed fields and `applyDeferredClientRequests()` applies it on the main thread inside
`requestChangeInModel()`/`doneChangeInModel()`, so the `m_accessBuffer.resize()` the finding names runs on
a control thread, with rendering held off. No product file changed in this session.

## Code evidence

- The resize the finding names: `include/RemotePluginAudioPorts.h:173` (`m_accessBuffer.resize(channels)`).
  It runs inside `updateBuffers()` (`:149`), whose first statement is `const RemotePluginBufferChangeGuard guard;`
  (`:152`). That guard takes the audio engine's model-change mutex (`src/core/RemotePlugin.cpp:428-436`), which the
  render thread holds for a whole period (`AudioEngine::renderNextPeriod()`, `src/core/AudioEngine.cpp:1222`:
  `const auto lock = std::lock_guard{m_changeMutex};`). The resize and the `m_insOuts` pointer it publishes
  (`include/RemotePluginAudioPorts.h:185-192`) therefore cannot be read by the render thread while they change.
- The only audio-thread message pump is `RemotePlugin::collectProcessingReplies()`
  (`src/core/RemotePlugin.cpp:656-692`, dispatching at `:689`), reached from `processPeriod()` (`:545`, calls at
  `:581` and `:627`). `process()` sets `m_inProcessPeriod = true` around it (`:527`, cleared `:529`), so every
  message handled on that path knows it is on the audio thread.
- `case IdChangeInputOutputCount` (`src/core/RemotePlugin.cpp:1164-1208`) therefore takes the deferral branch:
  `if (m_inProcessPeriod)` (`:1195`) stores `m_pendingInCount`/`m_pendingOutCount`, sets `m_countChangePending`
  (`:1202`) and `m_deferredWork`, and `break`s before the inline
  `m_audioPorts->audioPortsModel().setChannelCounts(...)` at `:1206`. The period then renders silence: the
  pending count change stops the request (`:564`) and a change seen mid-period discards the reply's planes
  (`:639-646`).
- The apply happens on the main thread: `RemotePlugin::applyDeferredClientRequests()`
  (`src/core/RemotePlugin.cpp:695-728`) calls `Engine::audioEngine()->requestChangeInModel()` at `:706`, applies
  the deferred counts at `:722`, and releases at `:727`. Nesting is safe by construction: the change lock is a
  `std::recursive_mutex` (`include/AudioEngine.h:994`), so the deferral's own guard and the ports' guard nest.
- Out-of-range counts are refused before any resize: `:1176` bounds them by `MaxChannelsPerAudioBuffer` (128)
  and `:1192` fails the plugin, which is the bounded maximum the finding's "fix" line asks for.

The three commits that did this are ancestors of HEAD (`git merge-base --is-ancestor`):
`3152a763ea` (defer the count change off the period), `7de396b3a5` (apply it inside the model-change guard) and
`ec235fd96a` (the ports guard around every resize/view update).

## Test matrix (step 3)

| Path | Where it is covered | Status this session |
|---|---|---|
| normal period, no reconfiguration | `RemotePluginClientE2ETest` suite (`realtimePathsAllocateNothingInSteadyState`, `:905`) | PASS |
| a count change during a period is not applied there | `channelCountRequestDuringPeriodIsAppliedOnTheMainThread` (`tests/src/core/RemotePluginClientE2ETest.cpp:1406`) | PASS (green), FAIL when the deferral is reverted |
| reconfiguration applied afterwards on the main thread | same slot: the model reaches 4 outputs after the timer tick (`QTRY_COMPARE_WITH_TIMEOUT`, `:1440`) | PASS |
| audio-thread constraints (no allocation while pumping) | `clientMessageDuringPeriodIsParsedWithoutAllocating` (`:1490`) and `realtimePathsAllocateNothingInSteadyState` (`:905`) | PASS |
| failure / exhaustion (dead client, out-of-range count, late reply) | `RemotePluginClientE2ETest` ("die-on-start", "silent", "stall", "late") and `RemotePluginAudioPortsTest` | PASS (suite) |
| platform build requirement | the fake client is POSIX-only; the slots `QSKIP` without it and the shm-FIFO transport is guarded by `SYNC_WITH_SHM_FIFO` | built and run on Linux/Qt 6.12 |

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-197.r1/logs`.

| Command | Log | EXIT |
|---|---|---|
| `cmake --build build --target FakeRemotePluginClient RemotePluginClientE2ETest RemotePluginAudioPortsTest AllocationProbeTest -- -j4` | `build-1.log` | 0 |
| `ctest -R 'RemotePluginClientE2ETest\|RemotePluginAudioPortsTest\|AllocationProbeTest' --output-on-failure` (from `build/tests`) | `test-1.log` | 0 (`100% tests passed out of 3`) |
| `./RemotePluginClientE2ETest channelCountRequestDuringPeriodIsAppliedOnTheMainThread clientMessageDuringPeriodIsParsedWithoutAllocating` | `test-2-slot.log` | 0 (`Totals: 4 passed, 0 failed, 0 skipped`) |
| `./RemotePluginClientE2ETest realtimePathsAllocateNothingInSteadyState` | `test-6-allocslot.log` | 0 (`Totals: 3 passed, 0 failed`) |
| controlled revert of the deferral, then `cmake --build build --target RemotePluginClientE2ETest -- -j4` | `build-2-red.log` | 0 |
| `./RemotePluginClientE2ETest channelCountRequestDuringPeriodIsAppliedOnTheMainThread` (deferral reverted) | `test-4-red.log` | **1** |
| `git checkout -- src/core/RemotePlugin.cpp`, then `cmake --build build --target RemotePluginClientE2ETest -- -j4` | `build-3-green.log` | 0 |
| `./RemotePluginClientE2ETest channelCountRequestDuringPeriodIsAppliedOnTheMainThread` (restored) | `test-5-green.log` | 0 |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | 0 (`RESULT: PASS`, 8 of 8 gate rows PASS) |
| same gate run again at the committed tip | `gates-2.log` | 0 (`RESULT: PASS`, 8 of 8 gate rows PASS) |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p8-197` | `submit-1.log`, `submit-2.log` | 0 (`RESULT: PASS - submit`, all six items PASS) |

The red run's failure, verbatim from `test-4-red.log`:

```
FAIL!  : RemotePluginClientE2ETest::channelCountRequestDuringPeriodIsAppliedOnTheMainThread() '!plugin.process()' returned FALSE. ()
   Loc: [../../tests/src/core/RemotePluginClientE2ETest.cpp(1431)]
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 508ms
```

The green run, verbatim from `test-5-green.log`:

```
PASS   : RemotePluginClientE2ETest::channelCountRequestDuringPeriodIsAppliedOnTheMainThread()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 572ms
```

### The red run, exactly

The test predates this session, so "revert my change" has no meaning here. What I did instead is revert
*the existing fix* in the working tree, run the guarding test, and restore:

```
# revert: the deferral branch in case IdChangeInputOutputCount, `if (m_inProcessPeriod)` -> `if (false && m_inProcessPeriod)`
git diff -- src/core/RemotePlugin.cpp > .../logs/revert-probe.patch     # exit 0, patch kept in the log folder
cmake --build build --target RemotePluginClientE2ETest -- -j4          # build-2-red.log,  EXIT=0
./RemotePluginClientE2ETest channelCountRequestDuringPeriodIsAppliedOnTheMainThread   # test-4-red.log, EXIT=1
git checkout -- src/core/RemotePlugin.cpp                              # restored, `git status --porcelain` empty
cmake --build build --target RemotePluginClientE2ETest -- -j4          # build-3-green.log, EXIT=0
./RemotePluginClientE2ETest channelCountRequestDuringPeriodIsAppliedOnTheMainThread   # test-5-green.log, EXIT=0
```

Nothing from that probe is in the tree: `git status --porcelain` was empty after the restore, and the only
files this session adds are this report (and the manifest in the log folder).

## One command that failed for an unrelated reason (disclosed)

`./AllocationProbeTest realtimePathsAllocateNothingInSteadyState` (`test-3-allocprobe.log`, **EXIT=1**,
`Unknown test function`). `AllocationProbeTest` is only the probe harness (`countsAnAllocationMadeInLibraryCode`
etc.); the slot lives in `RemotePluginClientE2ETest`, which I then ran (`test-6-allocslot.log`, EXIT=0).

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** This packet
  makes no product change; the test guarding the finding already existed. I reverted the *existing* deferral for
  one build and observed the guarding test fail: `test-4-red.log` EXIT=1 at
  `RemotePluginClientE2ETest.cpp:1431` (`'!plugin.process()' returned FALSE` — the count change was applied inside
  the period, so the period completed instead of rendering silence). Restored: `test-5-green.log` EXIT=0. The
  patch I reverted is `logs/revert-probe.patch` (the reverse of the deferral hunk).
- **The test is deterministic: no wall-clock timing, sleeps or races.** The slot drives one
  `plugin.process()` with the fake client's fixed "recount" mode (the client sends `IdChangeInputOutputCount`
  — frame id 14 with two int arguments, visible in `test-2-slot.log` as `fakeclient: -> id 14 (2 args)` — and
  the slot's comment at `tests/src/core/RemotePluginClientE2ETest.cpp:1429` records that it asks for 4 outputs
  while sending no planes for them), then asserts `out().channelCount() == 2` and both planes zero for the frames
  of that period (`:1431-1437`), and finally waits for the
  main-thread apply with `QTRY_COMPARE_WITH_TIMEOUT(..., 2000)` (`:1440`) on a 10 ms application timer — an event-loop
  wait on a state transition, not a measurement of a duration. No sleep, no injected clock, no second thread
  inside the assertion. `realtimePathsAllocateNothingInSteadyState` counts allocations around `process()`,
  `processMidiEvent()` and `applyDeferredClientRequests()` and requires exactly 0.
- **The fix is at the cause and complete: every caller and sibling checked.** The cause is the count change being
  applied from the audio-thread pump. Callers of the resize: `bufferPropertiesChanging()` ->
  `CustomAudioPorts::updateBuffers()` (`include/AudioPorts.h:398-403`), `AudioPorts::init()` (`:329-337`) and
  `activate()` (`include/RemotePluginAudioPorts.h:106-110`, `:232-240`) — all control-thread callers, the last two
  from `RemotePlugin::waitForInitDone()` (`src/core/RemotePlugin.cpp:412-421`). Sibling path into the same
  function: `ConfigurableAudioPorts::updateBuffers()` (`include/RemotePluginAudioPorts.h:283-292`) delegates to the
  same guarded `RemotePluginAudioPorts::updateBuffers()` (remote) or to `PluginAudioPortsBuffer` (local, whose
  resize is equally control-thread only). The other in-period client messages that used to act inline are deferred
  the same way (`:1078` arity refusal, `:1150` retired id, `:1212` debug text, `:1231-1260` replies), so the
  pump is allocation-free as a whole and not only for this message.
- **No collateral damage.** No product, test, save-format or command file changed, so old projects and saved data
  load exactly as before (`git status --porcelain` empty before this report; the report is the only new file).
  The channel-count limit that exists (`MaxChannelsPerAudioBuffer`, `src/core/RemotePlugin.cpp:1175-1193`) predates
  this session and is unchanged; I added no limit. The wire protocol, the reply accounting and the silence-when-
  pending behaviour are untouched.
- **ALREADY-FIXED proof: the code that shows it.** `src/core/RemotePlugin.cpp:1195` (the in-period branch defers
  instead of applying) with `:1202` (`m_countChangePending`), `:705-727` (`applyDeferredClientRequests()` applies
  it on the main thread under `requestChangeInModel()`), `include/RemotePluginAudioPorts.h:152` + `:173` (the
  resize runs under `RemotePluginBufferChangeGuard`, and `AudioEngine::renderNextPeriod()` holds `m_changeMutex`
  for the period, `src/core/AudioEngine.cpp:1222`). Ancestry checked: `3152a763ea`, `7de396b3a5`, `ec235fd96a`
  are ancestors of HEAD (`git merge-base --is-ancestor`, all three printed `ancestor=yes`).
- **Sibling commands** (`app.about`, `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`,
  `app.tour`, `app.tours`, `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`,
  `app.whats_new`, `arrangement.fit_tracks`, `arrangement.get_state`): not affected. This session's only changed
  file is this report - no `ControlCommands*`, `ControlRegistry`, `ControlReversibilityTable` or
  `tools/mcp-zene-control/zene_control/commands_snapshot.json` file is in it, so the `ControlCommandsSnapshot`
  canary cannot move and no command's behaviour or reversibility class changes. Those siblings also do not reach
  the changed code: the deferral lives in `RemotePlugin::processMessage()` / `applyDeferredClientRequests()`, and
  no `app.*` or `arrangement.*` handler drives a remote plugin's period (`submit_check.py` lists them because it
  reads the 652-file merged branch history against `050/m0-train-35`, not this session's diff).

## Other findings

- `tests/QA-GATES.md:2058-2074` still records this defect as "Known and deliberately not fixed, scheduled for
  the next release" (item (a): `IdChangeInputOutputCount` applied inline on the audio thread by the message pump
  inside `process()`). That paragraph is stale at this tip — the deferral landed in `3152a763ea`/`7de396b3a5`.
  I left it alone because this packet writes only its report; the doc should be corrected by whoever owns it.
  The same stale claim is not present in `docs/m0-blocker-fixes-2026-10-08/D-hosting.md:15`, which already
  records "new-p8-197 deferred apply — FIXED — `3152a763e`, `7de396b3a`".
- Item (b) of that QA-GATES paragraph (the same rebuild reachable from a GUI-thread pump through the VstPlugin
  `waitForMessage(..., busyWaiting=true)` helpers) is a control-thread allocation by design and is not an
  audio-thread defect; it is out of scope here.
- The shared-memory FIFO transport (`SYNC_WITH_SHM_FIFO`, Windows) compiles the same unified `process()`, so the
  deferral covers it too, but it was not run on this box (no Windows toolchain); the socket path is what I ran.
