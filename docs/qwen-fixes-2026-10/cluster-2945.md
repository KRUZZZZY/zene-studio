ALREADY-FIXED

# Cluster 2945 - `stem.job_start` decoded its source on the control thread

## Verdict

The finding describes the code before Q050-010. On this branch the source is NOT decoded in `stem.job_start`/`stemStartJob`: only its header is probed (`sf_open` + `sf_close`, no frame read), then the job is queued through `StemJobManager::submitFrom` with a loader that the manager's own worker thread calls. The job id and the queued job object are returned before that loader runs. The fix for this exact defect is commit `59060ceea5` ("run ... stem decoding off the interface thread ... (Q050-010)"), plus the tracked half `52d7334966`, and `git merge-base --is-ancestor 59060ceea5 HEAD` exits 0 in this worktree. No product code changed in this job.

Base: `fa4a5f2e7`. Branch: `bugs/qwen-2945`.

## Evidence (file:line)

| Claim | Where |
| --- | --- |
| The command handler does no decoding; it validates and delegates | `src/core/ControlCommandsStems.cpp:119-152`, call at `:143` |
| The file's own threading contract: only the header is read | `src/core/ControlCommandsStems.cpp:116-117` ("Only the header is read here (Q050-010); decoding, resampling and the separation all run on the manager's worker") |
| `start()` reads only the source's length, then queues a loader | `src/core/ControlStemSupport.cpp:200`, header read at `:223-226`, `submitFrom` at `:234-236` |
| The header probe opens and closes, it never decodes frames | `src/core/ControlStemSource.cpp:64-74` (`probeHeader`), `:87-108` (`stemSourceLength`) |
| Full decode and resample exist only as a worker loader | `src/core/ControlStemSource.cpp:115-131` (`stemDecodeAtModelRate`), used only by the lambda in `ControlStemSupport.cpp:234-236` |
| `submitFrom` stores the loader and returns the id immediately | `src/core/StemJobManager.cpp:185-204`, `enqueue` at `:208-222` |
| The loader is invoked from the worker loop | `src/core/StemJobManager.cpp:484` (`workerLoop`), `:518` (`runJob`), `:554-562` (`loadMix` calls `loader(...)`), worker created at `:88-92` |
| The old code truly was the bug: it decoded inline | `git show 52d7334966^:src/core/ControlStemSupport.cpp` line 230 `auto mix = SampleBuffer::fromFile(source);` inside `start()`, submitted at `:253`; `git show 52d7334966^:src/core/ControlCommandsStems.cpp` line 116 "decoded here, on the application thread" |
| The fix is in this branch's history | `59060ceea5` adds `src/core/ControlStemSource.cpp` with the header-only probe and the worker loader; `52d7334966` wires `start()` to it; `git merge-base --is-ancestor 59060ceea5 HEAD` EXIT 0 |

Reproduction is impossible on this branch by construction: the only `SampleBuffer::fromFile` in the stem path is inside `stemDecodeAtModelRate`, whose single caller is the loader passed to `submitFrom` and invoked from `loadMix` on the worker thread. `rg -n "fromFile|resampleBuffer" src/core/Control*.cpp` shows the stem path's only hit is `ControlStemSource.cpp:117`; the other hit is `ControlCommandsSessionRecordLand.cpp:174`, a different command (see Other findings).

## Covering test already in the tree

`tests/src/core/StemJobLoaderTest.cpp:77` `testTheLoaderRunsOnTheWorkerAfterSubmitReturned` is the Q050-010 regression test: it submits a loader that records `std::this_thread::get_id()`, asserts the loader ran off the caller's thread (`:100`) and that the caller's event loop turned while the loader was still blocked (`:101-102`), then releases it and asserts the job completes. That is exactly "return the job id before decoding begins". It passes in this worktree:

```text
$ cd build/tests && ctest -R 'StemJobLoaderTest|StemSplitPipelineTest' --output-on-failure
1/2 Test #664: StemJobLoaderTest ................   Passed    0.45 sec
2/2 Test #667: StemSplitPipelineTest ............   Passed    0.85 sec
100% tests passed out of 2
EXIT=0
```

No new test is added: per the procedure, an ALREADY-FIXED verdict goes to the report with the code evidence, and a RED run would require reintroducing the defect to a product file this job must not change. The claim is verified instead against the code and the pre-fix commit above.

## Commands and results

All build output and test output went to the log folder. Builds used `-j4`.

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target StemJobLoaderTest -j4` | 0 | `build-1-stem-job-loader.log` |
| 2 | `cd build/tests && ctest -R 'StemJobLoaderTest' --output-on-failure` | 0 | `test-1-stem-job-loader.log` (1/1 passed) |
| 3 | `cmake --build build --target StemSplitPipelineTest -j4` | 0 | `build-2-stem-split-pipeline.log` |
| 4 | `cd build/tests && ctest -R 'StemJobLoaderTest\|StemSplitPipelineTest' --output-on-failure` | 0 | `test-2-stem-family.log` (2/2 passed) |
| 5 | `git merge-base --is-ancestor 59060ceea5 HEAD` (and the pre-fix `git show` above) | 0 | `git-1-history-evidence.txt` |
| 6 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-2945` | 0 | `log-1-feature-check.txt` (`RESULT: PASS`, all 8 gates) |

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

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** No product change was made, so there is no reverted run to make: the verdict is ALREADY-FIXED and the defect is absent from this branch. The covering Q050-010 test `StemJobLoaderTest::testTheLoaderRunsOnTheWorkerAfterSubmitReturned` passes (EXIT 0, `test-2-stem-family.log`), and the pre-fix commit `52d7334966^` is quoted above showing the decode that it pins. No test was weakened or re-anchored.
- **The test is deterministic: no wall-clock timing, sleeps or races.** The regression evidence for this verdict is code-level (worker loop vs. `start()`), not a timing measurement. The existing covering test uses a thread-id seam plus atomic flags and a release latch rather than measuring decode speed; I added no timing-based test.
- **The fix is at the cause and complete: every caller and sibling was checked.** `stem.job_start` is the only caller of `control::stemStartJob` (`src/core/ControlCommandsStems.cpp:143`), which is a one-line delegate to `surface()->start` (`src/core/ControlStemSupport.cpp:582-590`). The GUI's "Split to stems" path goes through the same registered command (`src/gui/StemSplitController.cpp` calls `stem.job_start`), so both paths share the fixed `start()`. `stem.job_status`, `stem.job_result`, `stem.job_cancel` and `stem.tracks_create` read the manager's state and never decode a source. `stem.model_download` is the declared exception (network fetch on its own path) and is outside this finding. The `.ds`/`.ogg` header-less case is also routed to the worker: `stemSourceLength` sets `frames = 0` for those and `jobObject` fills the length from `m_manager.loadedFrames()` after the worker decoded (`ControlStemSupport.cpp:454-456`).
- **No collateral damage.** No project, save, undo or data format code changed; the diff for this job is the report only. Nothing new runs on the audio thread. No limit or check changed. The existing stem tests pass unchanged (`test-2-stem-family.log`, EXIT 0).
- **If your verdict is ALREADY-FIXED: the code (file:line) or contract that shows it.** `src/core/ControlStemSupport.cpp:223-236` (header length, then `submitFrom` with a loader) and `src/core/StemJobManager.cpp:554-562` (the loader is called from `loadMix` on the worker). The fix commit `59060ceea5` is an ancestor of HEAD (`EXIT=0` in `git-1-history-evidence.txt`).

## Other findings

- `include/ControlStemSupport.h:36` still says "`stem.job_start` decodes the source, submits and returns the id". That sentence predates Q050-010 and is ambiguous next to the correct implementation; the authoritative statement is `src/core/ControlCommandsStems.cpp:116-117`. Not changed here because this is an ALREADY-FIXED verdict and the finding is about behaviour, not comment wording.
- `src/core/ControlCommandsSessionRecordLand.cpp:174` calls `SampleBuffer::fromFile` in a different command group (session recording). Outside this finding; not changed.
