# M0 blocker fix: AudioPortsModelTest heap corruption

## Finding

`AudioPortsModelTest::initTestCase()` called `Engine::init(true)` but the test
class had no `cleanupTestCase()`. Engine initialization starts the `AudioDummy`
processing loop and its worker pool. On process exit, the still-running
`AudioDummy` thread reached `AudioEngineWorkerThread::drainDeferredPlayHandleEdits()`
and iterated the static `workerThreads` list while static teardown was freeing
that list.

AddressSanitizer reported the first invalid access as a 4-byte heap-use-after-free
read in thread `T25 (lmms::AudioDumm)`. Its stack was:

`QAtomicOps::loadRelaxed` → `QArrayData::needsDetach` →
`QList<AudioEngineWorkerThread*>::begin` →
`AudioEngineWorkerThread::drainDeferredPlayHandleEdits()` →
`AudioEngineWorkerThread::startAndWaitForJobs()` →
`AudioEngine::renderStageInstruments()` → `AudioDummy::run()`.

The read is at `src/core/AudioEngineWorkerThread.cpp:96`; the static list is
defined at line 46. The omitted teardown was at
`tests/src/core/AudioPortsModelTest.cpp:90`. Calling `Engine::destroy()` from
`cleanupTestCase()` stops processing and joins the engine workers before static
teardown (`src/core/Engine.cpp:96-112`, `src/core/AudioEngine.cpp:169-197`).
This also prevents the gcov exit handler from encountering the corrupted heap.

## Verification

The ASan tree used the requested Debug/Qt6 flags and built only
`AudioPortsModelTest` and its dependencies. Its initial run exposed the
use-after-free during process teardown; LeakSanitizer then stalled while the
audio thread was still live. With the cleanup fix, `AudioPortsModelTest` passed
under ASan with leak detection enabled.

The other port/remote-plugin tests passed under ASan with no additional
findings: `AudioPortsTest`, `PluginAudioPortsTest`,
`RemotePluginAudioPortsTest`, and `RemotePluginClientE2ETest`. The temporary
`build-asan` tree was removed after these runs.

The Debug coverage red run, after reversing only the cleanup change, passed all
18 test cases and printed `corrupted double-linked list` followed by SIGABRT
during gcov shutdown. QtTest's post-signal stack handling then remained hung;
the process was terminated after about 206 seconds and CTest returned 8. With
the cleanup restored, the same coverage test passed and CTest returned 0.

| Cluster | Verdict | Fix commit | Test | Red exit | Green exit | Gate exits |
| --- | --- | --- | --- | --- | --- | --- |
| heap-audioports | FIXED | `1dceccb2d` | `AudioPortsModelTest` (Debug coverage; ASan); `AudioPortsTest`, `PluginAudioPortsTest`, `RemotePluginAudioPortsTest`, `RemotePluginClientE2ETest` (ASan) | 8 (coverage; gcov heap abort, then test process stopped while QtTest stack handling hung) | 0 (coverage and ASan model test; all related ASan tests 0) | complexity 0, file-length 0, fork-sources 0, no-upstream-regression 0 |
