FIXED

# Cluster 5273 — a finished render or conversion scanned its output on the control (UI) thread

## Decision: REAL

The finding named `src/core/RenderQueue.cpp:136-144, 261-264`; line numbers have moved. On the base of
this branch (`3436a808b6`) the synchronous path is:

* `src/core/RenderQueue.cpp:212-221` — `reportOf()` reads the finished output for its report: the WHOLE
  file's SHA-256 (`BounceInPlace::sha256OfFile`, `:215`) and a second full decode for the loudness meter
  (`control::meterMeasureFile`, `:217`).
* `src/core/RenderQueue.cpp:241` — `conclude()` assigns `job->report = reportOf(*job)` INLINE.
* `src/core/RenderQueue.cpp:247-256` — `endJob()` calls `conclude()` on its caller's thread (`:255`).
* `src/core/RenderQueue.cpp:406-424` — `finish()` calls `endJob()` (`:412`).
* Both paths into `finish()` are the queue's own thread, i.e. the thread the control surface and the UI
  are answered on: the render's `QProcess::finished` / `errorOccurred` lambdas
  (`src/core/RenderQueue.cpp:364-380`) and the conversion worker's `Qt::QueuedConnection` post
  (`src/core/RenderQueue.cpp:391-397`). `RenderQueue` is a `QObject` singleton (`instance()`,
  `src/core/RenderQueue.cpp:282`) with no thread of its own, so "the queue's thread" is the control
  thread that also runs `render.queue_report` / `render.queue_list` /
  `src/core/ControlCommandsRenderQueue.cpp:71-82, 433-435` — and the GUI.

So a large render or conversion that finished ran a full-file hash plus a full-file decode inside that
callback: the control event loop (and so the UI) stayed blocked for as long as reading the output takes,
and the block scaled with the file. Bounce jobs are not affected and were already off-thread
(`src/core/RenderQueueBounce.cpp:200-213` finalises on a pool worker; its SHA-256 is computed there,
`src/core/BounceInPlace.cpp:246`), which is why the finding names renders and conversions.

## The fix

The scan is moved off the queue thread and its result is posted back; the job is NOT marked done until
the report is in place, so the existing contract "`state == done` implies a full `report`" (relied on by
`tests/src/core/ProjectToolsTest.cpp:155-156`, `ControlRenderJobTest.cpp:163-180`,
`tests/src/core/RenderSourceGuardTest.cpp:409`) is unchanged.

* `src/core/RenderQueue.cpp:241-254` — `conclude()` now decides only what the file says (wrote it / did
  not) and returns it; it no longer scans anything. A job that wrote nothing is `Failed` exactly as
  before, with the same messages.
* `src/core/RenderQueue.cpp:259-270` — `endJob()` returns whether the job wrote its file (commit of the
  staged output still happens here, before any report).
* `src/core/RenderQueue.cpp:425-445` — `finish()` hands such a job to `beginReport()` instead of ending
  it. Failure and cancellation take the old path (phase `failed` / `cancelled`, `TaskCenter` finish,
  queue released).
* `src/core/RenderQueue.cpp:451-464` — `beginReport()` sets phase `reporting`, keeps the slot, and starts
  `reportOf(out, startedMs, finishedMs)` on `QThreadPool::globalInstance()`; the report is posted back to
  the queue object with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
* `src/core/RenderQueue.cpp:466-478` — `applyReport()` assigns the report, then `state = Done`,
  `progress = 1.0`, phase `done`, `TaskCenter::finish(Done)` and releases the queue. A report that
  arrives for a job that was cancelled meanwhile is dropped.
* `src/core/RenderQueue.cpp:216-226` — `reportOf()` takes the three values it reads instead of the job,
  so the worker copies a path and two timestamps and never touches the job list. It still calls
  `meterMeasureFile`, which is a pure function of the file (`src/core/ControlCommandsMeterFile.cpp:195-250`:
  libsndfile + `LoudnessReport`, no engine or registry state), safe off the control thread.
* `src/core/RenderQueue.cpp:490-512` — `cancel()` gained the reporting window: a job whose report is
  being read has finished its work, so its task is reported cancelled and its slot released at once, and
  `applyReport()` drops the late report (`:495, 504-508`).
* `include/RenderQueue.h:149-152` — the three private declarations.

`bounce` jobs, `RenderQueue::convert()` (synchronous), `clearFinished()` and the capacity ratchet are
untouched.

## Test matrix

| path | how it is covered |
| --- | --- |
| normal: a conversion finishes | `ProjectToolsTest::aConversionReportIsReadOffTheControlThread` — report present, `sha256` non-empty, scan on a non-control thread |
| normal: a render finishes | `RenderSourceGuardTest::aRenderReportIsReadOffTheControlThread` — `bytes == 15` and the `sha256` of the committed destination, scan on a non-control thread |
| failure: no file written | `TaskCenterTest::failedRenderSurfacesChildDiagnostics`, `RenderSourceGuardTest::renderToSymbolicLinkDirectoryDoesNotReplaceIt` (report never built, `Failed`) |
| exhaustion: queue full | `ControlRenderJobTest` capacity slots; nothing added to the capacity path |
| cancellation: while running / while reporting | `TaskCenterTest::cancellingConversionStopsWorkerAndAdvancesQueue`, `cancellingRenderPreservesPreviousDestination`, `ProjectToolsTest::aQueuedJobIsCancelledByNumber`; the reporting window itself has no test (it needs a report to be in flight, i.e. a gate on the pool) and is verified by reading — see Self-check |
| audio thread | untouched: `RenderQueue` and everything above run on the control thread; no allocation, lock, wait or log added to any audio-path function |
| platform | Qt 5.10+ functor `invokeMethod` (already used in `RenderQueueBounce.cpp:211`), `QCryptographicHash`, `std::thread`/`std::mutex` only in tests; the render sibling is inside the file's existing non-Windows guard |

## The regression test and its seam

`RenderQueue` gained a test-only observation point, committed BEFORE the fix so the reversal proof
reverts the fix and not the seam (the same shape and the same reason as cluster 3714's
`AudioEngineWorkerThread::WorkerDrainProbe`):

* `include/RenderQueue.h:131-135` / `src/core/RenderQueue.cpp:61-62, 218, 315-318` — `ReportProbe` and
  `setReportProbeForTesting()`; the probe is called at the top of `reportOf()`, i.e. on whichever thread
  runs the scan, and records nothing itself.

The tests install it and assert the thread that ran the scan is not the thread the test (and so the
control surface) runs on:

* `tests/src/core/ProjectToolsTest.cpp:174-194` (helpers `:79-123`);
* `tests/src/core/RenderSourceGuardTest.cpp:418-438` (helpers `:68-112`).

No sleep, no elapsed-time assertion and no injected delay: the conversion/render is a real job driven
through the event loop to its own `done` state, and the probe only names the thread. With the fix
reverted the probe fires on the control thread and the slot fails on that assertion (see RED below).

## Commands and exit codes

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/5273.r1/logs` (every command below wrote its
full output to the named log).

Baseline before any change:

```sh
cmake --build build --target RenderSourceGuardTest ProjectToolsTest -j4 > .../logs/build-1.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^(RenderSourceGuardTest|ProjectToolsTest)$' --output-on-failure > .../logs/test-1.log 2>&1
# EXIT=0   (2/2 passed)
```

Seam + regression test, no fix yet (RED):

```sh
cmake --build build --target zene RenderSourceGuardTest ProjectToolsTest -j4 > .../logs/build-2.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^(RenderSourceGuardTest|ProjectToolsTest)$' --output-on-failure > .../logs/test-2.log 2>&1
# EXIT=8   (@RenderSourceGuardTest: FAIL! 'probe.thread() != std::this_thread::get_id()' returned FALSE;
#           @ProjectToolsTest: the same, 0% tests passed, 2 tests failed out of 2)
```

The fix (GREEN) and the whole family on final binaries:

```sh
cmake --build build --target zene RenderSourceGuardTest ProjectToolsTest ControlRenderJobTest \
  RenderTaskLifecycleTest TaskCenterTest RenderMenuQueueTest -j4 > .../logs/build-3.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^(RenderSourceGuardTest|ProjectToolsTest|ControlRenderJobTest|RenderTaskLifecycleTest|TaskCenterTest|RenderMenuQueueTest)$' --output-on-failure > .../logs/test-3.log 2>&1
# EXIT=0   100% tests passed out of 6
./RenderSourceGuardTest aRenderReportIsReadOffTheControlThread; ./ProjectToolsTest aConversionReportIsReadOffTheControlThread > .../logs/test-4.log 2>&1
# RENDER_SLOT_EXIT=0, CONVERT_SLOT_EXIT=0  (both named slots PASS)
```

Step 4b, the reversal proof (`fix.patch` = the two product files, the tests stay in place):

```sh
git diff -- src/core/RenderQueue.cpp include/RenderQueue.h > .../logs/fix.patch
# DIFF_EXIT=0      (184 lines)
git apply -R .../logs/fix.patch
# REVERSE_APPLY_EXIT=0
cmake --build build --target RenderSourceGuardTest ProjectToolsTest -j4 > .../logs/build-5.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^(RenderSourceGuardTest|ProjectToolsTest)$' --output-on-failure > .../logs/test-5.log 2>&1
# EXIT=8   RED: both slots FAIL -
#   RenderSourceGuardTest::aRenderReportIsReadOffTheControlThread 'probe.thread() != std::this_thread::get_id()'
#     returned FALSE. (the finished render's report was read on the control thread, the thread this test runs on)
#   ProjectToolsTest::aConversionReportIsReadOffTheControlThread ... (the same)
git apply .../logs/fix.patch
# APPLY_EXIT=0
cmake --build build --target RenderSourceGuardTest ProjectToolsTest -j4 > .../logs/build-6.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^(RenderSourceGuardTest|ProjectToolsTest)$' --output-on-failure > .../logs/test-6.log 2>&1
# EXIT=0   100% tests passed out of 2
```

Final rebuild of `zene` and of every test target that includes `include/RenderQueue.h`
(`ControlRenderJobTest`, `RenderTaskLifecycleTest`, `TaskCenterTest`, `RenderMenuQueueTest`,
`ProjectToolsTest`, `RenderSourceGuardTest`), then the family:

```sh
cmake --build build --target zene RenderSourceGuardTest ProjectToolsTest ControlRenderJobTest \
  RenderTaskLifecycleTest TaskCenterTest RenderMenuQueueTest -j4 > .../logs/build-7.log 2>&1
# EXIT=0
cd build/tests && ctest -R '^(RenderSourceGuardTest|ProjectToolsTest|ControlRenderJobTest|RenderTaskLifecycleTest|TaskCenterTest|RenderMenuQueueTest)$' --output-on-failure > .../logs/test-7.log 2>&1
# EXIT=0   100% tests passed out of 6
```

Gates:

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5273 > .../logs/gates-1.log 2>&1
# GATES_EXIT=0
#   PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
#        no-tautology / doc-figures; complexity and file-length: "no new item in your files"
#   RESULT: PASS
```

`.../logs/build-4.log` is a background build attempt that never started (0 bytes, stopped and rerun as
build-5 in the foreground); no result is claimed from it.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with it.** `test-5.log`
  EXIT=8 (both slots fail on the thread assertion) and `test-6.log` EXIT=0 after
  `git apply .../logs/fix.patch` — commands and exit codes above, copied from this session's logs.
* **The test is deterministic.** It uses the `ReportProbe` seam (`include/RenderQueue.h:131-135`): the
  probe is called by the product on the thread that runs the scan and records
  `std::this_thread::get_id()` under a mutex; the test then compares it with its own thread id. There is
  no sleep, no injected delay and no elapsed-time assertion. The only wait is
  `QTRY_COMPARE_WITH_TIMEOUT(stateOf(job), "done", 20000)` / `QTRY_VERIFY_WITH_TIMEOUT(... Done, 5000)`,
  the bounded event-loop poll the neighbouring slots in both files already use: it waits for the job's
  own state transition and cannot pass or fail on machine speed (a slow box only polls longer).
* **The fix is at the cause and complete.** Callers checked: `conclude()` and `endJob()` have exactly one
  caller each (`src/core/RenderQueue.cpp:267`, `:428`); `finish()` is called only from the render's
  process lambdas and the conversion worker's queued post, and both now go through `beginReport()`.
  Siblings checked: the bounce path already computed its report on a worker
  (`RenderQueueBounce.cpp:200-213`) and is untouched; `RenderQueue::convert()` is the synchronous
  compatibility path and never built a report, still does not. Readers checked:
  `ControlCommandsRenderQueue.cpp:71-82` (`render.queue_list` / `render.queue_report`) and
  `TaskCenter` show `running`/`reporting` until the report is in place, so no reader can observe a
  `done` job without its report.
* **No collateral damage.** Nothing in project save/load, undo or the command surface changed (no
  command added, no snapshot moved; the undo ledger is untouched — this path journals nothing). The
  failure path is byte-for-byte the old one (same error strings, `Failed`, no report). No limit was
  added. The one behavioural consequence is deliberate: a job that wrote its file now stays `running`
  (phase `reporting`, progress -1) until its report is in place, and the queue's next job starts after
  that, which is what keeps "done implies report". Old projects load as before — nothing serialised
  changed.
* **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; the verdict is FIXED.

## Other findings

* `docs/qwen-fixes-2026-10/cluster-new-p23-21.md` describes `finish()` as "calls `find()`, `endJob()` and
  `TaskCenter::finish()`, and changes the job state only". That is still true for every path except the
  new success path, which now continues in `beginReport()`; the old report is a historical record and was
  not edited.
* `reportOf()` still reads the destination path a second time after the file was committed. A user who
  moves or rewrites the output during the (now off-thread) scan gets a report of the file as it is then;
  the pre-fix window was narrower but non-zero. Not changed here — it is outside this finding.
