FIXED

`cluster new-p14-325` — "A throwing separator can terminate the application"
(`src/core/StemJobManager.cpp:519`, historical line).

## Verdict and evidence

REAL. The finding is exact, and the line it names is the call it describes.

- `src/core/StemJobManager.cpp:484` (pre-fix numbering, commit `21c2f3ee2e`) —
  `workerLoop()` is the top-level function of the detached worker thread:
  `src/core/StemJobManager.cpp:91` ran `workerLoop(state);` directly as the
  `std::thread` lambda body, `.detach()`ed at `:92`.
- At that commit, `src/core/StemJobManager.cpp:518` inside `workerLoop()` was bare
  `runJob(state, job);` — no `try`/`catch` anywhere in `workerLoop()`, `runJob()`,
  `prepareJob()`, `loadMix()` or `provisionModel()`.
- `runJob()` reaches the two native boundaries unguarded:
  `src/core/StemJobManager.cpp:596` `provisioner(job->cancelRequested, …)` and
  `src/core/StemJobManager.cpp:637` `state->separator->separate(…)` (pre-fix numbering).
- The standard's `std::thread` contract makes an uncaught exception out of a thread's
  initial function call `std::terminate`, and detaching does not change that, so the
  escape is fatal for the process, not just for the job. Observed this session:
  `test-8-red-final.log` — `terminate called after throwing an instance of
  'std::runtime_error'`, `Subprocess aborted`, ctest exit 8.

So: a backend that throws on a corrupt model, an inference failure or an allocation
error ended the DAW instead of failing one job. That is the defect as reported.

## What changed (cause, not symptom)

`src/core/StemJobManager.cpp`, `include/StemSeparation/StemJobManager.h` (comment only).

1. `workerLoop()` catches per job, `src/core/StemJobManager.cpp:526-528`:

   ```
   try { runJob(state, job); }
   catch (const std::exception& thrown) { failJob(state, job, QString::fromUtf8(thrown.what())); }
   catch (...) { failJob(state, job, QStringLiteral("the stem-separation backend threw an exception")); }
   ```

   The throw ends *that* job; the loop keeps draining, so one corrupt model cannot
   stall the splits queued behind it. The error text is the exception's own `what()`
   for a `std::exception`, a message of the manager's own otherwise (a non-standard
   throw carries no text).
2. `failJob()` (`src/core/StemJobManager.cpp:541`) now does the two things a throw
   needs and nothing else:
   - it clears `fetchingModel`, because a throw out of the provisioner skips the
     `store(false)` that follows its call — without this a failed job kept reporting
     "downloading a model" forever (found by the new provisioner test, see below);
   - it publishes `Failed` **only when the job is not already terminal**
     (`Completed`, `Cancelled`, `Failed`), so a throw that arrives after `runJob()`'s
     status switch cannot publish a second end for the same job. Both existing
     callers (`loadMix`, `provisionModel`, and the no-backend branch of `runJob`)
     call it while the job is `Running`, so nothing they do changes.
3. The detached thread's entry has a last-resort
   `try { workerLoop(state); } catch (...) { }` (`src/core/StemJobManager.cpp:95`),
   covering a throw from the loop itself (a mutex or the wait) rather than from a job.

Registration: no new product file. The new test file is in `tests/fork-sources.txt`
and `tests/all-sources.txt`; `tests/CMakeLists.txt` is recorded in
`tests/upstream-modifications.txt` with its reason.

## Test matrix (procedure step 3)

| path | how it is covered |
| --- | --- |
| normal path | the existing 24 `StemJobManagerTest` cases still pass (`test-4-green-verbose.log`, `test-10-…`); the new suite's second job in the drain case completes normally |
| failure path | `testAThrowingBackendFailsTheJobInsteadOfTheProcess` (backend throws), `testAThrowingProvisionerFailsTheJob` (provisioner throws), `testANonStandardThrowStillFailsTheJobWithAMessage` (`throw 42`) |
| exhaustion / queue | `testTheWorkerKeepsDrainingAfterAThrowingJob`: job 1 throws, job 2 completes, `jobFailed` once, `runningJobCount() == 0` |
| reconfiguration | `setSeparator` + `setModelProvisioner` are re-driven per manager in every new case; no new state survives a throw (`fetchingModel` cleared, `mix` reset by the loop) |
| audio-thread constraints | nothing added on the audio thread: the whole change is on the manager's worker thread, `failJob()` allocates only a `QString` copy the caller already made, and no new lock is taken beyond the job mutex `failJob()` already used |
| platform build | plain C++20 `try`/`catch` and `std::exception`, no platform-specific code; builds with `-j4` on this host (logs below). No timing, sleep or clock is used by any new assertion — synchronisation is the existing `jobFailed`/`jobFinished` Qt signals, waited on with `QTRY_VERIFY_WITH_TIMEOUT` |

Why a separate test target: `tests/src/core/StemJobManagerTest.cpp` was at **exactly
500 code lines** at HEAD (the file-length ratchet's limit, `tests/file-length-gate.sh`),
so the four new cases cannot live there — `feature_check-1.log` shows those additions
failing the ratchet at 569 code lines, and the same file records `StemJobManagerTest.cpp`
being restored to HEAD. The new cases therefore have their own target,
`tests/src/core/StemJobManagerThrowTest.cpp`, registered the same way as the other
single-suite regressions in `tests/CMakeLists.txt`. The same constraint shaped the fix:
`src/core/StemJobManager.cpp` was 494 code lines at HEAD. A first version with a separate
`failThrownJob()` helper and a multi-line catch was 537 (the `feature_check-1.log` FAIL);
the shipped version adds five code lines and is 499, so it adds no new function and
routes the throw through the existing `failJob()`.

## Commands and EXIT codes (copied from the logs)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-325.r7/logs`.

RED — product change reversed (procedure step 4b):

```
git diff -- src/core/StemJobManager.cpp include/StemSeparation/StemJobManager.h > LOG/fix.patch   EXIT=0
git apply -R LOG/fix.patch                                                                        EXIT=0
cmake --build build --target StemJobManagerThrowTest -j4        > LOG/build-9-red-final.log       EXIT=0
cd build/tests && ctest -R "^StemJobManagerThrowTest$" --output-on-failure > LOG/test-8-red-final.log  EXIT=8
```

`test-8-red-final.log`: `1/1 Test #664: StemJobManagerThrowTest ... Subprocess aborted***Exception`,
`terminate called after throwing an instance of 'std::runtime_error'`.

GREEN — product change re-applied:

```
git apply LOG/fix.patch                                                                           EXIT=0
cmake --build build --target StemJobManagerThrowTest StemJobManagerTest zene -j4 > LOG/build-11-green-final.log  EXIT=0
cd build/tests && ctest -R "StemJobManagerThrowTest|StemJobManagerTest|StemJobLoaderTest|StemSplitPipelineTest|StemQualityTest|StemModelStoreTest" --output-on-failure > LOG/test-9-green-final.log  EXIT=0
cd build/tests && ctest -V -R "^StemJobManagerThrowTest$" > LOG/test-10-green-verbose.log          EXIT=0
```

`test-9-green-final.log`: `100% tests passed out of 6`. `test-10-green-verbose.log`:
`PASS` for `testAThrowingBackendFailsTheJobInsteadOfTheProcess`,
`testAThrowingProvisionerFailsTheJob`, `testTheWorkerKeepsDrainingAfterAThrowingJob`,
`testANonStandardThrowStillFailsTheJobWithAMessage`; `Totals: 6 passed, 0 failed`.

Earlier runs of the same probes, kept for the record:

```
cmake --build build --target StemJobManagerTest -j4             > LOG/build-1-red.log    EXIT=0
cd build/tests && ctest -R StemJobManagerTest --output-on-failure > LOG/test-1-red.log    EXIT=8   (same terminate)
git checkout -- tests/src/core/StemJobManagerTest.cpp                                     EXIT=0   (ratchet: 500 -> 569 code lines)
cd build/tests && ctest -R "StemJobManagerTest|…" --output-on-failure > LOG/test-2-green.log  EXIT=8   (new provisioner case caught the stuck fetchingModel)
cmake --build build --target StemJobManagerTest StemModelStoreTest StemQualityTest -j4 > LOG/build-3-green.log  EXIT=0
cd build/tests && ctest -R "StemJobManagerTest|StemJobLoaderTest|StemSplitPipelineTest|StemQualityTest|StemModelStoreTest" --output-on-failure > LOG/test-3-green.log  EXIT=0
cmake --build build --target StemSplitMenuTest ControlStemWavTest -j4 > LOG/build-6-dependents.log  EXIT=0
cd build/tests && ctest -R "StemSplitMenuTest|ControlStemWavTest" --output-on-failure > LOG/test-7-dependents.log  EXIT=0
```

Gates:

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOG/feature_check-1.log   EXIT=1  (file-length: 537 / 569, before the split)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOG/feature_check-2.log   EXIT=1  (fork-sources + all-sources: the new test file was still untracked, so the header recipe did not derive it)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOG/feature_check-3.log   EXIT=0  RESULT: PASS, all eight rows PASS (complexity and file-length: "no new item in your files")
```

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  Yes, both runs this session: `test-8-red-final.log` EXIT=8 (`std::terminate`, the
  process aborts), `test-9-green-final.log` EXIT=0 (6/6) and
  `test-10-green-verbose.log` EXIT=0 (`Totals: 6 passed`). The only difference between
  the two runs is `fix.patch`, applied with `git apply -R` and then `git apply`
  (`EXIT=0` each time).
- **The test is deterministic — no wall-clock timing, sleeps or races.** The new cases
  use no sleep, no clock and no fixed delay. Synchronisation is the manager's own
  signals: `QSignalSpy` on `jobFailed`/`jobFinished` with
  `QTRY_VERIFY_WITH_TIMEOUT(…, 10000)`, which polls the spy until the queued signal is
  delivered. The values asserted are injected: the exception text (`"fake throw"`,
  `"provisioner exploded"`), the job ids returned by `submit()`, and the states read
  through the public getters. The queue-drain case has no ordering assumption: whether
  the second `submit()` lands before or after the first job ends, both jobs must reach
  their terminal state, and the test fails if either does not.
- **The fix is at the cause and complete; every caller and sibling checked.**
  The cause is that an exception escapes the worker thread's entry function, so the
  catch is at the two boundaries the finding names plus the thread entry:
  `runJob()` (which contains both the provisioner and `separate()` calls) per job in
  `workerLoop()` (`:526`), and the detached lambda (`:95`). Callers of `failJob()` —
  `loadMix()` (`:588`), `provisionModel()` (`:625`) and the no-backend branch of
  `runJob()` (`:640`) — all call it with the job `Running`, so the new terminal-state
  check never changes what they do; they were re-read and the whole existing suite
  passes unchanged (24 cases). The sibling terminal paths (`Status::Success`,
  `Status::Cancelled`, the `Status::Failed` branch) are untouched. `waitFor`/`cancel`
  paths untouched: a cancel that lands during a throw is reported as `Failed` exactly
  as a backend returning `Status::Failed` after a cancel request already is.
- **No collateral damage.** No save/load, project, undo or command code is touched, so
  old projects and saved data load exactly as before; no limit or check was added that
  other callers must respect. The one behaviour that changes outside the throw path is
  `failJob()` clearing `fetchingModel`, which is already false on every existing call
  path (`provisionModel()` stores false immediately after the provisioner returns, and
  the other callers never set it), so no existing caller observes it. Proof the rest is
  intact: `StemJobManagerTest` 24/24, `StemJobLoaderTest`, `StemModelStoreTest`,
  `StemQualityTest`, `StemSplitPipelineTest`, `StemSplitMenuTest`, `ControlStemWavTest`
  all pass, and `zene` links (`build-11-green-final.log` EXIT=0).
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED,
  and the pre-fix code is quoted above at the line numbers the finding points to
  (`src/core/StemJobManager.cpp:518` at pre-fix commit `21c2f3ee2e`, inside
  `workerLoop()` at `:484`, whose thread entry is `.detach()`ed at `:92`).
- **Sibling commands in the command files changed.** No command file was changed:
  this diff touches `src/core/StemJobManager.cpp`, the comment block of
  `include/StemSeparation/StemJobManager.h`, the new test file, `tests/CMakeLists.txt`
  and the two scope manifests. `ControlCommands*.cpp` and every handler behind
  `app.about`, `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`,
  `app.tour`, `app.tours`, `app.update_check_get`, `app.update_check_run`,
  `app.update_check_set`, `app.version`, `app.whats_new`, `arrangement.fit_tracks` and
  `arrangement.get_state` are untouched, so no `ControlCommandsSnapshot` canary moves
  and no sibling command's behaviour changes.

## Other findings

- `tests/src/core/StemJobManagerTest.cpp` sits at exactly 500 code lines, the
  file-length ratchet's limit, so every later regression for this area has to open a new
  target. Not fixed here (it is not this bug), but it is why cluster new-p14-325 needed
  its own test file.
- The runner-side suggestion from the hand-back — a post-session guard that treats zero
  edit calls or an empty diff as a model no-op — is tooling outside this worktree's
  scope, so it is not part of this commit.
