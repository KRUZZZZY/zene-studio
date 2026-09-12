# Test hygiene: the teardown abort, the class sweep, and the proofs

Lane: `post-alpha/test-hygiene` (worktree `zene-pa-testhygiene`), branch cut from
`post-alpha/integration` at `749d927b9`.

**Verdict in one paragraph.** The `QThread: Destroyed while thread is still running` abort is
not a defect in `AudioPortsTest` or `PdcMixerTest` and not a missing "stop the dummy device";
it is one `AudioEngineWorkerThread` that misses the single wake-up issued during engine
teardown, and a `wait(500)` in `~AudioEngine` that then gives up on it while the QThread object
is a child of the engine — so `~QObject` deletes a thread that is still running, and Qt answers
that with `qFatal`/SIGABRT. Fixed in two halves (a bounded re-check in the worker's wait, an
unbounded join in `~AudioEngine`); 31 of 184 engine-test runs aborted at load average ~27
before, 0 of 30 full-suite runs under load after. A sweep of the same class (engine/dummy-device
tests, harness threads, fixed `/tmp` paths, fixed ports) is in §6, the three tests that existed
but could never run in §7, and the limits — including one half of the fix whose necessity my own
measurement did **not** establish — in §9.

Everything below is a command that ran on this tree. Exit codes are unpiped
(`cmd > log 2>&1; echo EXIT=$?`), logs are inside the repository (`logs/`), never `/tmp`.

---

## 1. Reproduction

Run the suite the way the release verification will meet it: several suite invocations back to
back *and* in parallel with each other, which is the same interference in miniature.

The driver is `logs/repro/reproduce-abort.sh` (rounds × parallel pairs of `AudioPortsTest` and
`PdcMixerTest`); the full-suite driver is `tests/scripted/test-hygiene-under-load.sh`.

| run | command | runs | aborts | non-zero exits |
|---|---|---|---|---|
| R1 | `bash logs/repro/reproduce-abort.sh 8 8` | 128 (64 AudioPorts + 64 PdcMixer) | **13** (all `PdcMixerTest`) | 13 × `EXIT=134` |
| R2 | `bash logs/repro/reproduce-abort.sh 12 10` | 240 (120 + 120) | **18** (all `PdcMixerTest`) | 18 × `EXIT=134` |

Pre-fix totals: **31 of 184 `PdcMixerTest` runs aborted (16.8 %)** at load average 26–28 with
16 sibling test processes running. `AudioPortsTest` aborted **0 of 184** — see §9, item 1.

Exact abort output (`logs/repro/pdc-r8-1.log`):

```
PASS   : PdcMixerTest::unmuteDoesNotReplayFrozenDelayHistory()
QFATAL : PdcMixerTest::cleanupTestCase() QThread: Destroyed while thread is still running
FAIL!  : PdcMixerTest::cleanupTestCase() Received a fatal error.
Totals: 10 passed, 1 failed, 0 skipped, 0 blacklisted, 3355ms
********* Finished testing of PdcMixerTest *********
Received signal 6 (SIGABRT)
         cleanupTestCase function time: 501ms, total time: 3355ms
```

Three things in that paste matter:

* the failure is attributed to `cleanupTestCase()`, i.e. teardown — every assertion in the test
  body had already passed (`10 passed`);
* `cleanupTestCase function time: 501ms` — one `wait(500)` budget expiring, to the millisecond;
* the process dies by signal (exit 134), so nothing downstream can interpret it and the test
  that "fails" is whichever one was running when the budget expired.

### 1.1 Which thread the QThread was

`logs/repro/gdb-catch-abort.sh` runs `PdcMixerTest` under gdb until it aborts (caught on
attempt 18 of 40) and dumps the aborting backtrace. `logs/repro/gdb-run-18.log`:

```
#6  QMessageLogger::fatal(char const*, ...) const
#7  ?? () from libQt6Core.so.6                       <- QThread destructor -> qFatal
#8  lmms::AudioEngineWorkerThread::~AudioEngineWorkerThread (this=0x5555561070c0)
        at .../src/core/AudioEngineWorkerThread.cpp:132
#10 QObjectPrivate::deleteChildren()
#11 QObject::~QObject()
#12 lmms::AudioEngine::~AudioEngine (this=0x555556133300) at .../src/core/AudioEngine.cpp:144
#13 lmms::Engine::deleteHelper<lmms::AudioEngine> (ptr=lmms::Engine::s_audioEngine)
```

and `info threads` from the same run shows exactly **one** other live thread:

```
  Id   Target Id                       Frame
  18   Thread ... "lmms::AudioEngi"     __futex_abstimed_wait_common64 (... futex_word=0x555556107be4)
```

One worker, parked on a wait condition, out of `QThread::idealThreadCount() - 1` = 19 on this box.

### 1.2 The mechanism

`src/core/AudioEngineWorkerThread.cpp` `run()` (upstream, unchanged by this fork):

```cpp
QMutex m;
while( m_quit == false )          // (a) flag read OUTSIDE the mutex
{
    m.lock();
    queueReadyWaitCond->wait( &m );   // (b) block with no deadline
    globalJobQueue.run();
    m.unlock();
}
```

`quit()` sets `m_quit` and `startAndWaitForJobs()` wakes the condition — two separate steps, and
neither is atomic with respect to a worker arriving at (b). A worker preempted between (a) and
(b) misses the only wake-up that was meant to release it and then sleeps for good. `~AudioEngine`
then joins each worker with a fixed `wait(500)`; when that expires the worker is still running,
and because the QThreads are QObject children of the engine (`QThread( audioEngine )`), `~QObject`
deletes them and Qt aborts the process.

The 19 workers are why this is load-sensitive and why it looks random: the strand needs one
worker to lose one race, and under load the window between (a) and (b) is wide.

**The brief's sibling hypothesis is refuted by the tree.** "Stop the dummy device before
`Engine::destroy`" cannot be the fix: `PdcMixerTest` *already* stops the device before
`Engine::destroy` — `initEngine()` in `tests/src/core/PhaseDMixerTestSupport.h:64-68` calls
`Engine::init(true)` then `Engine::audioEngine()->audioDev()->stopProcessing()` — and it aborted
31 times out of 184. The aborting object is an `AudioEngineWorkerThread`, not the `AudioDummy`
device (§1.1), and the device is a separate thread with a 30 s join budget plus a `terminate()`
fallback (`AudioDevice::stopProcessingThread`), while the worker pool had 500 ms and no fallback.

---

## 2. The fix

Two changes, both to upstream-inherited files, both declared in
`tests/upstream-modifications.txt` in the same commit.

### 2.1 Half 1 — the missed wake-up is bounded

`src/core/AudioEngineWorkerThread.cpp:161` (the constant) and `:186` (the wait): the blocking wait
gets a 100 ms deadline (`kQuitRecheckMs`), so a lost wake-up delays the worker's exit by at most that instead of
stranding it. While the engine renders, wake-ups arrive every period and the timer never fires;
while it is idle each worker wakes, drains an empty queue and sleeps again 10×/s.

```cpp
queueReadyWaitCond->wait( &m, kQuitRecheckMs );
```

### 2.2 Half 2 — the join has no deadline

`src/core/AudioEngine.cpp:148`: `m_workers[w]->wait()` instead of `wait( 500 )` (the quit loop it
terminates is at `:126`). A QThread
object must never be destroyed while its thread may still run: Qt makes that an unconditional
`qFatal`, so any missed 500 ms budget aborts the process, whichever test is running.

### 2.3 Why not test-side

It cannot be. The stranded thread is the engine's own worker pool, created inside
`Engine::init(true)`; a test has no handle on `AudioEngine::m_workers` (private) and no way to
wake a worker that has missed the wake. The only test-side option was to avoid `Engine::init`
altogether, which would delete the coverage rather than fix the teardown.

The change is not "a product change to satisfy a test": it fixes the product's own shutdown,
which the released alpha records as a known defect ("`QThread: Destroyed while thread is still
running` abort on exit after a render", `docs/KNOWN-LIMITATIONS.md` in the released tree). The
suite is simply the instrument that made it reproducible, because a test process can be run
under load a hundred times an hour and a user's exit cannot.

---

## 3. Red / green

### 3.1 Half 1, mutation-tested deterministically

`tests/src/core/AudioEngineTeardownTest.cpp` (new, registered) pins the contract: *a quit ends
the loop of an already-parked worker, without anyone else waking it.*

With only the bounded wait reverted (`wait( &m )`, half 2 still in place):

```
$ (cmake --build build --target AudioEngineTeardownTest -j4)   # EXIT=0
$ build/tests/AudioEngineTeardownTest
FAIL!  : AudioEngineTeardownTest::quitWithoutAWakeupStopsAParkedWorker() Compared values are not the same
   Actual   (stranded): 8
   Expected (0)       : 0
Totals: 3 passed, 1 failed ...        EXIT=1
```

3 of 3 runs identical (`logs/teardown-mutB-{1,2,3}.log`) — all 8 workers left parked, which is
the strand in §1.2 reproduced on demand. With the bounded wait restored: 4 passed, 0 failed,
`EXIT=0` (`logs/teardown-green.log`, `logs/teardown-final.log`). The case leaks the stranded
QThreads deliberately: destroying a running QThread is the thing under test, and an abort would
hide the assertion that already failed.

### 3.2 Half 2 — necessity NOT established by measurement

Reverting only half 2 (`wait( 500 )`, bounded wait kept) and re-running the load reproduction:

```
$ bash logs/repro/reproduce-abort.sh 8 8      # 64 AudioPortsTest + 64 PdcMixerTest
  total runs 128, non-zero exits 0, aborts 0     (logs/repro/mutA-underload.txt)
$ find logs/repro -name 'pdc-r*.log' -newermt '<mutation-A build time>' -exec grep -l QFATAL {} \; | wc -l
0
```

So at this load the bounded re-check alone already removes the abort, and I did **not** prove
that half 2 is independently necessary. It is kept deliberately, as the structural guarantee the
task asks for ("a test cannot abort because a thread outlives its object"): with `wait(500)`,
*any* reason a worker is not finished when the budget expires — a lost wake-up, starvation, a slow
job — still ends in an unconditional `qFatal`, and this is the change that removes that class
rather than one cause of it. The trade it makes is stated in the comment: a job that never
returns now blocks shutdown instead of aborting the process.

*(Earlier in this lane I read a mutation-A run as 12 aborts; that was stale `pdc-r*-*.log` files
from the previous round sharing the log directory. Corrected here — the count in
`logs/repro/mutA-underload.txt` is 0.)*

### 3.3 Control

The pre-fix control is §1: 31/184 aborts of the same test, same driver, same box, load average
26–28. After the fix: 0/30 full-suite runs (§4) and 0/64 in the mutation-A run above.

---

## 4. Acceptance under load

`tests/scripted/test-hygiene-under-load.sh <rounds> <parallel> <ctest -j>` runs *parallel*
`ctest` invocations, each with its own `-j`, back to back, from `build/tests`, and reports every
run's unpiped exit code, its passed/failed/out-of counts and whether an abort signature appears
in the log.

| phase | command | suite runs | tests per run | aborts | failed runs |
|---|---|---|---|---|---|
| intermediate (42 tests) | `bash tests/scripted/test-hygiene-under-load.sh 10 3 2` | 30 | 42 | **0** | 2 (`TwoTrackRecordingHarness`, §5.1) |
| frozen tree (46 tests) | `bash tests/scripted/test-hygiene-under-load.sh 10 3 2` | 30 | 46 | **0** | **0** |

The frozen run's summary line (`logs/underload-frozen.txt`):

```
runs=30  runs_with_0_failed=30  runs_with_an_abort_signature=0  runs_with_nonzero_exit=0
verdict: PASS - no abort, no failed run
SCRIPT_EXIT=0
```

i.e. 30 sequential `ctest -j2` invocations, three at a time, each reporting `exit 0` and
`46/46 passed` — 1 380 test executions with no abort and no failure.

The intermediate run's 2 failures are the *other* defect class in the brief, caught by this
instrument: `TwoTrackRecordingHarness` writing to a fixed `/tmp` path (§5.1). At that point the
run had `EXIT=8` (ctest: some tests failed) and no abort — i.e. the teardown fix held while the
fixed-path defect remained, which is exactly the separation a trustworthy suite needs.

### 4.1 Verification commands for the record

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4 > logs/local-ci-build.log 2>&1; echo EXIT=$?
build EXIT=0
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 41
EXIT=0
```

```
$ cd build/tests && (time ctest -j2) > ../../logs/ctest-postfix-single.log 2>&1; echo EXIT=$?
real    0m27.190s
100% tests passed, 0 tests failed out of 42
EXIT=0
```

Gate 1 of `tests/run-all-gates.sh` (frozen tree): `100% tests passed, 0 tests failed out of 46`,
total test time 55.69 s, gate result `PASS` (`logs/gate-all.log:97`).

---

## 5. The other defects this instrument found

### 5.1 Fixed `/tmp` path — `TwoTrackRecordingHarness`

`TwoTrackRecordingHarness.cpp` defaulted its output directory to `/tmp/lmms-recording-harness`,
and the ctest entry point passes no argument, so two concurrent runs wrote the same two WAVs and
read each other's file back:

```
  output dir: /tmp/lmms-recording-harness
  decoded digest  : 0xfe5bbe695b7ff2ba
  expected digest : 0xc1fb0990bb990b25
  max |error|     : 0.703 (half 24-bit LSB = 5.96e-08)
***Failed  Test #37: TwoTrackRecordingHarness   2.03 sec
```

Per the brief this was already fixed on another branch (`dd121d606`, per-run output directory,
not an ancestor of `post-alpha/integration`). I **ported the existing commit** rather than
re-implementing it (`git cherry-pick -x dd121d606`, authorship preserved, commit message
unchanged); `2c11fe4c0` on this branch. Evidence it works here:

```
$ build/tests/TwoTrackRecordingHarness
  output dir: /tmp/lmms-recording-harness-3c6d733e11e3-291ed78c
EXIT=0
```

### 5.2 The same class, fixed here — `ScriptEngineTest`

`tests/src/core/ScriptEngineTest.cpp:139` (pre-fix line number) wrote a fixed
`QDir::tempPath()/lmms-version-test.lua`,
ran *that* file, and removed it. Any other process on the box — including a sibling worktree's
ctest — overwrites it between the write and the run, and the test then fails on someone else's
source. Now a per-call `QTemporaryDir`; the expectation and the coverage are unchanged.

### 5.3 The same class in the tooling

`tests/run-all-gates.sh` wrote gate 1's build log to `/tmp/gate1-build.log`: two concurrent gate
runs clobber one file, and a disk reclaim destroys the evidence (this program has already lost
one verification's `/tmp` evidence that way). It now writes `build/gate1-build.log`.

---

## 6. The sweep

Searches actually run (`git grep` over the worktree, `tests/reference/**` excluded as a frozen
copy of upstream sources):

```
grep -rn 'Engine::init|initEngine\(\)|Engine::destroy|destroyEngine\(\)|stopProcessing|audioDev' tests/src
grep -rn 'QThread|\.start\(\)|QThreadPool|QtConcurrent|std::thread|\.join\(\)|\.detach\(\)' tests/src
grep -rn '"/tmp|/tmp/|tempPath|QTemporaryDir|mkdtemp|mkstemp' tests/
grep -rniE 'port[^a-z]*[0-9]{2,5}|QTcpServer|QUdpSocket|bind\(|listen\(' tests/src tests/scripted tests/data
grep -rn 'QLocalServer|QLocalSocket|listen\(' src include
```

### 6.1 Engine / dummy device / teardown

35 test sources construct the engine. 14 stop the dummy device explicitly (13 pre-existing:
`AudioBusHandleTest`, `AudioPluginTest`, `ClapEffectIntegrationTest`, `MixerAbRegressionTest`,
`PluginPortsMigration{Test,Reference}`, `Vst3EffectIntegrationTest`, `ZynSeparateProcessTest`,
and the five that use `initEngine()` — `MixerRoutingBackwardCompatTest`, `PdcMixerTest`,
`PhaseDPerfBench`, `PhaseDSidechainTest`, `PhaseFChannelScaleTest`; plus the new
`AudioEngineTeardownTest`). The remaining 21 leave the device running until `Engine::destroy()`:
`AudioBusTest`, `AudioPortsModelTest`, `AudioPortsTest`, `AutomatableModelTest`,
`ClipSerialisationTest`, `MidiLearnTest`, `MidiLearnThreadTest`, `PluginAudioPortsTest`,
`RemotePluginAudioPortsTest`, `RemotePluginClientE2ETest`, `ScriptBindingsTest`,
`ScriptEngineTest`, `ScriptStabilisationTest`, `SessionModelTest`, `StemExportTest`,
`StemSplitPipelineTest`, `TimelineTest`, `MidiLearnGuiTest`, `AutomationTrackTest`,
`SampleClipWindowTest`, `WasmSandboxTest`.

| pattern | verdict | why |
|---|---|---|
| All 35 engine tests: the teardown abort | **fixed centrally, no per-test change** | The abort lives in `~AudioEngine`'s join, not in any test. `PdcMixerTest` already stopped the device and still aborted 31/184 (measured). 0/30 suite runs abort after the fix, with 21 of those tests still leaving the device running. |
| `Engine::init(true)` immediately followed by `audioDev()->stopProcessing()` | **benign-with-a-reason** | This is a *quiescence* idiom for tests that drive the mixer synchronously (stop the device so the next render is not raced), not a teardown fix. Neither necessary nor sufficient for the abort. |
| `AudioDevice::stopProcessingThread` — 30 s budget, `terminate()`, `wait(1000)`, then return | **benign-with-a-reason** | The only place upstream joins a device thread; unlike the worker pool it does not let a running QThread be deleted *if* `terminate()` works, and it warns on stderr when it does not (`"Thread not terminated yet"`). Not observed failing in any run here. Untouched. |
| `ProjectRenderer` / `RenderManager` (a QThread) | **benign-with-a-reason** | Owned and joined inside `RenderManager`; `StemExportTest`/`TwoTrackRecordingHarness` drive it to completion and it produced no abort in 184+ pre-fix runs. |
| `ProcessWatcher` (QThread, `RemotePlugin.h`) | **benign-with-a-reason** | Parented to the remote plugin and joined by its destructor; `RemotePluginAudioPortsTest`/`RemotePluginClientE2ETest` aborted in none of the runs. |
| `ScriptEngine`'s `lmms-lua-script-worker` QThread | **benign-with-a-reason** | `~ScriptEngine` calls `quit()` **and** `wait()` (no deadline) and only then `delete` (`src/core/ScriptEngine.cpp:281-286`). That is exactly the ordering half 2 imposes on the worker pool. |
| `StemJobManager`'s QThread | **benign-with-a-reason** | Same shape: `wait()` then `delete` (`src/core/StemJobManager.cpp:60-62`). Not built in this configuration (`WANT_STEM_SPLIT=OFF`). |

### 6.2 Harness threads

Every `std::thread` in the test tree is joined on the same path that created it; no `detach()`
anywhere:

| file | threads | joined | verdict |
|---|---|---|---|
| `tests/src/plugins/PluginPortsHarness.h:456,1023` | 2 | 2 | benign — both joined before return |
| `tests/src/gui/MidiLearnGuiTest.cpp:92` | 1 | 1 | benign — joined |
| `tests/src/core/MidiLearnThreadTest.cpp:78` | 1 | 1 | benign — joined |
| `tests/src/core/RecordRingBufferTest.cpp:201` | 1 | 1 | benign — joined |
| `tests/src/core/TwoTrackRecordingHarness.cpp:259` | 1 | 1 | benign — joined |
| `tests/src/core/TwoTrackAlsaCaptureProbe.cpp:174` | 1 | 1 | benign — joined (not a ctest entry, §7) |
| `tests/src/core/StemJobManagerTest.cpp` | 0 | — | uses the product's `StemJobManager` thread |

### 6.3 Fixed paths under `/tmp`

| file:line | path | verdict | why |
|---|---|---|---|
| `TwoTrackRecordingHarness.cpp:223` | `/tmp/lmms-recording-harness` | **fixed here (ported `dd121d606`)** | *Measured*: 2 of 30 suite runs read another run's WAV (digest mismatch, `max |error| 0.703`), §5.1. |
| `ScriptEngineTest.cpp:139` (was) | `tempPath()/lmms-version-test.lua` | **fixed here** (now `:144-146`) | Fixed name in the shared temp dir, written then executed then removed; a concurrent process changes what is executed. §5.2. |
| `run-all-gates.sh:87` (tooling) | `/tmp/gate1-build.log` | **fixed here** | Two concurrent gate runs clobber one log; `/tmp` evidence has already been lost to a reclaim. §5.3. |
| `StemModelStoreTest.cpp:62-64` | `/tmp/lmms-stem-model-dir-test` | benign-with-a-reason | Sets `LMMS_STEM_MODEL_DIR` before use, then every case uses a `QTemporaryDir`; `WANT_STEM_SPLIT=OFF`, so it is not built here. |
| `ClapEffectIntegrationTest.cpp:341-342` | `/tmp/clap_{before,after}.wav` | benign-with-a-reason | Write-only evidence artefacts: nothing is read back into an assertion (the levels are computed in memory). A concurrent run overwrites the artefact, not the verdict. Same shape in the unregistered `Vst3EffectIntegrationTest.cpp:283-284`. |
| `CrashReporterTest.cpp:226` | `"/tmp/crash reporter test.mmp"` | benign-with-a-reason | A path *string* handed to `setProjectPath()` and recorded in the report; no file is created or read. |
| `LoudnessReportTest.cpp:217` | `"/tmp/silent.wav"` | benign-with-a-reason | Display argument to `reportText()`; no filesystem access. |
| `ProjectRecoveryTest.cpp:96-212` | `/tmp/rec.mmp` etc. | benign-with-a-reason | Pure string comparisons of recovery decisions; no I/O. |
| `TwoTrackAlsaCaptureProbe.cpp:80` | `/tmp/lmms-recording-alsa` | benign-with-a-reason | Default of a standalone probe's `argv[3]`; not a ctest entry (§7). |
| `tests/data/loudness/render-evidence.sh:20` | `/tmp/lufs-evidence` | benign-with-a-reason | Scripted evidence tool, caller-supplied argument with a documented default; not in ctest. |
| `tests/src/core/MixerAbRegressionTest.cpp:35` | `/tmp/ab-before.raw` | benign-with-a-reason | A comment documenting the env var `LMMS_AB_RENDER_OUT`; the code reads the variable. |

### 6.4 Fixed ports

None. `git grep` for `QTcpServer`/`QUdpSocket`/`listen(`/`bind(`/hard-coded port numbers finds no
fixed port in any test source or script. The product's only socket listener is the remote-plugin
server, which binds an **abstract** Unix socket name (no port), and `RemotePluginClientE2ETest`
uses it. Verdict: **class empty on this tree** — reported so the next reader does not re-run the
search.

---

## 7. Tests that existed and could not run

`tests/CMakeLists.txt` on disk vs. what it registers, both sides enumerated
(`tests/unregistered-tests-gate.sh --verbose`): 60 tracked test sources under `tests/src/`,
55 registered, 5 not.

Three of them were dead by accident and I recovered all three; none needed a product change:

| source | was | now | what it needed |
|---|---|---|---|
| `tests/src/core/PhaseDSidechainTest.cpp` | **could not compile**: `PART_D_COMPRESSOR_LIBRARY` (line 103) was defined by nothing, so it was hand-excluded and looked like Phase D coverage | registered, **7 passed, 0 failed, EXIT=0** (`logs/recovered-PhaseDSidechainTest.log`) | `PART_D_COMPRESSOR_LIBRARY="$<TARGET_FILE:compressor>"` + `add_dependencies(... compressor)`; then `ENABLE_EXPORTS ON`, because the dlopen'ed module failed with `undefined symbol: _ZTIN4lmms16AutomatableModelE` |
| `tests/src/core/PhaseFChannelScaleTest.cpp` | never registered | registered, **10 passed, 0 failed, EXIT=0** | the one-line registration |
| `tests/src/core/MixerRoutingBackwardCompatTest.cpp` | never registered | registered, **5 passed, 0 failed, EXIT=0** | the one-line registration |
| `tests/src/core/MixerAbRegressionTest.cpp` | never registered | registered, **3 passed, 0 failed, EXIT=0** | the one-line registration |

Suite test count on this branch: **41 → 42** (the new teardown test) **→ 46** (the four
recovered). A different count on another branch is therefore expected, not a discrepancy: the
optional tests are configuration-gated, and the recovered four were simply absent.

The two remaining unregistered sources are left unbuilt **deliberately and in writing** (the
`DECLARED` table in `tests/unregistered-tests-gate.sh`), not deleted:

* `tests/src/core/PhaseDPerfBench.cpp` — a CPU-cost benchmark; in a suite that runs while sibling
  builds compile on the same box it would measure the machine's noise, which is why its own
  header brackets every window with twin windows. Run by hand.
* `tests/src/core/TwoTrackAlsaCaptureProbe.cpp` — a standalone probe with its own `main()` that
  needs a real ALSA capture device; it is not a QTest class.

And three are recorded as **open items, not as deliberate exclusions** — the VST3 host tests
(`tests/src/plugins/Vst3HostTest.cpp`, `Vst3BusMapTest.cpp`, `Vst3EffectIntegrationTest.cpp`)
have no counterpart of the `if(WANT_CLAP)` block that registers their CLAP twins, so they compile
nowhere. Wiring them needs VST3 SDK/fixture plumbing; that is a follow-up, and the gate will keep
naming them until it is done.

### 7.1 Gate 10 — the check that stops the fourth

`tests/unregistered-tests-gate.sh` (new; wired into `tests/run-all-gates.sh` as **Gate 10**).
Gate 9 answers "is this file in a *scope manifest*"; it cannot answer "is this test ever built",
which is how three dead test sources coexisted with an all-green gate suite. A test source that
is in no CMake target and in neither of the gate's two declared tables is a violation.

```
$ bash tests/unregistered-tests-gate.sh; echo EXIT=$?
... PASS: every test source under tests/src/ is registered, or declared with a reason
EXIT=0

# negative control: add an unregistered test source
$ printf 'int main(){return 0;}\n' > tests/src/core/NegativeControlTest.cpp
$ bash tests/unregistered-tests-gate.sh; echo EXIT=$?
... FAIL: 1 test source(s) are in no CMake target and declared nowhere.
EXIT=1
```

`tests/*.sh` is not scanned by Gate 9 (it scans C/C++ extensions under `tests/`, plus
`.py`/`.sh` under `tools/` only), so the three new scripts need no manifest entry; the new
`tests/src/core/AudioEngineTeardownTest.cpp` is registered in `tests/all-sources.txt` (the
manifest every other test file is in — `tests/fork-sources.txt` is scoped to `src include
plugins`, which is why no test file appears in it).

---

## 8. Gates

Expected honest results on this branch: `fork-sources-gate.sh` → 0 ·
`no-upstream-regression-gate.sh` → 0 · `run-all-gates.sh` → 3 (Gate 2 skipped without
`--with-coverage`; Gate 5 mutation runs).

```
$ bash tests/fork-sources-gate.sh > logs/gate-fork-sources.log 2>&1; echo EXIT=$?
EXIT=0
$ bash tests/no-upstream-regression-gate.sh > logs/gate-no-upstream.log 2>&1; echo EXIT=$?
EXIT=0
$ bash tests/run-all-gates.sh > logs/gate-all.log 2>&1; echo EXIT=$?
EXIT=3
```

| gate | exit | note |
|---|---|---|
| `tests/fork-sources-gate.sh` | **0** | `PASS: every tracked source in scope is registered (135 fork-NEW, 1007 inherited, 11 tooling)` |
| `tests/no-upstream-regression-gate.sh` | **0** | `PASS: every change to upstream-inherited code since 01148947e… is declared (101 file(s) in the ledger)` — includes the two files this lane touched |
| `tests/run-all-gates.sh` | **3** | gates 1, 3, 4, 5, 6, 7, 8, 9, 10 all **PASS**; gate 2 **SKIP** (`--with-coverage` not passed). `PASS-WITH-SKIPS` is not a pass — exit 3 is the honest result on this branch, and matches the branch's expected value. |
| `tests/unregistered-tests-gate.sh` (Gate 10, new) | **0** | wired into `run-all-gates.sh`; its own negative control exits 1 (§7.1) |
| `tests/no-tautology-gate.sh` | **0** | `PASS: every registered test file has test slots and real assertions, no literal tautologies` |
| coverage (Gate 2) | **skipped, stated** | needs a full `--with-coverage` build; the lane's mandate was teardown reliability, and I did not run it. |

---

## 9. What I could not reproduce, and the limits

1. **`AudioPortsTest` did not abort for me.** The brief names `AudioPortsTest` and `PdcMixerTest`;
   in 184 pre-fix runs (`AudioPortsTest` and `PdcMixerTest` in equal numbers, same rounds) the
   aborts were **31/184 on `PdcMixerTest` and 0/184 on `AudioPortsTest`**. The mechanism is shared
   (it lives in `~AudioEngine`, and `AudioPortsTest` does initialise the engine), but an
   unreproduced claim is not a fix, so I do not claim to have reproduced that half. The
   explanation consistent with the numbers: `AudioPortsTest` leaves the device rendering, which
   wakes the workers ~350 times a second and lets a stranded worker self-heal on the next period,
   whereas `PdcMixerTest` stops the device in `initTestCase()`, so the teardown-time wake-up is
   the **first and only** wake its workers ever see — the worst case for the race. That reasoning
   is not itself measured; what is measured is that both are fixed centrally, and that no test
   aborted in 30 post-fix suite runs.
2. **Half 2's necessity is unproven** (§3.2). It is retained as a deliberate structural
   guarantee, with its trade-off in the code comment.
3. **No independent reproduction of the product's own "abort on exit after a render"** — the
   released alpha records it as a known limitation. It is the same code path (`~AudioEngine`), so
   the fix should cover it, but I did not measure an application exit, and I do not claim it.
4. **The VST3 host tests are still unwired** (§7); Gate 10 now names them as open items.
   Registering them is a build-system task with SDK/fixture plumbing, out of this lane's scope.
5. **Gate 2 (coverage) was not run.**
6. **`PhaseDPerfBench` was not run** (it is a benchmark; excluded deliberately, §7).
7. The load in every measurement above is *real* sibling-lane compiles on this box (load average
   25–29, 20 cores, 30 GB RAM), not manufactured. Absolute abort rates will differ on a differently
   loaded machine; the counts and the mechanism, not the percentages, are the result.
