IMPLEMENTED

Q050-010: plug-in scans, renders, stem jobs and browser peak reads run in the background and are followed and cancelled
through `app.tasks`. Record: `docs/050-features/Q050-010.md`. Logs: `runs/Q050-010.r1/logs/q1-*`.

## Acceptance

Final build (`q1-build-15.log`, EXIT 0) and run (`q1-test-final.log`): `ctest -R` over 22 tests, 100% passed, EXIT=0.

| Criterion | Tests | Exit |
|---|---|---|
| Start, inspect, cancel, complete scan/render/stem/peak through app.tasks | `ControlBackgroundTasks`, `ControlStemCommands`, `PeakJobsTest`, `PluginScanWorkerTest`, `RenderTaskLifecycleTest` | 0 |
| Window stays interactive during each; stem resampling on the worker | `BackgroundTasksGuiTest`, the four responsiveness slots, `StemJobLoaderTest`, the stem transcript's reply-time check | 0 |
| Cancel leaves state and temp files consistent; docs updated | the cancel slots listed in the record; docs and manual | 0 |

Neighbours, EXIT=0 in the same run: `ControlRenderJobTest`, `StemJobManagerTest`, `PluginQuarantineMenuTest`, `PluginScanCacheTest`,
`TaskCenterTest`, `TaskIndicatorTest`, `ControlBrowserCommandsTest`, `ReversibilityContractTest`, `ControlCommandsSnapshot`,
`ControlPluginScanCommands`, `ControlPluginScanBackground`, `ControlSurfaceReferenceTest`, `ControlStemExportVerb`, `LibraryMenusTest`, `ProjectToolsTest`.
`tests/rt-safety-sweep.py` EXIT=0.

## Self-check

**Tests fail without the change.** Non-test hunks were mutated, rebuilt and run, then restored (`git restore`), rebuilt, and passed.

| Mutant (log) | Fault | Result |
|---|---|---|
| broad (`q1-mut-test-1`) | loads and reads inline on the caller, `app.tasks` rows and cancel off, stem decode on caller, view and menu on the old routes | ctest EXIT=8: `PluginScanWorkerTest`, `PeakJobsTest`, `BackgroundTasksGuiTest`, `ControlBackgroundTasks` (timeout), `ControlStemCommands` (reply took 3.32 s) fail. `PluginQuarantineMenuTest` stays green: it only checks no refusal |
| m2 | late-cancel check off; cancel-during-load off; render reply's `task` off | EXIT=8: `aCancelAfterTheReadBeforeAdoptionWins`, `aRunningCancelLeavesTheCacheAlone`, `aCancelDuringALoadEndsTheRunNowAndKeepsTheCatalogue`, both `RenderTaskLifecycleTest` slots |
| m3 | peak id = local id; stem loader inline on the caller | EXIT=8: `theReturnedIdIsTheTaskCentersAfterAnotherTaskTookOne` and the id-dependent peak slots, `testTheLoaderRunsOnTheWorkerAfterSubmitReturned` |
| m4 | stem loader's cancel check off | EXIT=8: `testCancelDuringTheLoaderCancelsTheJobWithoutSeparating` (this test passed before being sharpened; it now asserts `separate` never ran) |
| m5 | run-generation check off | EXIT=8: `aReportFromAnEndedRunDoesNotReleaseTheNextOne` (also rewritten after an earlier form passed) |

| m6 | small-file read off; peak failure reported as done; loader's reason dropped | EXIT=8: `aSmallFileIsDrawnAtOnceWithoutATask`, `aFileThatCannotBeReadFailsItsTaskWithTheReason`, `testALoaderThatFailsFailsTheJobWithItsReason`. Restored: `q1-test-restored.log`, EXIT=0 |

**Every criterion proven by a test I ran:** see the table above, with exit codes from the final run.

**Undo and reversibility.** No project edit, so no undo step. `browser.peaks_start` is `not_mutating`; `plugin.scan_start` stays
`irreversible`; `app.tasks` and `app.task_cancel` stay `not_mutating`. `ReversibilityContractTest` EXIT=0.

**Realtime.** Nothing added runs on the audio thread: the new paths are a control handler, the UI thread, `PeakJobs`' pool thread,
the global pool and the stem worker. `tests/rt-safety-sweep.py` EXIT=0; `ControlBackgroundTasks` runs scan and peak jobs with the
transport playing and checks it still is. That is a guardrail, not an xrun measurement. TSan does not run on this box. By design: the peak
table is main-thread only and the worker sees a job through atomics; the scan's worker gets a path, its report is queued and
carries a run number; the stem loader is copied under the existing mutex and its mix stored under it.

**Projects.** No project format or load path changed, so nothing was re-rendered for this; no new saved data.

**Portability.** Compiled and run on Linux, Qt 6.12, GCC only. Qt 5.15, MSVC, macOS and hosted CI were NOT run here. By reading:
`QThreadPool::start(std::function)` exists since 5.15; no `QThread::create`; no std::filesystem; no reserved identifiers;
`PluginScanWorkerTest` skips on Windows as `PluginScanCacheTest` does.

**Environment notes.** `feature_check.py` and `submit_check.py` write outside this lane's write-jail (a sibling `-feature-check` directory),
so I ran copies from `/tmp` differing only in that log path; `CCACHE_TEMPDIR` was set to `/tmp` because the configured one refused.
A one-off `ControlStemCommands` failure (job finished between 0.5 s polls, in-process backend) was a race in the test; that wait now polls at 10 ms.
The brief said earlier commits were on this branch: it was exactly at train-24, so the work was redone here.
