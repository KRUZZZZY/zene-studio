ALREADY-FIXED

# Cluster 2945 - `stem.job_start` decoded its source on the control thread

## Verdict

The finding describes the code before Q050-010. On this branch the source is NOT decoded in `stem.job_start`/`stemStartJob` on the application thread: only its header is probed (`sf_open` + `sf_close`, no frame read), then the job is queued through `StemJobManager::submitFrom` with a loader that only the manager's worker thread calls. The ordering is not "reply first, then decode": `StemJobManager::enqueue` calls `m_state->jobAvailable.wakeOne()` at `src/core/StemJobManager.cpp:217` and returns the id at `:218`, so the worker may enter the loader before the reply is written. What the finding is about - the decode blocking the control surface - is absent: the decode is never run on the handler's thread. The fix for this exact defect is commit `59060ceea5` ("run ... stem decoding off the interface thread ... (Q050-010)"), plus the tracked half `52d7334966`, and `git merge-base --is-ancestor 59060ceea5 HEAD` exits 0 in this worktree. No product code changed in this job.

Base: `fa4a5f2e7`. Branch: `bugs/qwen-2945`.

## Round 2 correction

The round-1 report said the job id and job object "are returned before that loader runs" and called the covering test exactly "return the job id before decoding begins". That ordering is not guaranteed and was wrong: `enqueue` wakes the worker before returning the id (`src/core/StemJobManager.cpp:217-218`), so the worker can start the loader first. The defect is the thread, not the ordering - the decode never runs on the application/control thread. The covering test's latch, bounded sleep loop and `QTRY` timeouts are now described as they are, not as timing-free.

## Evidence (file:line)

| Claim | Where |
| --- | --- |
| The command handler does no decoding; it validates and delegates | `src/core/ControlCommandsStems.cpp:119-152`, call at `:143` |
| The file's own threading contract: only the header is read | `src/core/ControlCommandsStems.cpp:116-117` ("Only the header is read here (Q050-010); decoding, resampling and the separation all run on the manager's worker") |
| `start()` reads only the source's length, then queues a loader | `src/core/ControlStemSupport.cpp:200`, header read at `:223-226`, `submitFrom` at `:234-236` |
| The header probe opens and closes, it never decodes frames | `src/core/ControlStemSource.cpp:64-74` (`probeHeader`), `:87-108` (`stemSourceLength`) |
| Full decode and resample exist only as a worker loader | `src/core/ControlStemSource.cpp:115-131` (`stemDecodeAtModelRate`), used only by the lambda in `ControlStemSupport.cpp:234-236` |
| `submitFrom` stores the loader and queues the job; `enqueue` wakes the worker and then returns the id | `src/core/StemJobManager.cpp:185-204`, `enqueue` at `:206-219` (`wakeOne()` at `:217`, `return job->id` at `:218`) |
| The worker can start the loader before `start()` builds its reply | the wake at `StemJobManager.cpp:217` precedes the return at `:218`; `workerLoop` at `:484`, `runJob`/`prepareJob`/`loadMix` at `:518`, `:544`, `:554-562` |
| The loader is invoked from the worker loop | `src/core/StemJobManager.cpp:484` (`workerLoop`), `:518` (`runJob`), `:554-562` (`loadMix` calls `loader(...)`), worker created at `:88-92` |
| The old code truly was the bug: it decoded inline | `git show 52d7334966^:src/core/ControlStemSupport.cpp` line 230 `auto mix = SampleBuffer::fromFile(source);` inside `start()`, submitted at `:253`; `git show 52d7334966^:src/core/ControlCommandsStems.cpp` line 116 "decoded here, on the application thread" |
| The fix is in this branch's history | `59060ceea5` adds `src/core/ControlStemSource.cpp` with the header-only probe and the worker loader; `52d7334966` wires `start()` to it; `git merge-base --is-ancestor 59060ceea5 HEAD` EXIT 0 |

Reproduction is impossible on this branch by construction: the only `SampleBuffer::fromFile` in the stem path is inside `stemDecodeAtModelRate`, whose single caller is the loader passed to `submitFrom` and invoked from `loadMix` on the worker thread. `rg -n "fromFile|resampleBuffer" src/core/Control*.cpp` shows the stem path's only hit is `ControlStemSource.cpp:117`; the other hit is `ControlCommandsSessionRecordLand.cpp:174`, a different command (see Other findings).

## Covering test already in the tree

`tests/src/core/StemJobLoaderTest.cpp:77` `testTheLoaderRunsOnTheWorkerAfterSubmitReturned` is the Q050-010 regression test. It submits a loader that records `std::this_thread::get_id()` (`:92-94`), asserts the loader ran off the caller's thread (`:100`) while the caller's 1 ms `QTimer` kept ticking (`:101`) and the job had not finished (`:102`), then releases the latch and asserts completion (`:103-106`). It proves the decode runs off the caller's thread and the caller's event loop keeps turning. It does NOT prove an ordering between the worker starting the loader and the command reply: `enqueue` wakes the worker first (`StemJobManager.cpp:217-218`), so the loader may already be running when `submitFrom` returns. It is also not timing-free: the loader spins in a bounded `std::this_thread::sleep_for(2 ms)` loop (`:95`), the test waits with `QTRY_VERIFY_WITH_TIMEOUT` (5000/10000 ms at `:99`, `:101`, `:104`), and it requires the 1 ms timer to reach 20 ticks (`:101`). Those are waits for conditions, not a measurement of decode duration, and they belong to the pre-existing Q050-010 test, which this job does not touch. It passes in this worktree:

```text
$ cd build/tests && ctest -R 'StemJobLoaderTest|StemSplitPipelineTest' --output-on-failure
1/2 Test #664: StemJobLoaderTest ................   Passed    0.54 sec
2/2 Test #667: StemSplitPipelineTest ............   Passed    1.43 sec
100% tests passed out of 2
EXIT=0
```

No new test is added: per the procedure, an ALREADY-FIXED verdict goes to the report with the code evidence, and a RED run would require reintroducing the defect to a product file this job must not change. The claim is verified instead against the code and the pre-fix commit above.

## Commands and results

All build output and test output went to the round-2 log folder. Builds used `-j4`.

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target StemJobLoaderTest StemSplitPipelineTest -j4` | 0 | `build-1-stem-tests.log` |
| 2 | `cd build/tests && ctest -R 'StemJobLoaderTest\|StemSplitPipelineTest' --output-on-failure` | 0 | `test-1-stem-family.log` (2/2 passed) |
| 3 | `git merge-base --is-ancestor 59060ceea5 HEAD` (and the pre-fix `git show` above) | 0 | `git-1-history-evidence.txt` |
| 4 | `git diff 1165a38f36 HEAD --stat -- tests/src/core/StemJobLoaderTest.cpp` and `git show 1165a38f36:tests/src/core/StemJobLoaderTest.cpp \| grep -n ...` | 0 | `git-2-test-unchanged.txt` (empty diff; `QVERIFY(id > 0)` and `QVERIFY(ranOffTheCaller.load())` still present) |
| 5 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-2945` | 0 | `log-1-feature-check.txt` (`RESULT: PASS`, all 8 gates) |

Gate table from `log-1-feature-check.txt`:

```text
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** No product change was made, so there is no reverted run to make: the verdict is ALREADY-FIXED and the defect is absent from this branch. The covering Q050-010 test `StemJobLoaderTest::testTheLoaderRunsOnTheWorkerAfterSubmitReturned` passes (EXIT 0, `test-1-stem-family.log`), and the pre-fix commit `52d7334966^` is quoted above showing the decode that it pins. The test file is untouched by this job: `git diff 1165a38f36 HEAD -- tests/src/core/StemJobLoaderTest.cpp` is empty and both `QVERIFY(id > 0)` and `QVERIFY(ranOffTheCaller.load())` are still in the committed file (`git-2-test-unchanged.txt`, EXIT 0). The round-1 reviewer fix that had removed an assertion from this test was dropped; nothing of it is in the tree, and this job removed no assertion.
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).** No new test is added in this job (ALREADY-FIXED, no product change), so no new timing is introduced. The pre-existing Q050-010 covering test is not timing-free and I do not claim it is: its loader spins in a bounded `std::this_thread::sleep_for(2 ms)` loop (`StemJobLoaderTest.cpp:95`), the waits are `QTRY_VERIFY_WITH_TIMEOUT` of 5000/10000 ms (`:99`, `:101`, `:104`), and it requires the 1 ms `QTimer` to reach 20 ticks (`:101`). Its deterministic part is the seam: the loader compares `std::this_thread::get_id()` with the caller's (`:84`, `:93`, `:100`), and the waits are for conditions rather than a measured decode duration. The verdict itself rests on the code paths, not on those waits.
- **The fix is at the cause and complete: every caller and sibling was checked.** `stem.job_start` is the only caller of `control::stemStartJob` (`src/core/ControlCommandsStems.cpp:143`), which is a one-line delegate to `surface()->start` (`src/core/ControlStemSupport.cpp:582-590`). The GUI's "Split to stems" path goes through the same registered command (`src/gui/StemSplitController.cpp` calls `stem.job_start`), so both paths share the fixed `start()`. `stem.job_status`, `stem.job_result`, `stem.job_cancel` and `stem.tracks_create` read the manager's state and never decode a source. `stem.model_download` is the declared exception (network fetch on its own path) and is outside this finding. The `.ds`/`.ogg` header-less case is also routed to the worker: `stemSourceLength` sets `frames = 0` for those and `jobObject` fills the length from `m_manager.loadedFrames()` after the worker decoded (`ControlStemSupport.cpp:454-456`).
- **No collateral damage.** No project, save, undo or data format code changed; the diff for this job is the report only. Nothing new runs on the audio thread. No limit or check changed. The existing stem tests pass unchanged (`test-1-stem-family.log`, EXIT 0).
- **If your verdict is ALREADY-FIXED: the code (file:line) or contract that shows it.** `src/core/ControlStemSupport.cpp:223-236` (header length, then `submitFrom` with a loader) and `src/core/StemJobManager.cpp:554-562` (the loader is called from `loadMix` on the worker). `enqueue`'s wake at `:217` before the `return` at `:218` means the worker may start the loader before the reply, never that the handler thread decodes; the handler's only file work is the header probe. The fix commit `59060ceea5` is an ancestor of HEAD (`EXIT=0` in `git-1-history-evidence.txt`).
- **Sibling commands in the command files the diff touched** (`app.about`, `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`, `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`, `app.whats_new`, `arrangement.fit_tracks`, `arrangement.get_state`): that list is computed from the whole-branch diff against the fix base, which includes the train merges this worktree already carries, not from this job - `git show --stat --oneline c38ee49cfe` shows this job's commit is the report only (`EXIT=0`, `git-3-own-commit.txt`). None of those verbs decodes audio: `app.*` is version/help/tour/update/task bookkeeping, and `arrangement.fit_tracks`/`get_state` edit or report arrangement geometry. The stem group's own siblings were checked in the item above. `stem.job_start` is the only command that names a source file for decoding, and its group mates (`get_state`, `job_status`, `job_result`, `job_cancel`, `tracks_create`, `model_download`) neither decode the source nor call `control::stemStartJob`.

## Other findings

- `include/ControlStemSupport.h:36` still says "`stem.job_start` decodes the source, submits and returns the id". That sentence predates Q050-010 and is ambiguous next to the correct implementation; the authoritative statement is `src/core/ControlCommandsStems.cpp:116-117`. Not changed here because this is an ALREADY-FIXED verdict and the finding is about behaviour, not comment wording.
- `src/core/ControlCommandsSessionRecordLand.cpp:174` calls `SampleBuffer::fromFile` in a different command group (session recording). Outside this finding; not changed.
