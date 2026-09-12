# The teardown abort, swept

The third instance of one defect class in this suite, and the sweep that closes the
class in `tests/`. Branch `post-alpha/teardown-2` off `post-alpha/integration`
(`f1b287926`).

    QFATAL : RenderJobQueueTest::poolModeStillRunsEveryJobExactlyOnce()
             QThread: Destroyed while thread is still running
    Received signal 6 (SIGABRT)                                       (exit 134)

**Status: reproduced, identified, fixed, swept, and proved 50 runs + 3 suite runs green.**
The render-job-queue abort is a **test-side** defect and is fixed in
`tests/src/core/RenderJobQueueTest.cpp`. The sweep then found a **fourth site of the
same class in product code** - the ALSA-sequencer MIDI client's bounded join, which the
release line's own `ControlShutdown` test hit - and that one is fixed in
`src/core/midi/MidiAlsaSeq.cpp` with the ledger clause appended in the same commit.

## 1. The abort, as measured

The release line's own coverage run is the most expensive instance: one test of 89
failed *and aborted the test phase*, which cost the whole coverage measurement
(`zene-pa-integration/build-coverage/tests/Testing/Temporary/LastTest.log:3084-3093`):

    FAIL!  : RenderJobQueueTest::poolModeStillRunsEveryJobExactlyOnce() Compared values are not the same
       Actual   (total): 63
       Expected (64)   : 64
       Loc: [.../tests/src/core/RenderJobQueueTest.cpp(264)]
    QFATAL : RenderJobQueueTest::poolModeStillRunsEveryJobExactlyOnce() QThread: Destroyed while thread is still running
    FAIL!  : RenderJobQueueTest::poolModeStillRunsEveryJobExactlyOnce() Received a fatal error.
    Totals: 5 passed, 1 failed, 0 skipped, 0 blacklisted, 121ms
    Received signal 6 (SIGABRT)

That log is the whole diagnosis in three lines, and it is worth reading carefully,
because the abort is not the defect - it is the second defect. Line 264 is reached only
if the loop above it (`QCOMPARE(job->runs, 1)`, 64 iterations) passed for **every** job.
So all 64 jobs ran exactly once, the sum of the observations is 64, and the *aggregate*
counter read 63. The job was not lost: an increment on the counter was.

### Rates, measured (same source, different conditions)

| # | Condition | Runs | Aborts | Rate |
|---|-----------|------|--------|------|
| 1 | Release line, coverage configuration (Debug + gcov, all objects) - the parent lane's measurement, log above | 10 | 3 | 30 % |
| 2 | Sibling worktree's build of the identical source (RelWithDebInfo), box compiling | 20 | 1 | 5 % |
| 3 | Same binary under gdb (2 catches in the 31st and 24th attempt) | 55 | 2 | 4 % |
| 4 | This lane's build, RelWithDebInfo, sequential | 50 | 0 | 0 % |
| 5 | Same, four parallel loops (4x25) | 100 | 0 | 0 % |
| 6 | Same, 60 busy loops on a 20-core box | 25 | 0 | 0 % |
| 7 | Case's own object only at `-O0 --coverage` | 40 | 1 | 2.5 % |
| 8 | Case's object **and** `AudioEngineWorkerThread.cpp` at `-O0 --coverage` | 50 | 6 | **12 %** |

Condition 8 is the instrument used for the red/green pair in §3-4: it is the coverage
configuration's compile mode (`tests/run-coverage.sh` builds with
`-DCMAKE_BUILD_TYPE=Debug`) applied to the two objects whose code the race runs through,
which widens the load-add-store window without a second full build
(`tests/integration-logs-teardown2/repro/instrument-o0.py`).

One abort captured end to end on this lane's own tree, exit code unpiped
(`tests/integration-logs-teardown2/repro/pre-fix-failing-run-output.log`):

    CAUGHT on attempt 30, EXIT=134
    FAIL!  : RenderJobQueueTest::poolModeStillRunsEveryJobExactlyOnce() Compared values are not the same
       Actual   (total): 63
       Expected (64)   : 64
       Loc: [.../zene-pa-teardown2/tests/src/core/RenderJobQueueTest.cpp(264)]
    QFATAL : ... QThread: Destroyed while thread is still running

### What the rate depends on

It is a property of the box, not of the code. Conditions 2-3 aborted while sibling lanes
were compiling; condition 4-6 is the same binary at the same `-O2`, after the build
finished, with zero aborts in 205 runs. The lost update needs the two threads that
increment the counter to be preempted between the load and the store of `++total` - the
wider the window (lower `-O`, gcov counters, more runnable threads) the more often it
lands. That is why a 50-run green at `-O2` alone would prove little, and why the proof in
§5 is run in the 12 % condition as well.

## 2. The identity of the thread that outlives its object

`gdb` on a caught abort (`tests/integration-logs-teardown2/repro/gdb-abort2.log`,
`gdb-cmds2.txt`):

    #4  __GI_abort
    #5  qAbort()
    #6  QMessageLogger::fatal(char const*, ...) const
    #7  ??? () at libQt6Core.so.6                       <- QThread::~QThread -> qFatal
    #8  RenderJobQueueTest::poolModeStillRunsEveryJobExactlyOnce (this=<optimized out>)
    #9  RenderJobQueueTest::qt_static_metacall (...) at RenderJobQueueTest.moc:154
    #10 QMetaMethod::invoke
    #13 QTest::qExec

    ==== frame 8 (the test slot) ====
    poolWorker = {<QThread> = {<No data fields>}, static staticMetaObject = {d = {...
        stringdata = ...qt_meta_stringdata_lmms__AudioEngineWorkerThread, ...
        static_metacall = lmms::AudioEngineWorkerThread::qt_static_metacall, ...}},
        m_quit = false}
    total = 63

    ==== threads ====
      1  RenderJobQueueT   __pthread_kill_implementation      (the aborting main thread)
      2  lmms::AudioEngi   __futex_abstimed_wait_common64 ... futex_word=0x555556228064
      3  lmms::AudioEngi   __futex_abstimed_wait_common64 ... futex_word=0x555556228064

* **The object:** `poolWorker`, a `lmms::AudioEngineWorkerThread` declared as a local of
  the test slot itself (`RenderJobQueueTest.cpp:250` in the pre-fix file).
* **Its parent:** **none**. `AudioEngineWorkerThread(nullptr)` is
  `QThread(nullptr)`, so the object has no QObject parent - it is owned by the test
  slot's stack frame, not by the engine.
* **The line that deletes it:** the compiler's destructor for that local, on the return
  path out of the slot. That return is the one `QCOMPARE` expands to when it fails at
  line 264 - which is why the destructor, and the abort, run inside frame 8 and not
  inside `~AudioEngine`.
* **`m_quit = false`**: the worker was never asked to quit. `stopWorker()` (pre-fix line
  266) is *after* the assertion, so the failed comparison skipped it and the thread was
  still parked in the 100 ms wait when its object died.

Threads 2 and 3 are the fixture's pool worker and this case's; both are alive and parked
in the timed futex wait, which is the previous lane's fix working as designed (a bounded
re-check instead of a stranded thread) - and is exactly why this is a *different* site.

Compare the previous instance (`213d58cac`, docs/TEST-HYGIENE.md), where the same
`qFatal` came from

    #8  lmms::AudioEngineWorkerThread::~AudioEngineWorkerThread
    #10 QObjectPrivate::deleteChildren()
    #12 lmms::AudioEngine::~AudioEngine

i.e. from the engine's child list. This one is the test's own object. That difference is
what makes the fix go on the test side.

## 3. The two defects, and the fix

**Defect A - a shared counter with two writers.** `CountingJob` incremented
`int& m_total` from whichever thread ran the job; in pool mode that is the calling thread
*and* the pool worker. Two threads incrementing one `int` lose an update when they
overlap, and the lost update is indistinguishable from a dropped job unless the per-job
observations are exact - which the log above shows they were.

**Defect B - the thread object destroyed before the join.** The case asserted first and
called `stopWorker()` last, so any failed assertion returned from the scope with the
worker still running and Qt aborted the process. An abort is worse than a failure: it
hides the assertion that failed and discards the rest of the binary's results, which is
how one lost increment in one case presented as "a test failed and the coverage
measurement is gone".

**The fix is test-side, both halves, in `tests/src/core/RenderJobQueueTest.cpp`:**

* `CountingJob::m_total` is `std::atomic<int>&`; every assertion reads `total.load()`.
  The 64 per-job `runs` assertions are untouched - the atomic aggregate is a cross-check
  of them, not a replacement. No assertion was weakened, and nothing is skipped.
* `stopWorker()` returns `bool` instead of asserting; every case **observes, joins, then
  asserts**, and a caller whose join failed `release()`s the pointer instead of
  destroying a running QThread (the same deliberate leak as
  `AudioEngineTeardownTest`'s stranded-worker case, with the reason in a comment).
  `initTestCase`/`cleanupTestCase`'s fixture worker is joined the same way.
* A new case, `poolModeAccountingIsExactWhenBothThreadsDrain`, pins defect A at a scale
  where it cannot hide: 512 jobs with no settle, so both threads drain the same queue at
  once. Red/green pair, measured:

      with a plain int counter     red 36 of 40 runs   (e.g. Actual 510, Expected 512)
      with the atomic counter      green 40 of 40 runs

  and the 36 red runs now **fail with a readable assertion and exit 1**, not an abort -
  defect B is what turned them into exit 134.

**Why not product-side.** Nothing in `AudioEngineWorkerThread`/`AudioEngine` is wrong
here: the queue ran every job exactly once, `~AudioEngine`'s join is already unbounded,
and the vanished thread belongs to the test's own fixture, which a test *can* reach. A
product change would have been the wrong site and would have needed a ledger entry for no
reason.

## 4. The sweep

`git grep` over `tests/` for the class's four signatures - a `QThread`/`std::thread`
started, a pool or engine or device started, a thread object destroyed without a join, a
shared counter written by two threads:

```bash
git grep -nE "\.start\(\)|->start\(" -- tests/src
git grep -nE "std::thread|QThread " -- tests/src
git grep -n "\.join\(\)|\.wait\(" -- tests/src
git grep -n "Engine::destroy|Engine::init|AudioEngineWorkerThread|ThreadableJob" -- tests/src
git grep -n "public QThread|: QThread" -- src/ include/     # every product thread, then who drives it
```

Every site found, and its verdict:

| # | Site | Starts | Verdict |
|---|------|--------|---------|
| 1 | `RenderJobQueueTest.cpp` `initTestCase` fixture worker | pool worker (QThread) | **fixed here** - joined via `stopWorker()`, released rather than destroyed if the join fails |
| 2 | `RenderJobQueueTest.cpp` `inlineModeNeverLetsThePoolTakeAJob` | local QThread | **fixed here** - same class as the flake: `expectAllRanOn()` could return early before `stopWorker()`; now joins first |
| 3 | `RenderJobQueueTest.cpp` `poolModeStillRunsEveryJobExactlyOnce` | local QThread + shared `int` | **fixed here** - the measured abort (join-before-assert; counter atomic) |
| 4 | `RenderJobQueueTest.cpp` `inlineModeRunsEveryJobOnceOnTheCallingThread`, `inlineModeDrainsAStaticQueueOnTheCallingThread`, `jobsThatDoNotRequireProcessingAreNotRun` | no thread of their own (inline path) | benign - deterministic inline mode, no worker to join; counter made atomic with the rest |
| 5 | `AudioEngineTeardownTest.cpp:123,160` | 8 workers per case | already fixed elsewhere - `213d58cac`'s own witness: 2000 ms waits, a stranded worker is `release()`d, never destroyed |
| 6 | the 40 test files that call `Engine::destroy()` (AudioBusTest, PdcMixerTest, ScriptEngineTest, MixerAbRegressionTest, the VST3/CLAP integration tests, ...) | the engine's 19-worker pool + the dummy device | already fixed elsewhere - product fix `213d58cac`: `~AudioEngine` joins every worker without a deadline, and the QThreads are children of the engine, so `deleteChildren()` can no longer meet a running thread. Exercised 86x per suite run, twice per release verification, 0 aborts in 3 runs here |
| 7 | product device thread: `AudioDummy::run()` (every `Engine::init(true)`) | `AudioDummy : QThread` | benign, upstream - `AudioDevice::stopProcessingThread()` is `wait(30000)` then `terminate()` + `wait(1000)`; unmodified upstream code, and no abort in any measured run. A device thread that hung for 31 s would still abort; not reachable here and not a test-side fix |
| 8 | `SessionSchedulerRenderTest.cpp:207` | `ProjectRenderer` (product QThread) | benign - `renderer.wait()` before the stack object dies; the `isReady()` early return precedes `startProcessing()` |
| 9 | `MasteringTest.cpp`, `StemExportTestSupport.h` (RenderManager) | `ProjectRenderer` owned by `RenderManager` | benign, checked - the manager resets the renderer from the renderer's own `finished()` signal (after `run()` returned) and the tests drive it through a `QEventLoop`; ~0 aborts over 3 suite runs with several renders each |
| 10 | `RemotePlugin`'s `ProcessWatcher` (RemotePluginClientE2ETest, RemotePluginAudioPortsTest, ZynSeparateProcessTest) | product QThread member | benign - `~RemotePlugin` does `m_watcher.stop(); m_watcher.wait();` unbounded before the member is destroyed |
| 11 | `ScriptEngine`'s worker (`ScriptEngineTest`, `ScriptStabilisationTest`, `ScriptBindingsTest`, `ControlAutomationScriptTest`) | product QThread | benign - `~ScriptEngine`: `quit()`, `wait()` unbounded, `delete` |
| 12 | `StemJobManager`'s worker (`StemJobManagerTest`) | product QThread | benign - dtor sets `m_quit`, `wakeAll()`, `wait()`, `delete`. A missed wake would hang the join; it cannot abort |
| 13 | `MidiLearnThreadTest.cpp:78` | `std::thread` | benign - `join()` at :82, nothing between |
| 14 | `MidiLearnGuiTest.cpp:92` | `std::thread` | benign - `join()` at :96 |
| 15 | `MixerConcurrencyTest.cpp:130` `renderWhileMutating` | `std::thread` | benign - stop flag then `join()` at :138; no early return between |
| 16 | `MixerConcurrencyTest.cpp:153` `escapesTheRenderPeriod` | `std::thread` | benign - `join()` at :174, after `doneChangeInModel()` |
| 17 | `MixerConcurrencyTest.cpp:536` `movingAChannelIsSerialisedWithTheRenderPeriod` | `std::thread` | benign - `join()` at :569 before the assertions; shares only `std::atomic` counters |
| 18 | `MixerConcurrencyTest.cpp:650` `addingAPlayHandleIsSerialisedWithTheIterator` | `std::thread` | benign - `join()` at :667 before the assertions |
| 19 | `RecordRingBufferTest.cpp:201` | `std::thread` | benign - `join()` at :230; the `Q_ASSERT` inside the producer is an invariant, not teardown |
| 20 | `RecordingRealtimeTest.cpp:136` | `std::thread` | benign - `join()` at :154 |
| 21 | `RecordingRealtimeTest.cpp:246` | `std::thread` | benign - `join()` at :264; `holding` is atomic |
| 22 | `SessionSchedulerTest.cpp:495` | `std::thread` | benign - `join()` at :521; `producerDone` is atomic |
| 23 | `TwoTrackAlsaCaptureProbe.cpp:174` | `std::thread` | benign - `join()` at :201 in the same scope; standalone probe, not a ctest test |
| 24 | `TwoTrackRecordingHarness.cpp:308` | `std::thread` | benign - `join()` at :336; shared results read after the join |
| 25 | `PluginPortsHarness.h:456`, `:1023` (`renderInFreshThread`, `renderInstrumentInFreshThread`) | `std::thread` | benign - `join()` on the next line |

The second half of the class - test bookkeeping written by a second thread - was swept
the same way: `RenderJobQueueTest`'s `total` was the only non-atomic shared counter that a
worker writes (fixed here). Everywhere else the shared state is already `std::atomic`
(`MixerConcurrencyTest`'s `created`/`moves`/`reorders`, `RecordingRealtimeTest`'s
`holding`, `SessionSchedulerTest`'s `producerDone`, `MidiLearnThreadTest`'s `Delivery`),
lambda-local (`MixerConcurrencyTest`'s `round`), or written in the thread and read only
after a `join()` (`TwoTrackRecordingHarness`'s `producerAllocations`, `TwoTrackAlsaCaptureProbe`'s
`captured`/`xruns`).

### 4a. The product's own threads, and the fourth site the sweep found

The same grep over `git grep -n "public QThread|: QThread" -- src/ include/` lists every
QThread the tests can reach indirectly, because a test that starts the engine starts them:

| Product thread | Destroyed by | Verdict |
|---|---|---|
| `AudioEngineWorkerThread` (the engine's pool, 19 of them under `Engine::init`) | `~AudioEngine`, joined unbounded (`213d58cac`) | already fixed elsewhere; the test-side sites that start one directly are #1-#4 above |
| `AudioDummy`, `AudioAlsa`, `AudioOss`, `AudioPulseAudio`, `AudioSndio` (the audio device) | `AudioDevice::stopProcessingThread()`: `wait(30000)`, then `terminate()` + `wait(1000)`; every device dtor calls `stopProcessing()` | benign as measured - bounded, upstream, and the thread leaves its loop on the first period after `m_running` clears. A device thread that hung for 31 s would still meet `~QThread`; that is an upstream product decision (terminate a backend thread or block shutdown) and no run here reached it |
| `MidiAlsaSeq` (ALSA-sequencer MIDI client) | `~MidiAlsaSeq`: **`wait(EventPollTimeOut*2)` = 500 ms** | **the fourth site - fixed here**, see below |
| `MidiAlsaRaw` | `~MidiAlsaRaw`: `wait(1000)`, then `terminate()` | same class, **not fixed**: reachable only when the configured MIDI device is ALSA-raw (this box uses the sequencer). Its loop polls with a 10 s timeout, so the honest fix is `terminate(); wait();` - a product decision, and unverifiable here without that backend. Named so the next lane does not have to rediscover it |
| `MidiOss`, `MidiSndio` | `~MidiOss` / `~MidiSndio`: `wait(1000)`, then `terminate()` | same shape, benign as measured: unreachable on this box (no OSS/sndio MIDI), and both already `terminate()` after the bounded wait |
| `MidiJack` | `~MidiJack`: `wait(1000)` | **same defect, named not fixed**: its `run()` sleeps 1 s per iteration, so the join budget equals the loop's worst-case latency; unreachable here (no JACK) |
| `ProjectRenderer` | `abortProcessing()` = `m_abort = true; wait()`, and `RenderManager` resets it only after `finished()` | benign - unbounded join before destruction (sweep #8, #9) |
| `ProcessWatcher` (remote plugins) | `~RemotePlugin`: `m_watcher.stop(); m_watcher.wait();` | benign - unbounded join (sweep #10) |
| `InstrumentLoaderThread` (GUI instrument loader) | GUI-only, short-lived, not instantiated by any test in the suite | not reached by the suite; no test starts it |

**The fourth site, found by the third acceptance suite run.** Suite run 3 - the one under
40 busy loops, i.e. the condition the release verification's box is in - was **not** green
on the first attempt:

    5/86 Test #79: ControlShutdown ... ***Failed 14.24 sec
    [shutdown: no usable audio device] exit=-6 after 1.40s, socket exists=True
    QThread: Destroyed while thread is still running
    The following tests FAILED: 79 - ControlShutdown (Failed)

`ControlShutdown` (`tests/control-shutdown.py`) drives the real GUI binary through two
shutdown reproductions and requires exit 0. Re-run under load with the abort shim
(`repro/repro-control-shutdown-abort.sh`), it aborted on the **first** attempt and the
shim named the thread (`repro/control-shutdown-abort.log`):

    === ABORT TRACE ===
    ...  libQt6Core.so.6(qAbort)
    ...  libQt6Core.so.6(qt_assert ...)                       <- ~QThread -> qFatal
    zene(+0x399181)   non-virtual thunk to lmms::MidiAlsaSeq::~MidiAlsaSeq()   include/MidiAlsaSeq.h:53
    zene(lmms::AudioEngine::~AudioEngine+0xa4)
    zene(lmms::Engine::destroy+0x1ee)
    zene(gui::MainWindow::~MainWindow())   src/gui/MainWindow.cpp:271
    ...  QCoreApplication::exec

so the thread is the ALSA-sequencer MIDI client's, and the object is destroyed by
`~AudioEngine`'s `delete m_midiClient`. The destructor is

    MidiAlsaSeq::~MidiAlsaSeq()
    {
        if( isRunning() )
        {
            m_quit = true;
            wait( EventPollTimeOut*2 );     // EventPollTimeOut = 250 -> a 500 ms budget
            ...close the sequencer handle...
        }
    }

- the *same* bounded-join shape as the previous lane's `~AudioEngine`, in a file that is
already in the upstream ledger. Under load the polling thread does not notice `m_quit`
inside 500 ms, `wait()` returns with the thread alive, and the object's own destructor
chains into `~QThread`.

This is **product-side and the test cannot reach it**: the thread is created inside
`MidiAlsaSeq`'s constructor, owned by `AudioEngine::m_midiClient`, and there is no accessor
a test could use to quit and join it - `ControlShutdown` can only observe the process's
exit code. Fixed in `src/core/midi/MidiAlsaSeq.cpp` as one line plus the reason
(`wait()` unbounded; `run()` leaves its loop within one 250 ms poll interval of `m_quit`,
so the join terminates, and the sequencer handle is now closed only after the thread has
stopped - the old code could close it while the thread was still polling it). The file is
already declared in `tests/upstream-modifications.txt`; a clause naming this defect and
that trade was appended to its entry **in the same commit**, as
`tests/no-upstream-regression-gate.sh` requires.

## 5. The proof

Exit codes are captured unpiped everywhere (`cmd > log 2>&1; echo EXIT=$?`) and every log
is in `tests/integration-logs-teardown2/`. Two batteries: the first after the test-side
fix, the second after the `MidiAlsaSeq` product change, which invalidates every
measurement taken on the earlier binaries.

**5a. After the test-side fix**

| Run | Condition | Result |
|-----|-----------|--------|
| `post-fix-case-50.log` | release configuration, 50 sequential runs of `poolModeStillRunsEveryJobExactlyOnce` | **50 passed, 0 failed, 0 aborts** |
| `post-fix-instrumented-50.log` | the §1 condition 8 instrument, where the pre-fix build aborted **6 of 50** | **50 passed, 0 failed, 0 aborts** |
| `post-fix-witness-50.log` | the new witness case, 50 sequential runs | **50 passed, 0 failed** |
| `post-fix-allcases-40.log` | the whole `RenderJobQueueTest` binary, 40 sequential runs | **40 passed, 0 failed** |
| `suite-run-1-alone.log` | `ctest --test-dir build/tests -j2`, alone | **EXIT=0, 86/86 passed, 0 aborts**, 62 s |
| `suite-run-2-interference-loops.log` | same, while four parallel loops of the case ran | **EXIT=0, 86/86, 0 aborts** |
| `suite-run-3-interference-hogs.log` | same, on a deliberately oversubscribed box (40 busy loops on 20 cores) | **EXIT=8 - 1 test failed, 1 abort: `ControlShutdown`, the fourth site (§4a)** |

**5b. After fixing the fourth site** (the whole tree re-verified, since a product change
invalidates 5a)

| Run | Condition | Result |
|-----|-----------|--------|
| `post-fix2-case-50.log` | release configuration, 50 sequential runs of the failing case | **50 passed, 0 failed, 0 aborts** |
| `post-fix2-instrumented-50.log` | the same instrument as §1 #8, where the pre-fix build aborted **6 of 50** | **50 passed, 0 failed, 0 aborts** |
| `post-fix2-allcases-40.log` | the whole `RenderJobQueueTest` binary, 40 sequential runs | **40 passed, 0 failed** |
| `post-fix2-control-shutdown-6.log` | `ControlShutdown`, 6 runs under 40 busy loops (it aborted on the first attempt before the fix) | **6 passed, 0 failed, exit 0 each** |
| `suite2-run-1-alone.log` | `ctest -j2`, alone | **EXIT=0, 86/86, 0 aborts**, 64 s |
| `suite2-run-2-interference-loops.log` | while four parallel loops of the case ran | **EXIT=0, 86/86, 0 aborts**, 62 s |
| `suite2-run-3-interference-hogs.log` | 40 busy loops on 20 cores - the run that was red in 5a | **EXIT=0, 86/86, 0 aborts**, 138 s |

`aborts` is a grep for `QFATAL`, `QThread: Destroyed` and `Received signal 6` over each
log: 0 in every green run and in all six `ControlShutdown` runs. The suite count is **86**
in the release configuration, the number this line documents; adding a case to an existing
binary does not change it, and 0 tests would have been treated as an error, not a pass.

A 12 % pre-fix rate gives a 1-in-600 chance that a 50-run green is luck, which is what
makes the instrumented column evidence rather than a coin toss - the release-configuration
column alone (0 aborts at `-O2` even before the fix, on a quiet box) would not.

## 6. The render did not move

    bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz -o ...
    sha256 943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526   (run 1 and run 2)
    data chunk sha256   b37cefc5a97e2d4664bbb0087a187935cdb9421030492f3b21972c3a7e3e59ca   (2,177,024 bytes)
    file 2,177,120 bytes, RENDER_EXIT=0 both runs

Both runs bit-identical and equal to the documented constant, and identical again after
the `MidiAlsaSeq` change - the final numbers are in `render-postfix-hashes.txt`
(`render-postfix-1.log`, `render-postfix-2.log`). The test change cannot reach the render;
the product change is one line in a MIDI client's destructor, and this is the check that
says so rather than argues so.

## 7. What I could not do, and the honest limits

* **The 30 % rate at `-O2` did not reproduce here today.** It did reproduce while sibling
  lanes were compiling (2 aborts in 20 runs, plus 2 more under gdb), and it is on record
  at 3 in 10 in the release line's coverage log; at `-O2` on a quiet box this lane
  measured 0 aborts in 205 runs. The pre/post proof therefore leans on the instrumented
  condition (§1 #8), where the same defect is measurable at 12 %, and the release
  configuration is the control column. Anyone repeating this should not expect the
  release configuration to be red on demand.
* **The instrument is narrower than the coverage configuration**: two objects at
  `-O0 --coverage` rather than every object, hence 12 % rather than the coverage build's
  30 %. It is applied to the same code path (the test's counter and the worker's loop) and
  the direction of the bias is known.
* **Four sibling MIDI clients are the same defect and are left as found, deliberately,
  each named with the fix it needs** (§4a): `MidiAlsaRaw` (`wait(1000)` then `terminate()`;
  reachable only with an ALSA-raw MIDI device, and its loop polls with a 10 s timeout, so
  the honest fix is `terminate(); wait();`), `MidiJack` (a 1 s join budget against a loop
  that sleeps 1 s per iteration), `MidiOss` and `MidiSndio` (`wait(1000)` + `terminate()`,
  unreachable on this box). None could be *verified* here - no JACK, no OSS/sndio MIDI
  device, and the ALSA-raw backend is not what this box or its tests configure - and
  blind product surgery on a backend a lane cannot exercise is worse than a precise
  verdict. `MidiAlsaSeq` was fixed because it was proved, by a red test, on this box.
* **The audio device's own join is still bounded** (`wait(30000)` then `terminate()` +
  `wait(1000)`, sweep #7). A device backend that hung for 31 s would still meet `~QThread`.
  Whether to terminate a device thread or block the shutdown is a product decision, not a
  test-side one, and no run here reached it - recorded rather than changed.
* **Two sites of the class stay as found in tests/**, both checked and benign (§4 #7/#9
  in the product table): `RenderManager` destroying its renderer from the renderer's own
  `finished()` signal, and `AudioDevice::~AudioDevice`'s `assert(!isRunning())` (compiled
  out under `-DNDEBUG`, and unreachable while the device stop path works).
* **No manifest had to be re-derived.** The one upstream file touched
  (`src/core/midi/MidiAlsaSeq.cpp`) was already declared in
  `tests/upstream-modifications.txt`; the clause was appended to its entry, and
  `tests/no-upstream-regression-gate.sh`, `fork-sources-gate.sh`, `file-length-gate.sh
  --check`, `unregistered-tests-gate.sh`, `no-tautology-gate.sh`, `duplication-gate.sh`
  and `release-version-gate.sh` all pass on the final tree
  (`gate-*.log` in the log directory).
* **The `-O0 --coverage` instrument is not the coverage build.** It recompiles two objects
  the way that configuration does; the release line's 3-in-10 is a full Debug + gcov tree.
  The direction of the bias is known (12 % < 30 %) and the pre/post pair is measured with
  the same instrument on both sides.

## 8. Reproducing any of this

```bash
cd .../zene-pa-teardown2
JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 4     # configure + build + ctest
ctest --test-dir build/tests --output-on-failure              # 86 tests

# the case, 50 times, exit codes unpiped
for i in $(seq 1 50); do ./build/tests/RenderJobQueueTest poolModeStillRunsEveryJobExactlyOnce \
    >/dev/null 2>&1; echo "run$i EXIT=$?"; done

# the pre-fix rate, on the pre-fix tree: widen the window with the coverage
# configuration's compile mode on the two objects the race runs through
python3 tests/integration-logs-teardown2/repro/instrument-o0.py build RenderJobQueueTest.cpp "-O0 --coverage"
python3 tests/integration-logs-teardown2/repro/instrument-o0.py build AudioEngineWorkerThread.cpp "-O0 --coverage"
(cd build/tests && eval "$(cat CMakeFiles/RenderJobQueueTest.dir/link.txt) --coverage")   # relink WITH gcov
bash tests/integration-logs-teardown2/repro/loop-case.sh "$PWD/build/tests/RenderJobQueueTest" \
    poolModeStillRunsEveryJobExactlyOnce 2 25 /tmp/rate.log      # measured 12 % before the fix

# the identity of the vanished thread, without a core file
gcc -shared -fPIC -O1 -g -o /tmp/abort-trace.so \
    a throwaway LD_PRELOAD abort shim (removed from the tree after CI's check-namespace -- a C file cannot declare a C++ namespace and the checker has no allowance for tools/*.c) -ldl    # or: gdb -x repro/gdb-cmds2.txt
```

Restore the release configuration afterwards by recompiling those two objects normally
(`touch` the sources and rebuild the target); the acceptance runs in §5 were all made with
a binary that carries zero `__gcov_` symbols after that restore.
