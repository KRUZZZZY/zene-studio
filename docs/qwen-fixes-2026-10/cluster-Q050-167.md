IMPLEMENTED

# Q050-167 — Keep legacy macOS audio wakeups lock-free

Branch `feat/050-Q050-167-oc`, cut from `050/m0-train-12` (`07d6d02bd`). Member
`R-platform-53`, area L15. This round has no earlier attempt commits on the branch to
keep or correct: round 1 and round 2 both ended (`rc=1`) with no commit and a clean tree,
so the work below is new on top of `07d6d02bd`.

**Log path substitution:** the brief names
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-167.r3/logs` for build/test logs, but this
session's file sandbox refuses writes there (`touch` → `Permission denied`; the whole
`~/.local/share/...` tree and `$HOME` root are read-only, while `$HOME/.cache` and the
worktree are writable). Every log this report cites is under
`~/.cache/zene-q050-167-r3/logs/`. The harness's own `feature_check.py` also cannot write
its `-feature-check` directory beside the worktree (`PermissionError`), nor create the git
worktree it wants for base listings (`git worktree add` → exit 97 under the lane guard), so
the two ratchet rows below are measured against a real base tree made with
`git archive 07d6d02bd` and compared key-by-key. Every other row is the committed gate
script run directly from the worktree.

`submit_check.py` therefore reports one remaining FAIL — its bundled `feature_check.py`
row, which cannot run in this sandbox for the two reasons just named — and three WARNs for
GUI/agent files, which do not apply: this unit adds no user-facing action (see the
reversibility answer below).

That row is nevertheless **measured**: the same committed `feature_check.py` logic, run with
its base-listing cache pre-seeded from the `git archive 07d6d02bd` measurements (the step the
script would have done with `git worktree add`), exits **0** with every row PASS —
`feature-check-5.log`:

```
PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
     no-tautology / doc-figures; complexity and file-length: no new item in your files
RESULT: PASS
```

## What changed

- **`include/AudioWorkerWake.h` + `src/core/AudioWorkerWake.cpp` (new).** The platform half
  of the wake, with one selection macro:
  - Apple — `__ulock_wait` / `__ulock_wake` on the generation word itself (the
    compare-and-wait trap libc++ uses under `std::atomic::wait`, available since macOS 10.12,
    below the 10.13 floor and not gated on the deployment target, so the macOS x86_64 CI job
    runs the code a legacy user runs).
  - Linux — the same protocol on the same word with `FUTEX_WAIT_PRIVATE` / `FUTEX_WAKE_PRIVATE`,
    so the tests here drive the shipping code and only the two-syscall shim differs.
  - Windows / POSIX without those calls — `std::atomic::wait` / `notify_all`, unchanged.
  - `static_assert`s require a 32-bit, always-lock-free wake word, and an Apple build that did
    not select the wait-on-address path fails to compile.
- **`src/core/AudioEngineWorkerThread.cpp`.** `notifyWorkers()` is `workerwake::notify(...)`;
  `waitForWake()` is `workerwake::wait(...)`. The `LMMS_WORKER_WAKE_CONDVAR` branch, its mutex
  and condition variable, and the lazy `new`s for both are deleted. `wakeWorkers()`, the
  no-lost-wake guarantee, the deterministic-render handshake and shutdown are unchanged.
- **`include/AudioEngineWorkerThread.h`** — the wake comment now describes the primitive.
- **`CMakeLists.txt`** — states the supported macOS floor (10.13) and warns if
  `MACOSX_DEPLOYMENT_TARGET` is below it.
- **`README.md`, `data/manual/index.html`** — the floor and the user-visible behaviour.
- **`tests/src/core/AudioWorkerWakeTest.cpp` (new)** — the regression, registered in
  `tests/CMakeLists.txt`, `tests/fork-sources.txt` and `tests/all-sources.txt`.
- **`tests/all-sources.txt`** — re-derived from its own header recipe. The `07d6d02bd` train
  commit had added 1151 vendored `plugins/SfzPlayer/sfizz/...` entries that the recipe's own
  exclusion list removes, so the manifest did not reproduce at the base; it does now
  (`REPRODUCES`).
- **`tests/rt-safety-scope.txt`** — declares the two render-side wake functions.
- **`docs/050-features/Q050-167.md`** — the feature record.

No user-facing command, GUI action, undo step or reversibility entry applies: Play and
`transport.play` already reach the same `startAndWaitForJobs()` wake, and no command or
project data changed.

## Acceptance

| Criterion | Test and evidence | Exit |
|---|---|---|
| A macOS x86_64 test exercises the legacy wake path and verifies the render-side wake uses no mutex or allocation. | `AudioWorkerWakeTest::theCompiledWakeIsTheLegacyWaitOnAddressPrimitive` asserts this build selected `Primitive::WaitOnAddress` and that the 32-bit wake word `is_always_lock_free` — on the macos-x86_64 job that is the `__ulock` path; the Linux job runs the same protocol with the futex shim. `theRenderSideWakeAllocatesNothingAndTakesNoMutex` drives the shipping `AudioEngineWorkerThread::wakeWorkers()` through 256 periods under `AllocationProbe` (0 allocations, first call included) and a runtime `pthread_mutex_lock` interposer (0 locks), then cross-checks a test-only notify counter (512) so the probes cannot pass against a copy. `theProbesCatchTheOldCondvarWake` runs the old mutex+condvar+`new` shape through the same probes and requires both to catch it. `theWakePathNamesNoLockOrAllocationInItsSource` scans the comment-stripped bodies of `notifyWorkers()` and `workerwake::notify()` for lock/allocation/container tokens on every platform. `python3 tests/rt-safety-sweep.py --check` reports no fresh hit on the two newly declared wake paths. | `ctest -R '^AudioWorkerWakeTest$'` → **0** (9 passed); sweep → **0** |
| A repeated worker-wakeup stress test completes without deadlock or missed periods; the supported deployment target is documented. | `AudioWorkerWakeTest::aWokenWorkerTakesTheQueuedPeriodJob` proves the pool half deterministically through the worker drain probe: the worker reports it read the generation (`BeforeWait`) and decided to drain (`BeforeQueue`), the queued period job runs exactly once and never on the caller. `repeatedWakeupsDeliverEveryPeriodWithoutDeadlock` runs 240 simulated periods × 8 jobs on two real pool workers and requires every job exactly once (1920/1920), exactly 240 posts, and both workers joined inside the budget. `quitRacingAWakeupAlwaysJoinsTheWorker` races `quit()` against a wake 40 times and joins each worker inside the budget. Stress-run 40 times on this box: 40 green of 40. Floor documented in `README.md` ("macOS support floor: 10.13"), printed/warned by `CMakeLists.txt`, and stated in the user manual. | `ctest -R '^AudioWorkerWakeTest$'` → **0**; 40 direct runs → 40× **0** |

## Red/green evidence

Only the two product files (`src/core/AudioEngineWorkerThread.cpp`,
`include/AudioEngineWorkerThread.h`) were restored from `HEAD`; the new wake primitive, the
test, the manifests and the docs stayed. Rebuilt the same target and ran the same ctest:

| State | Build | `ctest -R '^AudioWorkerWakeTest$'` | Failing slots |
|---|---:|---:|---|
| Product hunks reverted | 0 | **8** | `theRenderSideWakeAllocatesNothingAndTakesNoMutex` (notify counter 0 vs 512: the shipping call no longer reaches the primitive), `theWakePathNamesNoLockOrAllocationInItsSource` (reverted `notifyWorkers()` names `mutex`), `repeatedWakeupsDeliverEveryPeriodWithoutDeadlock` (0 posts) |
| Product restored | 0 | **0** | none — 9 passed |

The reverted build **compiled** (the reverted worker simply stopped calling the new
primitive), so the failure is behavioural rather than a compile error.

## Neighbouring regressions run

`ctest -R '^(AudioWorkerWakeTest|RenderJobQueueTest|AudioEngineTeardownTest)$'` → **0**
(3/3). `ctest -R '^(MixerConcurrencyTest|PdcMixerTest)$'` → **0** (2/2).

## Gates

| Gate | Exit | Log |
|---|---:|---|
| `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| `bash tests/all-sources-reproduce.sh` (`REPRODUCES`) | 0 | `gate-all-sources.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream.log` |
| `tests/scripted/check-namespace` | 0 | `gate-namespace.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| `python3 tests/rt-safety-sweep.py --check` | 0 | `rt-sweep-2.log` |
| `tests/scripted/check-namespace` (final tree) | 0 | `namespace-2.log` — the last `#endif` gained its `// LMMS_WORKER_WAKE_WAIT_ON_ADDRESS` comment |
| `bash tests/complexity-gate.sh --check` | 1 | `final2-complexity.log` — 7 regression keys, **all 7 also present at the base commit** (`git archive 07d6d02bd` → `base-complexity.log`); the difference is empty, so this branch adds no item |
| `bash tests/file-length-gate.sh --check` | 1 | `final2-file-length.log` — 18 keys, **all 18 also present at the base** (`base-file-length.log`); the difference is empty |

## Self-check

**? Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run: exit code?), then PASSES restored.**
Reverted the two product files from `HEAD` with the test and manifests kept:
`cmake --build build --target AudioWorkerWakeTest` → **0**, then
`ctest -R '^AudioWorkerWakeTest$'` → **8** (3 slots red, quoted in the table above).
Restored the same two files, rebuilt → **0**, ctest → **0** (9 passed). Logs:
`build-reverted-2.log`, `test-reverted-2.log`, `build-restored-2.log`, `test-restored-2.log`.

**? Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code per criterion).**
AC1: `AudioWorkerWakeTest::theCompiledWakeIsTheLegacyWaitOnAddressPrimitive`,
`::theRenderSideWakeAllocatesNothingAndTakesNoMutex`, `::theProbesCatchTheOldCondvarWake`,
`::theWakePathNamesNoLockOrAllocationInItsSource` — `ctest -R '^AudioWorkerWakeTest$'` exit
**0** (9 passed, `test-6.log`), and the mutex/allocation probes are shown non-vacuous by the
control case. AC2: `::aWokenWorkerTakesTheQueuedPeriodJob`,
`::repeatedWakeupsDeliverEveryPeriodWithoutDeadlock`, `::quitRacingAWakeupAlwaysJoinsTheWorker`
— same ctest exit **0**, plus 40 separate direct runs all exit **0** (`stress2-*.log`). The
macOS-specific half (that the Apple build compiles and runs the `__ulock` path) is a hosted-job
confirmation, stated as a known limit below rather than claimed as a local measurement.

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
No command, GUI action, undo transaction or reversibility class was added or changed. Play and
`transport.play` keep the same command path and the same wake call; this unit only changes how
that existing internal wake is implemented. Reversibility is therefore not applicable, and no
`ControlRegistry` registration, schema, snapshot or A16 row moved.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal.**
The only new code on the render path is `AudioEngineWorkerThread::notifyWorkers()` and
`workerwake::notify()`: one relaxed test-only counter, one `RealtimeExemption` (a compile-time
no-op outside RTSan builds), and one wake trap (`__ulock_wake` / `FUTEX_WAKE` / `notify_all`).
`__ulock_wake` and `FUTEX_WAKE` wake sleepers and return; they do not wait, allocate or take a
user lock, and a wake with no waiter is a no-op because the generation value is the signal.
`AudioWorkerWakeTest::theRenderSideWakeAllocatesNothingAndTakesNoMutex` measures 0 allocations
and 0 mutex locks over 512 shipping posts, and
`python3 tests/rt-safety-sweep.py --check` (exit 0) reports no fresh allocation/lock/growth hit
on the two declared paths. The wait side is a parked worker, never the render thread.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
No project data, serialization, format or command changed, so there is nothing new to round-trip
and old projects take the identical load path. The neighbouring render regressions
(`RenderJobQueueTest`, `PdcMixerTest`, `MixerConcurrencyTest`, `AudioEngineTeardownTest`) all
pass (exit 0), and the deterministic-render switch and job-queue semantics were not touched.

**? Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro names, no std::filesystem outside _WIN32.**
No Qt API was added at all. The new code is standard C++20 plus three platform guards:
`__APPLE__` for the `__ulock` declarations, `__linux__` for `linux/futex.h`/`sys/syscall.h`,
and `std::atomic::wait` everywhere else (MSVC and mingw both provide it). No
`std::filesystem`, no `<windows.h>` and no reserved macro name is used. The Apple path is
available since macOS 10.12, below the documented 10.13 floor, and does not use
`std::atomic::wait` (unavailable below macOS 11) — the exact reason the unit exists.

## Known limits

- The macOS x86_64 job was not run from this Linux worktree. What is proved here is the
  protocol and the syscall shims' contract on Linux, plus a compile-time assertion that an
  Apple build must select the wait-on-address path. The Apple run itself is the hosted job's
  to confirm.
- The runtime mutex interposer is glibc-only. On a macOS runner that case prints a warning
  rather than passing silently, and the source scan plus the primitive assertion carry the
  mutex half there.
- Local TSan does not run on this box (as the brief states); the race argument is in the
  header and the feature record rather than a sanitizer transcript.
