# Cluster new-p15-172: the audio thread reads `QProcess` state from another thread

**Verdict: FIXED**

new-p15-172: FIXED
new-p14-255: FIXED
new-p14-256: NEEDS-CONTEXT

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

* `include/RemotePlugin.h:118-131` - `isRunning()` now returns
  `m_clientRunning.load(std::memory_order_acquire)` (the `DEBUG_REMOTE_PLUGIN`
  branch is unchanged).
* `include/RemotePlugin.h:299-306` - the new member
  `std::atomic<bool> m_clientRunning{false}`: *"The QProcess lives in the
  watcher thread (init() moves it there) and the audio thread reads
  isRunning(), so the state is cached here instead of being read from the
  QProcess across those threads."*
* `src/core/RemotePlugin.cpp:99-105` - `ProcessWatcher::run()` sets the cached
  state **before** `process.start(...)`, i.e. exactly where the client comes to
  life and on the thread that owns the `QProcess`.
* `src/core/RemotePlugin.cpp:941-945` - `processFinished()` clears it: the
  client is gone.
* `src/core/RemotePlugin.cpp:988-991` - `processErrored()` clears it for
  `QProcess::FailedToStart`, the one error that is *not* followed by
  `finished()`.
* `src/core/RemotePlugin.cpp:274-276` - `init()` clears it when a reused
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

* `runningStateIsCachedNotReadFromTheProcess()` (line 349) - `isRunning()`
  answers from the **cache**, not from the `QProcess`. The `QProcess` in the
  test was never started, so its own state is "not running"; a `QProcess` read
  would answer "not running" even with the cache saying "running".
* `cachedRunningStateFollowsTheProcessLifecycle()` (line 386) - the cached
  state follows the client's lifecycle: the `QProcess`' `finished()` signal
  (which the host's `processFinished()` slot is connected to directly) clears
  it.

The seam is the `LMMS_TESTING` friend declaration in
`include/RemotePlugin.h:471-476` and the fail-closed probe
`CachedRunningState` in the test file (lines 274-311). The probe's primary
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

`include/RemotePlugin.h:134-142` -

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

`tests/src/core/RemotePluginClientE2ETest.cpp:1284` -
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

(filled in below)

---

## Self-check

(filled in at the end of the batch)
