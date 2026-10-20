# Cluster new-p15-172: the audio thread reads `QProcess` state from another thread

**Verdict: FIXED**

new-p15-172: FIXED
new-p14-255: FIXED
new-p14-256: FIXED

Same-file batch: three bugs in `include/RemotePlugin.h` (the RPC base of the
out-of-process plugin hosting). Each one is decided, fixed and proved on its
own; the evidence tables are per bug.

---

## 1. new-p15-172 - the audio thread reads `QProcess` state from another thread

**Verdict: FIXED**

### The bug (REAL)

`RemotePlugin::isRunning()` read the `QProcess`' own state:

* `include/RemotePlugin.h:118-125` (before the fix) - `isRunning()` returned
  `m_process.state() != QProcess::NotRunning`.
* `src/core/RemotePlugin.cpp:319` (before the fix) - `init()` does
  `m_process.moveToThread(&m_watcher)`, so from that moment the `QProcess`
  object lives in the **watcher thread**.
* `src/core/RemotePlugin.cpp:470` (before the fix) - `process()` calls
  `isRunning()`, and `process()` is the **audio thread**'s period function
  (called from the instrument hosts' `playImpl()`).

`QProcess::state()` returns `d->processState`, a plain enum member that the
process' own thread updates. Reading it from the audio thread is therefore
(a) an unsynchronized read of another thread's data (a C++ data race) and
(b) a read of a `QObject` that does not live in the calling thread - the
`QProcess` was explicitly moved to the watcher thread to work around
QTBUG-8819. The state was read on every audio period.

### The fix (at the cause)

The running state is cached in an atomic that the process lifecycle updates,
so the audio thread never touches the `QProcess`:

* `include/RemotePlugin.h:118-130` - `isRunning()` now returns
  `m_clientRunning.load(std::memory_order_acquire)` (the `DEBUG_REMOTE_PLUGIN`
  branch is unchanged).
* `include/RemotePlugin.h:304-309` - the new member
  `std::atomic<bool> m_clientRunning{false}`: *"The QProcess lives in the
  watcher thread (init() moves it there) and the audio thread reads
  isRunning(), so the state is cached here instead of being read from the
  QProcess across those threads."*
* `src/core/RemotePlugin.cpp:99-106` - `ProcessWatcher::run()` sets the cached
  state **before** `process.start(...)`, i.e. exactly where the client comes to
  life and on the thread that owns the `QProcess`.
* `src/core/RemotePlugin.cpp:955-959` - `processFinished()` clears it: the
  client is gone.
* `src/core/RemotePlugin.cpp:996-1000` - `processErrored()` clears it for
  `QProcess::FailedToStart`, the one error that is *not* followed by
  `finished()`.
* `src/core/RemotePlugin.cpp:273-276` - `init()` clears it when a reused
  plugin starts a new connection (the old client's state must not carry over).

The semantics are preserved exactly: the old code answered `true` for
`QProcess::Starting` and `QProcess::Running` and `false` for
`QProcess::NotRunning`; the cache is set where the process is started (the
`Starting` window included) and cleared where it ends.

**Realtime:** `std::atomic<bool>` is lock-free; the audio thread only *loads*
it. Nothing is allocated, freed, locked, waited for or logged on the audio
path, and the load replaces a `QProcess::state()` call, so the audio thread
does strictly less work than before.

**Siblings checked:** `m_process.state()` is read in exactly two places -
`isRunning()` (fixed) and `~RemotePlugin()` (`src/core/RemotePlugin.cpp:228`).
The destructor's read is a *same-thread* read: `m_watcher.wait()` has returned,
so `ProcessWatcher::run()` has already moved the `QProcess` back to the
plugin's thread (`src/core/RemotePlugin.cpp:126`) - it is left as it is.
`hostProcessId()` (`include/RemotePlugin.h:275`) also reads the `QProcess`,
but it is a deliberate, documented query ("*deliberately a query and not a
cached copy*") and its only callers are main-thread ones
(`plugins/ZynAddSubFx/ZynAddSubFx.cpp:450`,
`src/core/ControlCommandsOutOfProcessSupport.cpp:181`) - not the audio thread
this bug is about. It is left as it is.

### The regression

`tests/src/core/RemotePluginAudioPortsTest.cpp`:

* `runningStateIsCachedNotReadFromTheProcess()` (line 360) - `isRunning()`
  answers from the **cache**, not from the `QProcess`. The `QProcess` in the
  test was never started, so its own state is "not running"; a `QProcess` read
  would answer "not running" even with the cache saying "running".
* `cachedRunningStateFollowsTheProcessLifecycle()` (line 390) - the cached
  state follows the client's lifecycle: the `QProcess`' `finished()` signal
  (which the host's `processFinished()` slot is connected to directly) clears
  it.

The seam is the `LMMS_TESTING` friend declaration in
`include/RemotePlugin.h:472-477` and the fail-closed probe
`CachedRunningState` in the test file (lines 288-317). The probe's primary
template is selected whenever the cached state is not reachable from the test
(no friend seam, i.e. the unfixed tree), so **both tests fail at run time in
the unfixed tree instead of failing to compile** - the unfixed tree is exactly
what the RED proof below rebuilds.

Neither test waits for a clock, sleeps, or races: the cache is steered
directly and the lifecycle signal is emitted directly (`QProcess::finished()`
is a public signal), so both are deterministic.

### Evidence (new-p15-172)

All ctest invocations ran from `build/tests`. The affected ctest family
(`ctest -R 'RemotePlugin|ZynSeparateProcess'`) contains 3 tests; the fixed runs
executed 3/3 successfully.

| Check | Command (exact) | Exit/result |
|---|---|---|
| GREEN build after the fix (`build-2-p15-172.log`) | `cmake --build build --target RemotePluginAudioPortsTest -j 20 > .../build-2-p15-172.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after the fix (`test-2-p15-172.log`) | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../test-2-p15-172.log 2>&1; echo EXIT=$?` | **0**; 1/1 passed |
| PROOF: remove the product patch (`fix.patch`) | `git diff -- include/RemotePlugin.h src/core/RemotePlugin.cpp > .../fix.patch && git apply -R .../fix.patch; echo EXIT=$?` | 0 (103 lines) |
| PROOF: rebuild without the fix (`build-3-p15-172-red.log`) | `cmake --build build --target RemotePluginAudioPortsTest -j 20 > .../build-3-p15-172-red.log 2>&1; echo EXIT=$?` | 0 |
| PROOF: RED without the fix (`test-3-p15-172-red.log`) | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../test-3-p15-172-red.log 2>&1; echo EXIT=$?` | **8**; `runningStateIsCachedNotReadFromTheProcess()` - *"'CachedRunningState&lt;RemotePlugin&gt;::steer(plugin, true)' returned FALSE. (the remote plugin does not cache its running state - isRunning() reads the QProcess)"*; `cachedRunningStateFollowsTheProcessLifecycle()` - same assertion; 21 passed, 2 failed |
| PROOF: restore the patch (`build-4-p15-172-green.log`) | `git apply .../fix.patch; echo EXIT=$?` then `cmake --build build --target RemotePluginAudioPortsTest -j 20 > .../build-4-p15-172-green.log 2>&1; echo EXIT=$?` | 0; 0 |
| PROOF: GREEN with the fix again (`test-4-p15-172-green.log`) | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../test-4-p15-172-green.log 2>&1; echo EXIT=$?` | **0**; 1/1 passed |
| Dependent rebuild, every target that includes the header (`build-5-p15-172-deps.log`) | `cmake --build build --target zene lmmsobjs RemotePluginAudioPortsTest RemotePluginClientE2ETest FakeRemotePluginClient ZynSeparateProcessTest vestige vsteffect ZynAddSubFxCore RemoteZynAddSubFx RemoteVstPlugin64 NativeLinuxRemoteVstPlugin64 -j 20 > .../build-5-p15-172-deps.log 2>&1; echo EXIT=$?` | 0 |
| Affected-family run (`test-5-p15-172-family.log`) | `ctest -R 'RemotePlugin\|ZynSeparateProcess' --output-on-failure > .../test-5-p15-172-family.log 2>&1; echo EXIT=$?` | **0**; 3/3 passed (RemotePluginAudioPortsTest, RemotePluginClientE2ETest, ZynSeparateProcessTest) |

`...` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-172.r1/logs`.

---

## 2. new-p14-255 - waiting for host info can erase an earlier protocol failure

**Verdict: FIXED**

### The bug (REAL)

`include/RemotePlugin.h:129-133` (before the fix) -

```cpp
	inline void waitForHostInfoGotten()
	{
		m_failed = waitForMessage( IdHostInfoGotten ).id
							!= IdHostInfoGotten;
	}
```

`waitForMessage()` reads the client's messages and hands each one to
`processMessage()`, and `processMessage()` is what *fails* the plugin on a
protocol mismatch: the retired single-count ids (`src/core/RemotePlugin.cpp:
1052-1064`) and a message with too few arguments
(`src/core/RemotePlugin.cpp:1015-1020`) both set `m_failed = true`. The
assignment above then **overwrites** that verdict with the wait's own result:
the client sent `IdHostInfoGotten`, so `m_failed` became `false` again and the
plugin carried on as if nothing had happened.

The sibling `waitForInitDone()` (`src/core/RemotePlugin.cpp:402-403`) has
always preserved it (`m_failed = m_failed || !initDone;`) - with the comment
that the verdict of a protocol mismatch has to be sticky, because the failure
path otherwise degrades into the silent wrong audio it exists to prevent.
`waitForHostInfoGotten()` is the same wait on the same flag and had lost that
property.

Callers: `plugins/VstBase/VstPlugin.cpp:217` (`VstPlugin::tryLoad()`, the VST
handshake) is the only one in the tree.

### The fix (at the cause)

`include/RemotePlugin.h:134-143` -

```cpp
	inline void waitForHostInfoGotten()
	{
		const bool gotHostInfo = waitForMessage( IdHostInfoGotten ).id == IdHostInfoGotten;
		m_failed = m_failed || !gotHostInfo;
	}
```

The wait's own result is now *or-ed* into the existing failure state, exactly
as `waitForInitDone()` does it. A plugin that was failed while the wait was
running stays failed; a plugin that was not failed is failed when the host
info does not arrive (unchanged).

**Realtime / undo / commands:** the wait runs on the main thread (the VST
plugin's load path), the flag is an existing `std::atomic<bool>`, no new
allocation, lock, wait or log was added, and no command was added.

### The regression

`tests/src/core/RemotePluginClientE2ETest.cpp:1295` -
`hostInfoWaitKeepsAnEarlierProtocolFailure()`: the stand-in client
(`tests/src/plugins/FakeRemotePluginClient.cpp`, the new `badhostinfo` mode)
answers the host's `IdSyncKey` with the **retired** `IdChangeInputCount` id
first and only then with `IdHostInfoGotten`. The host refuses the retired id
(refused loudly, `m_failed = true`) and then waits for the host info - and
the failure must survive that wait. The test asserts both halves: the refusal
was reported ("removed message id") and the plugin is still failed.

The test is deterministic: the client sends its three messages in a fixed
order, the host reads them in that order, and nothing waits for a clock.

### Evidence (new-p14-255)

| Check | Command (exact) | Exit/result |
|---|---|---|
| GREEN build after the fix (`build-6-p14-255.log`) | `cmake --build build --target RemotePluginClientE2ETest FakeRemotePluginClient -j 20 > .../build-6-p14-255.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after the fix (`test-6-p14-255.log`) | `ctest -R '^RemotePluginClientE2ETest$' --output-on-failure > .../test-6-p14-255.log 2>&1; echo EXIT=$?` | **0**; 1/1 passed |
| PROOF: remove the product patch (`fix.patch`) | `git diff -- include/RemotePlugin.h > .../fix.patch && git apply -R .../fix.patch; echo EXIT=$?` | 0 |
| PROOF: rebuild without the fix (`build-8-p14-255-red.log`) | `cmake --build build --target RemotePluginClientE2ETest -j 8 > .../build-8-p14-255-red.log 2>&1; echo EXIT=$?` | 0 |
| PROOF: RED without the fix (`test-8-p14-255-red.log`) | `ctest -R '^RemotePluginClientE2ETest$' --output-on-failure > .../test-8-p14-255-red.log 2>&1; echo EXIT=$?` | **8**; `hostInfoWaitKeepsAnEarlierProtocolFailure()` - *"'plugin.failed()' returned FALSE. (the protocol failure was erased by the host-info wait)"*; 26 passed, 1 failed |
| PROOF: restore the patch (`build-9-p14-255-green.log`) | `git apply .../fix.patch; echo EXIT=$?` then `cmake --build build --target RemotePluginClientE2ETest -j 8 > .../build-9-p14-255-green.log 2>&1; echo EXIT=$?` | 0; 0 |
| PROOF: GREEN with the fix again (`test-9-p14-255-green.log`) | `ctest -R '^RemotePluginClientE2ETest$' --output-on-failure > .../test-9-p14-255-green.log 2>&1; echo EXIT=$?` | **0**; 1/1 passed |

---

## 3. new-p14-256 - a sample-rate update is treated as successful after its reply times out

**Verdict: FIXED**

### The bug (REAL)

`include/RemotePlugin.h:190-196` (before the fix) -

```cpp
	void updateSampleRate( sample_rate_t _sr )
	{
		lock();
		sendMessage( message( IdSampleRateInformation ).addInt( _sr ) );
		waitForReplyWithin( message( IdInformationUpdated ) );
		unlock();
	}
```

`waitForReplyWithin()` is the **bounded** wait: it gives up after
`QueryReplyTimeout` (100 ms, `include/RemotePlugin.h:209`) and returns an
undefined message. The reply was thrown away, so the host went on rendering at
the new rate while the client - which never acknowledged `IdSampleRateInformation`
- went on rendering at the old one. The two sides then disagree about the
sample rate, which is the silent wrong audio the acknowledgement exists to
prevent; the host has no way to tell the client to go back.

The client half of the protocol answers this message with `IdInformationUpdated`
(`include/RemotePluginClient.h:261-269`), so a client that does not answer is
broken, busy or gone.

Caller: `plugins/ZynAddSubFx/ZynAddSubFx.cpp:558` (the Zyn instrument's
out-of-process host) is the only one in the tree.

### The fix (at the cause)

The reply is now checked, and a client that does not acknowledge the new rate
fails the plugin (which silences it - `process()` renders silence for a failed
plugin, `src/core/RemotePlugin.cpp:470-474`) instead of continuing with two
different rates:

* `include/RemotePlugin.h:200-205` - the declaration, with the contract in the
  comment.
* `src/core/RemotePlugin.cpp:801-816` - the definition:
  `waitForReplyWithin()`'s result is compared with `IdInformationUpdated`; an
  unanswered update logs one `qCritical` line and sets `m_failed = true`.

The function moved out of line (it was inline in the header) so that the
diagnostic can be logged from the .cpp, which already includes `<QDebug>`; the
header and the definition changed together.

**Realtime / undo / commands:** the call is a main-thread event (a rate
change), the flag is the existing `std::atomic<bool> m_failed`, no new
allocation, lock, wait or command was added, and the bounded wait itself is
unchanged (100 ms, as before).

### The regression

`tests/src/core/RemotePluginClientE2ETest.cpp:1335` -
`unansweredSampleRateUpdateFailsThePlugin()`: the stand-in client
(`tests/src/plugins/FakeRemotePluginClient.cpp`, the new `deafsamplerate` mode)
never acknowledges a sample-rate update, and the plugin must be failed.

The positive control is the same test's sibling
`acknowledgedSampleRateUpdateKeepsThePlugin()` (line 1363): the stand-in client
now answers `IdSampleRateInformation` with `IdInformationUpdated` exactly like
the real client does, and a plugin whose client acknowledged the new rate keeps
running. The fixture change is what makes the control meaningful - before it,
the stand-in never answered and the control could not have passed.

The test is deterministic: the client either answers or it does not, and the
host's bounded wait is the production behaviour being tested (the verdict does
not depend on how long the wait took).

### Evidence (new-p14-256)

| Check | Command (exact) | Exit/result |
|---|---|---|
| GREEN build after the fix (`build-10-p14-256.log`) | `cmake --build build --target RemotePluginClientE2ETest FakeRemotePluginClient zene -j 8 > .../build-10-p14-256.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after the fix (`test-10-p14-256.log`) | `ctest -R '^RemotePluginClientE2ETest$' --output-on-failure > .../test-10-p14-256.log 2>&1; echo EXIT=$?` | **0**; 1/1 passed |
| PROOF: remove the product patch (`fix.patch`) | `git diff -- include/RemotePlugin.h src/core/RemotePlugin.cpp > .../fix.patch && git apply -R .../fix.patch; echo EXIT=$?` | 0 (both the declaration and the definition) |
| PROOF: rebuild without the fix (`build-11-p14-256-red.log`) | `cmake --build build --target RemotePluginClientE2ETest -j 8 > .../build-11-p14-256-red.log 2>&1; echo EXIT=$?` | 0 |
| PROOF: RED without the fix (`test-11-p14-256-red.log`) | `ctest -R '^RemotePluginClientE2ETest$' --output-on-failure > .../test-11-p14-256-red.log 2>&1; echo EXIT=$?` | **8**; `unansweredSampleRateUpdateFailsThePlugin()` - *"'plugin.failed()' returned FALSE. (an unacknowledged sample-rate update was treated as successful)"*; 28 passed, 1 failed |
| PROOF: restore the patch (`build-12-p14-256-green.log`) | `git apply .../fix.patch; echo EXIT=$?` then `cmake --build build --target RemotePluginClientE2ETest -j 8 > .../build-12-p14-256-green.log 2>&1; echo EXIT=$?` | 0; 0 |
| PROOF: GREEN with the fix again (`test-12-p14-256-green.log`) | `ctest -R '^RemotePluginClientE2ETest$' --output-on-failure > .../test-12-p14-256-green.log 2>&1; echo EXIT=$?` | **0**; 1/1 passed |
| Dependent rebuild, every target that includes the header (`build-13-p14-256-deps.log`) | `cmake --build build --target zene lmmsobjs RemotePluginAudioPortsTest RemotePluginClientE2ETest FakeRemotePluginClient ZynSeparateProcessTest vestige vsteffect ZynAddSubFxCore RemoteZynAddSubFx RemoteVstPlugin64 NativeLinuxRemoteVstPlugin64 -j 8 > .../build-13-p14-256-deps.log 2>&1; echo EXIT=$?` | 0 |
| Affected-family run (`test-13-p14-256-family.log`) | `ctest -R 'RemotePlugin\|ZynSeparateProcess' --output-on-failure > .../test-13-p14-256-family.log 2>&1; echo EXIT=$?` | **0**; 3/3 passed (RemotePluginAudioPortsTest, RemotePluginClientE2ETest, ZynSeparateProcessTest) |
| The **real** client acknowledges the rate (no collateral damage) | `./RemotePluginClientE2ETest` (29 passed, 0 skipped - the real-client case runs) and `./ZynSeparateProcessTest` (6 passed; no "did not acknowledge" line) | 0; 0 |

---

## Self-check

**1. Does each regression test FAIL with the product change reverted and PASS with
it?**

Yes, one RED/GREEN proof per bug, each with the product patch reversed and
re-applied (`git apply -R` / `git apply` of the recorded `fix.patch`):

| Bug | RED (product patch reverted) | GREEN (patch restored) |
|---|---|---|
| new-p15-172 | `ctest -R '^RemotePluginAudioPortsTest$'` -> **exit 8**, `runningStateIsCachedNotReadFromTheProcess()` and `cachedRunningStateFollowsTheProcessLifecycle()` both fail | `ctest -R '^RemotePluginAudioPortsTest$'` -> **exit 0** |
| new-p14-255 | `ctest -R '^RemotePluginClientE2ETest$'` -> **exit 8**, `hostInfoWaitKeepsAnEarlierProtocolFailure()` fails | `ctest -R '^RemotePluginClientE2ETest$'` -> **exit 0** |
| new-p14-256 | `ctest -R '^RemotePluginClientE2ETest$'` -> **exit 8**, `unansweredSampleRateUpdateFailsThePlugin()` fails | `ctest -R '^RemotePluginClientE2ETest$'` -> **exit 0** |

**2. Is the test deterministic - no wall-clock timing, sleeps or races?**

Yes. The seams are:

* new-p15-172: the `LMMS_TESTING` friend declaration in
  `include/RemotePlugin.h` lets the test steer the cached running state
  (`CachedRunningState::steer()`) and emit the `QProcess`' `finished()` signal
  (`CachedRunningState::finishProcess()`) directly. The probe is fail-closed,
  so in the unfixed tree the test **fails at run time** (it does not fail to
  compile) - which is what the RED proof above shows.
* new-p14-255: the stand-in client sends a fixed message sequence (retired id,
  then `IdHostInfoGotten`); the host reads them in that order. No clock.
* new-p14-256: the stand-in client either answers the rate update or does not;
  the host's 100 ms bound is the production behaviour under test, and the
  verdict does not depend on how long the wait took. The positive control
  proves the other branch.

**3. Is the fix at the cause and complete - every caller and sibling checked?**

* new-p15-172: `m_process.state()` is read in exactly two places -
  `isRunning()` (fixed) and `~RemotePlugin()` (left alone on purpose: after
  `m_watcher.wait()` the `QProcess` has been moved back to the plugin's thread
  by `ProcessWatcher::run()`, so that read is same-thread).
  `hostProcessId()` also reads the `QProcess`, but its header documents it as
  a deliberate query and its callers are main-thread ones
  (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:450`,
  `src/core/ControlCommandsOutOfProcessSupport.cpp:181`) - not the audio thread
  this bug is about.
* new-p14-255: the sibling `waitForInitDone()` (`src/core/RemotePlugin.cpp:
  402-403`) already had the "preserve the failure" contract; the fix makes
  `waitForHostInfoGotten()` match it. The only caller is
  `plugins/VstBase/VstPlugin.cpp:217`, which reads `failed()` right after the
  wait - so the preserved verdict is what that check now sees.
* new-p14-256: the only caller is `plugins/ZynAddSubFx/ZynAddSubFx.cpp:558`;
  the client half that has to answer is `include/RemotePluginClient.h:261-269`
  (`IdSampleRateInformation` -> `IdInformationUpdated`), and both in-tree
  remote clients use it. The VST path has its own `updateSampleRate()`
  (`plugins/VstBase/VstPlugin.cpp:349`) and is not touched.

**4. No collateral damage?**

* No project data, save format or command surface is touched: the three
  changes are in the remote-plugin RPC layer. No command was added, nothing
  on the audio thread allocates, frees, locks, waits or logs (the new atomic
  load replaces a `QProcess::state()` call).
* The new failure verdict in `updateSampleRate()` only fires for a client that
  does not acknowledge the rate; the real client does, and the real-client
  cases pass (29/29 in `RemotePluginClientE2ETest`, including the
  `RemoteZynAddSubFx` case that calls `updateSampleRate()`; 6/6 in
  `ZynSeparateProcessTest`, with no "did not acknowledge" line).
* The new failure verdict in `waitForHostInfoGotten()` only fires for a client
  that violates the protocol; the VST handshake is unchanged for a well-behaved
  client.
* The cached running state follows the client exactly as `QProcess::state()`
  did (true from the start of the process until it ends), so no caller sees a
  different answer than before.
* One pre-existing flake was found and is **not** caused by this batch:
  `RemotePluginClientE2ETest::midiSentPerPeriodIsCapped()` fails under heavy
  machine load (the host's non-blocking send finds no room, so a period is
  skipped and the client sees more than one period's MIDI events). Measured
  with the pristine `HEAD~1` product code and with this branch's code, 8
  interleaved runs each under 16 spinners: 6/8 failures on both
  (`logs/flake-capper-pre-existing.log`).

**5. Verdict evidence (ALREADY-FIXED / FALSE-POSITIVE)?**

Not applicable - all three bugs are REAL and FIXED; the evidence for each is
the code quoted above (the pre-fix source lines) plus the RED/GREEN proof.
