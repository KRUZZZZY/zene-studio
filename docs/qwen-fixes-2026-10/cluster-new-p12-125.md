FIXED

# Cluster new-p12-125 — "Non-threaded LV2 worker runs work synchronously from scheduling"

## Verdict

REAL, and fixed. Round 1's FALSE-POSITIVE verdict was wrong: I had treated `renderOnly()` as "no
real-time audio thread", but `Engine::init(true)` starts `AudioDummy`, whose paced `run()` calls
`renderNextPeriod()` on its own thread, and `--script` can load a project and play it through that
device (`ScriptBindings.cpp:891-894`, `ScriptEngine.cpp:590-592`). `renderNextPeriod()` opens a
`RealtimeScope` (`src/core/AudioEngine.cpp:1212`), so a render-only `Lv2Worker` was calling the
plug-in's `work()` inline on that realtime-paced thread.

The fix: a render-only worker may still run `work()` inline, but only while the offline
(deterministic) render flag is set — which only `ProjectRenderer` sets. Everywhere else, including
`AudioDummy`'s paced rendering, the request is queued to the worker thread. The worker thread now
always exists, so the audio thread never has to allocate or start one.

## Root cause (line numbers re-read in this worktree)

| Fact | Location |
| --- | --- |
| Render-only engine (used by `--render` and `--script`) starts `AudioDummy` | `src/core/AudioEngine.cpp:796-800`; `src/core/main.cpp:1479`, `:1626` |
| `AudioDummy` is a paced device thread that calls `renderNextPeriod()` | `include/AudioDummy.h:93-123` |
| `renderNextPeriod()` enters a `RealtimeScope` | `src/core/AudioEngine.cpp:1212` |
| The worker mode is chosen from `renderOnly()` | `src/core/lv2/Lv2Proc.cpp:603-605` |
| Old non-threaded branch ran `work()` inline unconditionally | `src/core/lv2/Lv2Worker.cpp:203-211` (before this fix) |
| Only `ProjectRenderer` marks a deterministic offline render | `src/core/ProjectRenderer.cpp:94-101`, `:289-290`; `include/AudioEngineWorkerThread.h:139-140` |

## The fix (product diff)

- `include/Lv2Worker.h:49-54`, `:77`: the constructor parameter's contract is documented — `threaded
  == true` always queues; `threaded == false` (render-only) inlines only during an offline render and
  queues otherwise. The worker thread always exists.
- `src/core/lv2/Lv2Worker.cpp:103-112`: the `std::thread` is started unconditionally (with its
  alternate-stack registration). This is what makes the audio thread able to queue without starting
  or allocating anything.
- `src/core/lv2/Lv2Worker.cpp:140-145`: the destructor always posts and joins.
- `src/core/lv2/Lv2Worker.cpp:205-241`: `scheduleWork()` chooses `inlineWork = !m_threaded &&
  s_inlineWorkDepth == 0 && s_queuedWorkDepth == 0 &&
  AudioEngineWorkerThread::deterministicProcessing()`. The inline branch is the old offline path
  (semaphore + `work()` on the render thread); the else branch is the old threaded, lock-free ring
  path. `AudioDummy` never sets the deterministic flag, so its scheduling takes the queue path.
- `src/core/lv2/Lv2Worker.cpp:42-70`, `:151-172`: `respond()` delivers `work_response()` straight to
  `run()` only when it is called from an inline `work()` (thread-local inline depth), and queues it
  otherwise — so queued work on the worker thread still responds through `emitResponses()` on the
  audio thread. The queued-depth guard also makes a nested `schedule_work()` from inside `work()`
  queue instead of re-taking the work lock that thread already holds.
- No change to `Lv2Proc.cpp`, plugin save/load, commands, undo, or project data.

Realtime: the queue path is the existing lock-free `LocklessRingBuffer` write plus `Semaphore::post`
(no allocation, lock or wait), the same code the live GUI path already runs on the audio thread. The
inline path is reachable only on the offline render thread.

## Test matrix

| Path | Coverage |
| --- | --- |
| Normal live/AudioDummy path (deterministic flag off) | `lv2_worker_live_processing_keeps_work_off_the_calling_thread` — `work()` must not run on the scheduling thread |
| Offline-render path (deterministic flag on) | `lv2_worker_offline_render_still_runs_work_inline` — `work()` must run inline before `schedule_work()` returns |
| Failure/exhaustion | unchanged: the queued path keeps its `m_requests.free() < sizeof(size) + size` → `LV2_WORKER_ERR_NO_SPACE` check (`Lv2Worker.cpp:227-230`); no new limit was added |
| Reconfiguration | none: `renderOnly` is fixed for the engine lifetime and is read once at worker construction (`Lv2Proc.cpp:603`) |
| Audio-thread constraint | live scheduling now takes only the existing lock-free ring + semaphore post; no allocation/lock/wait added |
| Platforms | test uses only QtTest and `std::thread`; the test file is already registered (`tests/CMakeLists.txt:2119`) and builds on the configured Linux tree |

The test drives the affected decision directly with the same seam the production code reads:
`AudioEngineWorkerThread::setDeterministicProcessing(false/true)`. `AudioDummy`'s thread never calls
the setter, so `false` is exactly the live/`AudioDummy` state; `ProjectRenderer` is the only caller
that passes `true`. No sleeps or wall-clock assertions are used; the live test waits for the worker
to finish with `QTRY_VERIFY_WITH_TIMEOUT` and then asserts the thread identity.

## Commands and exit codes (all logs in `.../new-p12-125.r2/logs/`)

| Step | Command | EXIT | Log / result |
| --- | --- | --- | --- |
| RED build | `cmake --build build --target WorkerAltStackTest -j4` | 0 | `build-red-1.log` |
| RED test | `cd build/tests && ctest -R '^WorkerAltStackTest$' --output-on-failure` | 8 | `test-red-1.log` — new live test FAILED: "work() ran inline on the thread that called schedule_work()"; 5 passed, 1 failed |
| GREEN build | `cmake --build build --target zene WorkerAltStackTest -j4` | 0 | `build-green-4.log` |
| GREEN test (final) | `cd build/tests && ctest -R '^WorkerAltStack' -V` | 0 | `test-green-5.log` — 6 passed, 0 failed; family 2/2 (`WorkerAltStackTest`, `WorkerAltStackRegistration`) |
| 4b save patch | `git diff -- include/Lv2Worker.h src/core/lv2/Lv2Worker.cpp > .../fix.patch` | 0 | `fix.patch` |
| 4b reverse | `git apply -R .../fix.patch` | 0 | product files reverted, test kept |
| 4b rebuild | `cmake --build build --target zene WorkerAltStackTest -j4` | 0 | `build-reverted-2.log` |
| 4b test reverted | `cd build/tests && ctest -R '^WorkerAltStackTest$' --output-on-failure` | 8 | `test-reverted-2.log` — live test FAILED again, offline test passed |
| 4b reapply | `git apply .../fix.patch` | 0 | product files restored |
| 4b rebuild | `cmake --build build --target zene WorkerAltStackTest -j4` | 0 | `build-green-4.log` |
| 4b test restored | `cd build/tests && ctest -R '^WorkerAltStack' -V` | 0 | `test-green-5.log` — PASS |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `feature-check-1.log` — all 8 rows PASS, `RESULT: PASS` |
| Completion | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p12-125` | see `submit-check-1.log` | recorded after the report commit |

Registration: the two changed product files are declared in `tests/upstream-modifications.txt`
(`include/Lv2Worker.h`, `src/core/lv2/Lv2Worker.cpp`); the test file is already in
`tests/fork-sources.txt`.

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it:** `test-reverted-2.log`
  (EXIT=8, the live test fails with "work() ran inline on the thread that called schedule_work()")
  and `test-green-5.log` (EXIT=0, 6 passed / 0 failed). The same test failed before the first product
  edit in `test-red-1.log` (EXIT=8).
- **Deterministic test:** no wall clock, sleep or race assertion. The seam is
  `AudioEngineWorkerThread::setDeterministicProcessing(false/true)`, the production flag
  `ProjectRenderer` alone sets; the live test waits for the worker's completion flag with
  `QTRY_VERIFY_WITH_TIMEOUT` and then compares `std::thread::id`s. `AudioDummy` never sets the flag,
  so `false` is the real live state.
- **Fix at the cause and complete:** the scheduling decision in `Lv2Worker::scheduleWork()` is the
  cause; `respond()`, the worker thread lifecycle, and the destructor are kept consistent with it.
  `Lv2Proc.cpp` needs no change: it still passes `!renderOnly()`. Sibling paths checked:
  `respond()` queue-vs-direct, `workerFunc()` queued execution, `emitResponses()` and
  `notifyPluginThatRunFinished()` were read and remain correct for both modes.
- **No collateral damage:** no save format, project data, command, undo or GUI file changed. Offline
  `--render` exports still run `work()` inline on the render thread (the second test asserts this),
  and live/GUI workers still always queue as before. The only new behaviour is that a render-only
  worker during paced `AudioDummy` processing queues instead of inlining; the existing 32 KiB request
  ring and its `NO_SPACE` result are unchanged.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable; this is a FIXED verdict with the code
  and contract above, and round 1's false-positive claim is explicitly retracted here.
- **Sibling commands:** no command file was changed, so no command siblings exist. `submit_check.py`
  lists `app.*`/`arrangement.*` ids only because its diff range includes the pre-existing train
  commits; this job's commit contains only the LV2 worker fix, its test, the two registration lines
  and this report. The `ControlCommandsSnapshot` canary is untouched.
- **Completion check:** `submit_check.py` run after the report commit, log `submit-check-1.log`.

## Other findings

- `AudioEngine::renderNextPeriod()` opens a `RealtimeScope` unconditionally
  (`src/core/AudioEngine.cpp:1212`), including offline renders. In a `WANT_DEBUG_RTSAN` build the
  inline offline `work()` path can therefore report a plug-in's own allocations as realtime
  violations even though the export thread has no deadline. Not fixed here (instrumentation-only,
  outside this bug).
