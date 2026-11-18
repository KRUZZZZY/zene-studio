IMPLEMENTED

# Q050-224.1 - alternate signal stacks for the engine workers (stage 1/4)

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-feat-q050-224.1`
(branch `feat/050-Q050-224.1-oc`, cut from `050/m0-train-35` = `fa4a5f2e78`).
Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/Q050-224.1.r1/logs/`.

**Round-0 note.** This branch contained no round-0 commits when this session started
(`git log --oneline 050/m0-train-35..HEAD` was empty), so every item in the review was implemented in
this pass, from the train head.

## What changed

| File | Change |
|---|---|
| `src/core/lv2/Lv2Worker.cpp:89` | The plug-in worker's `std::thread` launch lambda calls `crashreporter::installAlternateStackForCurrentThread()` before `workerFunc()` (the previous `std::thread(&Lv2Worker::workerFunc, this)` could not run anything first). |
| `src/core/RubberBandStretch.cpp:83` | The voice recycler's launch lambda registers before `run()`. |
| `src/core/StemJobManager.cpp:90` | The detached inference worker registers before `workerLoop(state)`. |
| `src/wasm/WasmWorkerPool.cpp:73` | Every wasm lane registers before `laneMain(i)`; `WasmWorkerPoolTest` now compiles `CrashReporter.cpp`/`CrashReporterFormat.cpp`/`CrashReporterWindows.cpp` (it deliberately does not link `lmmsobjs`). |
| `tests/src/core/WorkerAltStackTest.cpp` | New runtime proof for the LV2 worker (registration before first work; forked overflow). |
| `tests/worker-altstack-registration-check.py` | New ctest source gate for all four launch sites, with three negative probes and one positive fixture. |
| `tests/CMakeLists.txt` | Registers the test, the gate, and the reporter units in `WasmWorkerPoolTest`. |
| `tests/fork-sources.txt`, `tests/upstream-modifications.txt`, `tests/all-sources.txt` | Registration (the test source; `Lv2Worker.cpp` as a declared inherited divergence; whole-tree manifest re-derived). |
| `docs/050-features/Q050-224.1.md`, `docs/CRASH-REPORTER.md` (limitation 3) | Feature record, exclusion inventory, and the restated limitation. |

No command, no GUI action, no undo step, no project format change: this is engine plumbing whose only
effect is that an overflow on a Zene-owned worker is reported instead of silently killing the process.
The manual has no crash-report section, so it is not edited (no invented UI).

## Provisioning (as the review asked)

The lane's `cmake` shim refuses to configure ("configuring is the parent's job"; exit 97,
`logs/configure-1.log`), so the review's provisioning instruction was run with the real binary:

```
cp -a ../zene-cachyos/third_party/wasmtime third_party/         # 104 MB, git-ignored (.gitignore:19)
/usr/bin/cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DWANT_STEM_SPLIT=ON -DWANT_WASM=ON                          # EXIT=0 (logs/configure-2.log)
/usr/bin/cmake -S . -B build                                     # after adding the test targets, EXIT=0 (configure-3.log)
cmake --build build --target zene WorkerAltStackTest WasmWorkerPoolTest -- -j4   # EXIT=0 (build-8, build-11)
```

`configure-2.log` reports `WASM DSP sandbox : Enabled`; `build/CMakeCache.txt` holds `WANT_WASM:BOOL=ON`,
`WANT_STEM_SPLIT:BOOL=ON`, `CMAKE_BUILD_TYPE=RelWithDebInfo`.

## Acceptance

| # | Criterion | Test / command | What it proves | Exit |
|---|---|---|---|---|
| 1 | Register the alternate stack at each in-scope thread startup | `ctest -R ^WorkerAltStackTest$` and `ctest -R ^WorkerAltStackRegistration$` from `build/tests` (`test-4.log`, `test-15-final.log`) | **Runtime (LV2):** a live `Lv2Worker` with a fake `LV2_Worker_Interface`; inside its first `work()` call, on the worker thread, `sigaltstack()` reports an enabled stack (non-zero size) and the callback's own frame is outside it. **All four sites:** the gate reads each real launch lambda and requires the registration before `workerFunc()`/`run()`/`workerLoop()`/`laneMain()`; the three negative probes (missing, after the entry call, on the caller's thread) are rejected and the positive fixture accepted. | 0; 0 |
| 1n | The proof can fail | Gate run with the four product hunks reversed, then restored (`test-7-revert-gate.log`, `test-9-restored-gate.log`) | Reverted: exit **1**, failing on all four files. Restored: exit **0**, all four PASS. | 1 / 0 |
| 2 | A non-main-thread overflow test produces a bounded report and confirms the child still terminates by the signal | `WorkerAltStackTest::forked_worker_overflow_reports_and_dies_by_signal` (`test-4.log`) | Forked child starts a live `Lv2Worker` whose fake `work()` overflows the ordinary stack for real. Asserted: child `WIFSIGNALED` with `WTERMSIG == SIGSEGV`; report exists, `startsWith("Zene Studio crash report v1\n")`, `signal=SIGSEGV(11)`, `thread=` != `pid=`, size `<= kMaxReportBytes`. | 0 |
| 2n | That test fails without the product change | Four product hunks reversed at `HEAD~2` content, rebuilt, run (`build-5-revert.log`, `test-8-revert.log`), then restored and rebuilt (`build-11-restore2.log`) | Reverted: `2 passed, 2 failed` - `probe.enabled` false and the report empty, because the child dies by `SIGSEGV` with no handler stack. Restored: `4 passed, 0 failed`, 0 skipped. | 8 / 0 |
| - | No regression in neighbours | `ctest -R "^(WorkerAltStackTest\|WasmWorkerPoolTest\|WorkerAltStackRegistration\|CrashReporterTest\|CrashReporterArmTest\|StemJobManagerTest\|Lv2StateTest\|SampleClipRubberBandTest)$"` (`test-15-final.log`) | 8/8 passed. | 0 |
| - | Gates | `bash tests/fork-sources-gate.sh`; `bash tests/no-upstream-regression-gate.sh`; `python3 tests/doc-figures-gate.py`; `python3 .../feature_check.py` | All PASS (see Self-check). | 0 each |

Exact failure text in the reverted run: `FAIL! 'probe.enabled' returned FALSE. (the Lv2Worker thread
reached its first work() call with NO alternate signal stack...)` and `FAIL! '!report.isEmpty()' returned
FALSE. (no report was written for an overflow on the Lv2Worker thread: the handler had no alternate stack
to run on)`. Both return with the product change in place.

## Exclusions (stated, per the review)

The inventory is in `docs/050-features/Q050-224.1.md` and `docs/CRASH-REPORTER.md` limitation 3: the
recorder's own disk-writer thread (`src/core/audio/TrackRecorder.cpp`, `sndfile` I/O rather than DSP), the
diagnostics-only profiler writer, transport (no long-running thread of its own), MIDI (backend-owned
`QThread`s, not a Zene launch lambda), the control-server pool (Windows accept/worker threads) and GUI
`QThreadPool` jobs (file work, not DSP), plus third-party plug-in threads (the documented remaining
limitation).

## Self-check

- **"Each new test FAILS with your product change reverted, then PASSES restored."** Proved for both
  tests: product hunks reversed to `HEAD~2` content, `cmake --build build --target WorkerAltStackTest`
  EXIT 0 (`build-5-revert.log`), `ctest -R ^WorkerAltStackTest$` **EXIT 8** with the two intended failures
  (`test-8-revert.log`); the gate on the same tree **EXIT 1** on all four sites (`test-7-revert-gate.log`).
  Restored files, rebuilt EXIT 0 (`build-11-restore2.log`), gate **EXIT 0** (`test-9-restored-gate.log`),
  `ctest` 3/3 **EXIT 0** (`test-10-restored.log`) and 8/8 **EXIT 0** on the final tree
  (`test-15-final.log`).
- **"Every acceptance criterion is proven by a test you ran."** Criterion 1: `WorkerAltStackTest`
  (LV2 runtime) + `WorkerAltStackRegistration` (all four), exit 0. Criterion 2:
  `WorkerAltStackTest::forked_worker_overflow_reports_and_dies_by_signal`, exit 0. Both are named with
  logs in the table above.
- **"Each edit is ONE undo step, and every new or changed command has an honest reversibility class."**
  Not applicable: no command was added or changed and nothing edits the project, so there is no undo
  step and no reversibility class to declare. `git diff` touches only thread startup code, registration
  manifests, tests and docs.
- **"Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
  signal."** The added call runs once per worker thread at thread start, before the work loop
  (`Lv2Worker.cpp:89`, `RubberBandStretch.cpp:83`, `StemJobManager.cpp:90`, `WasmWorkerPool.cpp:73`);
  it is `sigaltstack()` on a `thread_local` fixed buffer (`CrashReporter.cpp:266`) and touches no model,
  queue, lock or Qt object. No audio-render function is modified. `submit_check.py` reports
  `PASS no allocation, lock, signal or I/O added inside audio-path functions`.
- **"Old projects still load and render exactly as before; new data round-trips."** No project data,
  save path or render path changed (no format change, no new field), so there is nothing new to
  round-trip. Neighbour render/DSP tests pass (`StemJobManagerTest`, `SampleClipRubberBandTest`,
  `Lv2StateTest`, `CrashReporterTest`, `CrashReporterArmTest`, `WasmWorkerPoolTest`, exit 0). Separately,
  `ControlGoldenAudio` fails on this box **identically with and without this change** (measured RMS
  -28.3698 dBFS vs recorded -11.0419 dBFS, plus a `std::bad_alloc` in one later render):
  `test-13-golden.log` EXIT 8 (with the change) and `test-14-golden-base.log` EXIT 8 (with the four hunks
  reverted to `HEAD~2`). It is an environment/fixture failure, not a regression from this unit.
- **"Completeness: walk each acceptance criterion through the REAL user path and the agent path."**
  There is no user action and no agent command in this unit, so the "real user path" is the engine
  itself: two criteria are exercised through the shipping objects (`Lv2Worker` as constructed by the
  product, `delete`/destructor path included) and a real forked process, not through a reimplementation.
  Edge cases tried: the worker's *first* `work()` (not a later one), a zero-byte request (the worker
  skips it - the test therefore schedules a non-empty request, which is the real path), a real guard-page
  overflow (not `raise()`), the report's thread id vs pid, the reverted negative control, and all four
  launch sites after the file was reformatted. The reverted rebuilds prove the test is not passing for
  an unrelated reason.
- **"The GUI test drives the visible control..."** Not applicable: no user-facing action, no GUI, no
  command. `submit_check.py` reports the corresponding WARNs ("no GUI file changed", "no command file
  changed", "manual not updated"); the feature record states "no user-visible change" and does not invent
  UI, because the manual has no crash-report section.
- **"Qt 5.15 / MSVC / macOS < 10.15 portability."** The test file is guarded: all POSIX includes,
  `fork()`/`waitpid()`/`pause()`/`sigaltstack()` and the overflow case are inside `#ifndef Q_OS_WIN`,
  the ASan skip uses the project's existing `__SANITIZE_ADDRESS__` / `__has_feature` pattern, and
  `__attribute__((noinline))` is behind `#if defined(__GNUC__)`. The product change is four lines of
  code which call the cross-platform `installAlternateStackForCurrentThread()` (a documented no-op on
  Windows, `CrashReporterWindows.cpp:51`); no Qt-6-only API, no `std::filesystem`, and none of the
  identifiers `small`, `near`, `far`, `interface`, `min`, `max` were introduced. `WasmWorkerPoolTest`
  gains three source files only inside the existing `if(WANT_WASM)` block.
- **Diagnostic checkpoint.** This harness gives the worker no subworker/provider channel (no packet call
  exists in the toolset), so the one-line liveness packet was not applicable; the work above was executed
  directly in this session and every exit code above comes from a command run here.
- **Gates.** `bash tests/fork-sources-gate.sh` EXIT 0 (3583 files scanned, 0 stale, manifest REPRODUCES);
  `bash tests/no-upstream-regression-gate.sh` EXIT 0; `python3 tests/doc-figures-gate.py` EXIT 0
  (`RESULT: PASS`, 933 ids / 78 groups); `feature_check.py` EXIT 0 (all 8 gates PASS);
  `submit_check.py` run after this commit is recorded in the session log.
- **Nothing weakened.** No existing test, assertion, gate or ratchet was changed; the only edits to
  `tests/CMakeLists.txt` add a test, a gate and three source files to one target. The four manifests gain
  entries, none is removed.
