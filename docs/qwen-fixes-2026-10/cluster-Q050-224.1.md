IMPLEMENTED

# Q050-224.1 - alternate signal stacks for the engine workers (stage 1/4)

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-feat-q050-224.1`
(branch `feat/050-Q050-224.1-oc`, cut from `050/m0-train-35` = `fa4a5f2e78`).
Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/Q050-224.1.r3/logs/`.

**Round history.** Round 0 had no commits on the branch when it started; rounds 1-2 implemented the four
sites, the LV2 runtime proof and the four-site gate. Round 2's review found one blocking CI defect and
three documentation/inventory points; this round fixes them (below). Every result quoted here was produced
in this session, in `r3`.

## Round-3 changes (the review's points)

1. **Windows compile (blocking).** `tests/src/core/WorkerAltStackTest.cpp` used `stack_t`, `::sigaltstack`
   and `SS_DISABLE` under `#ifdef LMMS_HAVE_LV2` alone, while `<atomic>` was included only under
   `#ifndef Q_OS_WIN`. Windows CI defines `LMMS_HAVE_LV2` (vcpkg installs lilv; the msys2 clang-arm64
   dependency file lists it), and neither MSVC nor mingw has `sigaltstack`. Fixed without weakening:
   - `<atomic>` is now included unconditionally (it is standard C++ and `AltStackProbe` uses it on every
     platform).
   - `overflowNow()` is compiled only under `#if !defined(Q_OS_WIN)`; `fakeWork()`'s sigaltstack query and
     the overflow branch are the `#else` of `#if defined(Q_OS_WIN)`. On Windows the callback records that
     the worker reached `work()` and returns.
   - `lv2_worker_has_alternate_stack_before_first_work()` **still runs on Windows** - it is not skipped -
     and asserts the documented no-op contract from the same call the launch lambdas make:
     `QVERIFY2(!lmms::crashreporter::installAlternateStackForCurrentThread(), ...)`, after a live
     `Lv2Worker` has run `work()`. The second slot's Windows skip is unchanged from the accepted round-2
     file (there is no `fork()`/`SIGSEGV` reporting to test in a no-op reporter).
   - The header comment now says "three negative probes" (it said two).
   A local preprocessor model (below) shows no POSIX token is active in either the MSVC or the mingw
   Windows branch.
2. **Loop-body claim.** `docs/050-features/Q050-224.1.md` no longer claims a probe for loop-body
   placement; it says the single predicate rejects a lambda that does not register before its entry call,
   which is why a moved registration is rejected, and lists the three probes that do exist (missing,
   after the entry call, on the caller's thread).
3. **Exclusion inventory.** Added rows for `src/core/LinkAudioStream.cpp:469` (network audio I/O),
   `src/core/TakeRecorder.cpp:90` (recorder family, beside `TrackRecorder`) and
   `src/core/Oscillator.cpp:313-316` (one-shot start-up wavetable threads), in the feature record and in
   `docs/CRASH-REPORTER.md` limitation 3.
4. **Log paths.** All citations below are `r3` logs produced in this session; the earlier `r1` directory
   no longer exists.

## What changed overall

| File | Change |
|---|---|
| `src/core/lv2/Lv2Worker.cpp:89` | The plug-in worker's `std::thread` launch lambda calls `crashreporter::installAlternateStackForCurrentThread()` before `workerFunc()` (the previous `std::thread(&Lv2Worker::workerFunc, this)` could not run anything first). |
| `src/core/RubberBandStretch.cpp:83` | The voice recycler's launch lambda registers before `run()`. |
| `src/core/StemJobManager.cpp:90` | The detached inference worker registers before `workerLoop(state)`. |
| `src/wasm/WasmWorkerPool.cpp:73` | Every wasm lane registers before `laneMain(i)`; `WasmWorkerPoolTest` now compiles `CrashReporter.cpp`/`CrashReporterFormat.cpp`/`CrashReporterWindows.cpp` (it deliberately does not link `lmmsobjs`). |
| `tests/src/core/WorkerAltStackTest.cpp` | Runtime proof for the LV2 worker (registration before first work; forked overflow), portable to Windows as described above. |
| `tests/worker-altstack-registration-check.py` | ctest source gate for all four launch sites, with three negative probes and one positive fixture. |
| `tests/CMakeLists.txt` | Registers the test, the gate, and the reporter units in `WasmWorkerPoolTest`. |
| `tests/fork-sources.txt`, `tests/upstream-modifications.txt`, `tests/all-sources.txt` | Registration (the test source; `Lv2Worker.cpp` as a declared inherited divergence; whole-tree manifest re-derived). |
| `docs/050-features/Q050-224.1.md`, `docs/CRASH-REPORTER.md` (limitation 3) | Feature record, exclusion inventory, and the restated limitation. |

No command, no GUI action, no undo step, no project format change: this is engine plumbing whose only
effect is that an overflow on a Zene-owned worker is reported instead of silently killing the process.
The manual has no crash-report section, so it is not edited (no invented UI).

## Build state

The tree was provisioned in the previous round (copying the git-ignored `third_party/wasmtime` and
configuring with `WANT_STEM_SPLIT=ON`, `WANT_WASM=ON`; the lane's `cmake` shim refuses to configure, so
the review's instruction was run with `/usr/bin/cmake`). This round reused that configuration: the cache
still holds `WANT_WASM:BOOL=ON`, `WANT_STEM_SPLIT:BOOL=ON`, `CMAKE_BUILD_TYPE=RelWithDebInfo`, and

```
cmake --build build --target WorkerAltStackTest -- -j4    # EXIT=0 (build-1.log)
cmake --build build --target zene WorkerAltStackTest WasmWorkerPoolTest -- -j4   # EXIT=0 (build-3-restore.log)
```

## Acceptance

| # | Criterion | Test / command | What it proves | Exit |
|---|---|---|---|---|
| 1 | Register the alternate stack at each in-scope thread startup | `ctest -R ^WorkerAltStackTest$` and `ctest -R ^WorkerAltStackRegistration$` from `build/tests` (`test-1-focused.log`, `test-5-restored.log`) | **Runtime (LV2):** a live `Lv2Worker` with a fake `LV2_Worker_Interface`; inside its first `work()` call, on the worker thread, `sigaltstack()` reports an enabled stack (non-zero size) and the callback's own frame is outside it. On Windows the same live worker reaches `work()` and the slot asserts the reporter's documented no-op instead. **All four sites:** the gate reads each real launch lambda and requires the registration before `workerFunc()`/`run()`/`workerLoop()`/`laneMain()`; the three negative probes (missing, after the entry call, on the caller's thread) are rejected and the positive fixture accepted. | 0; 0 |
| 1n | The proof can fail | Gate run with the four product hunks reversed, then restored (`test-2-revert-gate.log`, `test-4-restored-gate.log`) | Reverted: exit **1**, failing on all four files. Restored: exit **0**, all four PASS. | 1 / 0 |
| 2 | A non-main-thread overflow test produces a bounded report and confirms the child still terminates by the signal | `WorkerAltStackTest::forked_worker_overflow_reports_and_dies_by_signal` (`test-1-focused.log`) | Forked child starts a live `Lv2Worker` whose fake `work()` overflows the ordinary stack for real. Asserted: child `WIFSIGNALED` with `WTERMSIG == SIGSEGV`; report exists, `startsWith("Zene Studio crash report v1\n")`, `signal=SIGSEGV(11)`, `thread=` != `pid=`, size `<= kMaxReportBytes`. | 0 |
| 2n | That test fails without the product change | Four product hunks reversed to the train content, rebuilt (`build-2-revert.log`), run (`test-3-revert.log`), then restored and rebuilt (`build-3-restore.log`) | Reverted: `2 passed, 2 failed` - `probe.enabled` false and the report empty, because the child dies by `SIGSEGV` with no handler stack. Restored: `4 passed, 0 failed`, 0 skipped. | 8 / 0 |
| - | No regression in neighbours | `ctest -R "^(WorkerAltStackTest\|WasmWorkerPoolTest\|WorkerAltStackRegistration\|CrashReporterTest\|CrashReporterArmTest\|StemJobManagerTest\|Lv2StateTest\|SampleClipRubberBandTest)$"` (`test-5-restored.log`) | 8/8 passed. | 0 |
| - | Gates | `bash tests/fork-sources-gate.sh`; `bash tests/no-upstream-regression-gate.sh`; `python3 tests/doc-figures-gate.py`; `python3 .../feature_check.py` | All PASS (see Self-check). | 0 each |

Exact failure text in the reverted run: `FAIL! 'probe.enabled' returned FALSE. (the Lv2Worker thread
reached its first work() call with NO alternate signal stack...)` and `FAIL! '!report.isEmpty()' returned
FALSE. (no report was written for an overflow on the Lv2Worker thread: the handler had no alternate stack
to run on)`. Both return with the product change in place.

## Windows portability model (no Windows toolchain here)

This box builds Qt 6/Linux only, so the Windows branch is checked structurally, the way other CI-only
fixes here are: a preprocessor model evaluates the file's real `#if`/`#elif`/`#else` tree with
`Q_OS_WIN=1`, `LMMS_HAVE_LV2=1` for both an MSVC-like (`__GNUC__=0`) and a mingw-like (`__GNUC__=1`)
environment, strips comments and asserts that none of `sigaltstack`, `stack_t`, `SS_DISABLE`, `pid_t`,
`fork(`, `waitpid`, `usleep`, `pause(`, `wait.h`, `unistd.h`, `signal.h`, `memset` is in an active line.
Result: 0 tokens for both environments, while the active Windows lines are the live `Lv2Worker` case,
`ran.store(...)`, and the `installAlternateStackForCurrentThread()` no-op assertion. Hosted CI remains the
final compiler check (msvc-x64 and windows-arm64).

## Exclusions (stated, per the review)

The inventory is in `docs/050-features/Q050-224.1.md` and `docs/CRASH-REPORTER.md` limitation 3: the
recorder family's disk-writer threads (`src/core/audio/TrackRecorder.cpp`, `src/core/TakeRecorder.cpp`,
`sndfile` I/O rather than DSP), the diagnostics-only profiler writer, the Ableton-Link audio stream's
network thread (`src/core/LinkAudioStream.cpp`), transport (no long-running thread of its own), the
one-shot wavetable threads at `src/core/Oscillator.cpp:313-316` (created and joined at start-up), MIDI
(backend-owned `QThread`s, not a Zene launch lambda), the control-server pool (Windows accept/worker
threads) and GUI `QThreadPool` jobs (file work, not DSP), plus third-party plug-in threads (the documented
remaining limitation).

## Self-check

- **"Each new test FAILS with your product change reverted, then PASSES restored."** Product hunks
  reversed to the train content, `cmake --build build --target WorkerAltStackTest` EXIT 0
  (`build-2-revert.log`), `ctest -R ^WorkerAltStackTest$` **EXIT 8** with the two intended failures
  (`test-3-revert.log`); the gate on the same tree **EXIT 1** on all four sites (`test-2-revert-gate.log`).
  Restored files, rebuilt EXIT 0 (`build-3-restore.log`), gate **EXIT 0** (`test-4-restored-gate.log`),
  focused ctest 8/8 **EXIT 0** (`test-5-restored.log`).
- **"Every acceptance criterion is proven by a test you ran."** Criterion 1: `WorkerAltStackTest`
  (LV2 runtime) + `WorkerAltStackRegistration` (all four), exit 0. Criterion 2:
  `WorkerAltStackTest::forked_worker_overflow_reports_and_dies_by_signal`, exit 0. Both are named with
  logs in the table above.
- **"Each edit is ONE undo step, and every new or changed command has an honest reversibility class."**
  Not applicable: no command was added or changed and nothing edits the project, so there is no undo step
  and no reversibility class to declare. `git diff` touches only thread startup code, registration
  manifests, tests and docs.
- **"Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
  signal."** The added call runs once per worker thread at thread start, before the work loop
  (`Lv2Worker.cpp:89`, `RubberBandStretch.cpp:83`, `StemJobManager.cpp:90`, `WasmWorkerPool.cpp:73`); it
  is `sigaltstack()` on a `thread_local` fixed buffer (`CrashReporter.cpp:266`) and touches no model,
  queue, lock or Qt object. No audio-render function is modified. `submit_check.py` reports
  `PASS no allocation, lock, signal or I/O added inside audio-path functions`.
- **"Old projects still load and render exactly as before; new data round-trips."** No project data, save
  path or render path changed (no format change, no new field), so there is nothing new to round-trip.
  Neighbour render/DSP tests pass (`StemJobManagerTest`, `SampleClipRubberBandTest`, `Lv2StateTest`,
  `CrashReporterTest`, `CrashReporterArmTest`, `WasmWorkerPoolTest`, exit 0 in `test-5-restored.log`).
  Separately, `ControlGoldenAudio` fails on this box **identically with and without this change**
  (measured RMS -28.3698 dBFS vs recorded -11.0419 dBFS, plus a `std::bad_alloc` in one later render):
  `test-6-golden.log` (with the change) and `test-7-golden-base.log` (four hunks reverted), both EXIT 8.
  It is an environment/fixture failure, not a regression from this unit.
- **"Completeness: walk each acceptance criterion through the REAL user path and the agent path."** There
  is no user action and no agent command in this unit, so the "real user path" is the engine itself: two
  criteria are exercised through the shipping objects (`Lv2Worker` as constructed by the product, with its
  destructor/join path) and a real forked process, not through a reimplementation. Edge cases tried: the
  worker's *first* `work()` (not a later one), a zero-byte request (the worker skips it - the test
  therefore schedules a non-empty request, which is the real path), a real guard-page overflow (not
  `raise()`), the report's thread id vs pid, the reverted negative control, and all four launch sites
  after the file was reformatted. The reverted rebuilds prove the test is not passing for an unrelated
  reason.
- **"The GUI test drives the visible control..."** Not applicable: no user-facing action, no GUI, no
  command. `submit_check.py` reports the corresponding WARNs ("no GUI file changed", "no command file
  changed", "manual not updated"); the feature record states "no user-visible change" and does not invent
  UI, because the manual has no crash-report section.
- **"Qt 5.15 / MSVC / macOS < 10.15 portability."** After this round's fix: all POSIX includes and every
  POSIX call (`fork`, `waitpid`, `pause`, `usleep`, `sigaltstack`, `stack_t`, `SS_DISABLE`) sit under
  `#ifndef Q_OS_WIN` / `#if !defined(Q_OS_WIN)`, including the sigaltstack query and the overflow inside
  `fakeWork()`; `<atomic>` is unconditional. On Windows the file compiles a live-worker case that asserts
  the reporter's documented no-op. The ASan skip uses the project's existing `__SANITIZE_ADDRESS__` /
  `__has_feature` pattern and is compiled out on MSVC; `__attribute__((noinline))` is behind
  `#if defined(__GNUC__)` and only in the POSIX branch. The product change is four call sites of the
  cross-platform `installAlternateStackForCurrentThread()` (a documented no-op on Windows,
  `CrashReporterWindows.cpp:51`); no Qt-6-only API, no `std::filesystem`, and none of the identifiers
  `small`, `near`, `far`, `interface`, `min`, `max` were introduced. `WasmWorkerPoolTest` gains three
  source files only inside the existing `if(WANT_WASM)` block. The preprocessor model above reports 0
  POSIX tokens active for MSVC-like and mingw-like Windows environments.
- **Diagnostic checkpoint.** This harness gives the worker no subworker/provider channel (no packet call
  in the toolset), so the one-line liveness packet was not applicable; every exit code above comes from a
  command run in this session.
- **Gates.** `bash tests/fork-sources-gate.sh` EXIT 0 (`gate-fork-sources.log`); the manifest
  reproduces and every tracked source is registered. `bash tests/no-upstream-regression-gate.sh` EXIT 0
  (`gate-no-upstream.log`). `python3 tests/doc-figures-gate.py` EXIT 0 (`gate-doc-figures.log`, 933 ids /
  78 groups). `feature_check.py` EXIT 0 (`feature-check.log`, all 8 gates PASS). `submit_check.py` EXIT 0,
  `RESULT: PASS - submit` (`submit-check-1.log`).
- **Nothing weakened.** No existing test, assertion, gate or ratchet was changed: the Windows branch adds
  a real assertion (documented no-op) rather than a skip, the first slot still runs on every platform, and
  the second slot's Windows skip is the one accepted in round 2. `tests/CMakeLists.txt` only adds a test,
  a gate and three source files to one target; manifests gain entries, none is removed.
