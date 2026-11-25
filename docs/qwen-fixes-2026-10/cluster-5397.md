ALREADY-FIXED

5397: ALREADY-FIXED — "Split to stems" no longer resamples the clip on the GUI thread

Finding: `src/gui/StemSplitController.cpp:159-163` (historical) resampled the full source buffer with
`resampleBuffer(source, StemModelSampleRate)` on the GUI thread before submitting it to the stem worker.

## Decision and code evidence

This branch already has the fix the finding asks for. The GUI never resamples. A clip is either handed to the
registered `stem.job_start` as a file path, and the job's worker decodes and resamples it, or its native-rate
WAV is written on a pool thread first.

- `src/gui/StemSplitController.cpp:219-256` (`splitClipToStems`): takes a snapshot, then calls
  `resolveSourceFile`. It then calls `beginPreparation` for an in-memory clip or `startJob` for a
  file-backed one. There is no `resampleBuffer` or `SampleBuffer` copy on this path. An fff grep for
  `resampleBuffer` across the tree finds hits only in `src/core/ControlStemSource.cpp:130`,
  `src/core/RenderQueue.cpp:207`, `src/core/SampleBufferResample.cpp:58`, tests and docs. It finds none in
  `src/gui/`.
- `src/gui/StemSplitController.cpp:289-313` (`resolveSourceFile`): a clip loaded from a file passes
  only its absolute path on. The comment at :297-298 says "the engine decodes that same file and
  resamples it to the model's rate, both on its own worker".
- `src/gui/StemSplitController.cpp:321-355` (`beginPreparation`): an in-memory clip is written with
  `control::stemWriteWavFile(..., buffer.sampleRate(), ...)`. That runs at its own rate, inside
  `QThreadPool::globalInstance()->start(...)` (:343). The result is posted back with a queued
  `invokeMethod` (:350).
- `src/core/ControlStemSupport.cpp:224-237` (`stemStartJob`): the caller only reads the header length
  (`stemSourceLength`, :227). It submits `m_manager.submitFrom([source](...) { return stemDecodeAtModelRate(source, cancel, why); }, ...)`
  (:235-237), so the loader runs on the manager's worker.
- `src/core/ControlStemSource.cpp:115-131` (`stemDecodeAtModelRate`): this is where
  `SampleBuffer::fromFile` and `resampleBuffer(decoded, StemModelSampleRate)` (:130) now live. It is the
  worker-side loader.

History: `git log --oneline -S resampleBuffer -- src/gui/StemSplitController.cpp` (EXIT=0) lists
`1ec0e0bd86` ("feat(stems): the GUI and an agent share the registered stem.* commands, four tracks one undo step
(Q050-035)") as the last commit that changed the count. Its diff for this file, saved to
`logs/01-show-1ec0e0bd86.diff`, removes line 233:
`-	const auto buffer = resampleBuffer(snapshot.source, StemModelSampleRate);`.
`git merge-base --is-ancestor 1ec0e0bd86 HEAD` gives EXIT=0. The core half (header-only start, decode and
resample in the worker loader) is documented in `docs/qwen-fixes-2026-10/cluster-4091.md` (commits
`52d7334966`, `ef85c1ac92`, Q050-010). The earlier SKIPPED-SCOPE attempt
(`docs/bunny-fixes-2026-10-05/gui/batch-12.md:36`) quoted the line before it was removed.

## Existing regression coverage

`tests/src/gui/StemSplitBackgroundTest.cpp` already pins the behaviour:
- `theLengthOfASourceNeverDecodesOnTheCaller` (:197): `stemSourceLength` runs zero full decodes
  (`SampleDecoder::decodeCallCountsForTesting().full == 0`).
- `aDrumSynthSourceIsDecodedByTheJobWhileTheLoopTurns` (:220): `stem.job_start` replies with `frames == 0`,
  and the decode happens later on the worker while the caller's event loop turns.
- `aLongInMemoryClipIsWrittenOnAWorkerThenBecomesAJob` (:244): `splitClipToStems` on a 120 s in-memory clip
  returns at once. The write runs on a thread other than the caller (`m_writer != this_thread`, :262), and
  the job then runs through `stem.job_start`.

No product code was changed, so there is no new regression test and no red/green pair.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5397.r1/logs)

| # | Command | EXIT |
|---|---------|------|
| 01 | `git show 1ec0e0bd86 -- src/gui/StemSplitController.cpp > logs/01-show-1ec0e0bd86.diff` | 0 |
| — | `git merge-base --is-ancestor 1ec0e0bd86 HEAD` | 0 |
| 02 | `cmake --build build --target zene StemSplitBackgroundTest StemSplitMenuTest -j4 > logs/02-build.log 2>&1` | 0 |
| 03 | `cd build/tests && ctest -R StemSplit --output-on-failure > logs/03-ctest-stemsplit.log 2>&1` | 8 (StemSplitPipelineTest "Not Run": its target was not built yet; Menu and Background passed) |
| 04 | `python3 …/qwen/feature_check.py <worktree> > logs/04-feature-check.log 2>&1` | 0 (RESULT: PASS, all 8 gates PASS) |
| 05 | `cmake --build build --target StemSplitPipelineTest -j4 > logs/05-build-pipeline.log 2>&1` | 0 |
| 06 | `cd build/tests && ctest -R StemSplit --output-on-failure > logs/06-ctest-stemsplit.log 2>&1` | 0 ("100% tests passed out of 3": StemSplitPipelineTest, StemSplitMenuTest, StemSplitBackgroundTest) |

Feature-check table (log 04):
```
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

Delegation: Haiku (two starts) ran the builds, the `ctest -R StemSplit` runs and `feature_check.py`, and
reported the exit codes. I checked the summary line of log 06 myself. Sonnet was not used: there was no fix
or test to write, because the verdict is ALREADY-FIXED. DeepSeek was unavailable.

## Self-check

Answers to the ASK items from `submit_check.py` (log `07-submit-check.log`):

- **Regression test fails with the product change reverted and passes with it:** not applicable. The verdict
  is ALREADY-FIXED and this commit changes no product file (only this report). The earlier fix's behaviour is
  pinned by the existing `StemSplitBackgroundTest`, which passes here (log 06, EXIT=0, "100% tests passed
  out of 3"). Its tests pass when the fix is present. I did not revert `1ec0e0bd86` to get a red run,
  because that commit is a large feature (Q050-035) and not a patch that can be reverted on its own.
- **Determinism:** I added no new test. The existing tests use explicit seams: the preparation hook
  (`StemSplitController::setPreparationHookForTest`, `StemSplitBackgroundTest.cpp:148`), which records the
  writer's thread id, and `SampleDecoder::decodeCallCountsForTesting()` (:215, :240). The hook's 2 ms poll loop
  and the `QTRY_*` waits belong to the existing test, not to this job.
- **Fix at the cause, callers and siblings:** I changed no code. I checked both GUI routes into the job:
  file-backed (`StemSplitController.cpp:299-308` → `startJob` → `stem.job_start`) and in-memory
  (`:321-355` → pool worker → `preparationEnded` :390 → `startJob`). I also checked the agent route
  (`ControlCommandsStems.cpp:119-152` → `stemStartJob`, `ControlStemSupport.cpp:224-237`). None of them
  resamples on the caller. The only `resampleBuffer` call on the stem path is in the worker loader
  `ControlStemSource.cpp:130`.
- **No collateral damage:** this commit has no product change, so loading projects and saved data is
  unchanged. No limit or check was added.
- **ALREADY-FIXED evidence:** `src/gui/StemSplitController.cpp:289-313` and `:321-355`,
  `src/core/ControlStemSupport.cpp:235-237`, `src/core/ControlStemSource.cpp:130`. The removed line is in commit
  `1ec0e0bd86` (log 01, diff line 233), and that commit is an ancestor of HEAD (EXIT=0).
- **Sibling commands (app.*, arrangement.*):** not affected. The check lists the command files changed on
  this branch against its base (652 files from the train merges). This job changes no command file, so none of
  `app.about … arrangement.get_state` is touched.

## Other findings

- The brief points to the proof-manifest schema at `schemas/proof.json` in the config directory, but
  `/home/kruzzzzy/.config/kads/zene/` holds only `state/`, so the schema could not be checked. The manifest
  uses the field names from the brief's example.
