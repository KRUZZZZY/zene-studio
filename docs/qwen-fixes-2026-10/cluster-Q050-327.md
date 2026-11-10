IMPLEMENTED

# Q050-327 Freeze selected tracks as one parallel task

Branch `feat/050-Q050-327-oc`, cut from `050/m0-train-26` (`af010bc3cc`). Feature record with the design:
`docs/050-features/Q050-327.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-327.r1/logs/` (one file per
build/test run; numbers below name them).

New: `freeze.tracks` (true_inverse) - two or more instrument/sample tracks rendered as concurrent child renders behind
ONE TaskCenter task (aggregate progress, Cancel), installed all-or-nothing as ONE ProjectJournal step. GUI: the track
menu's **Freeze several tracks...** opens a checkable track list whose Freeze button sends the same command.

## Acceptance

All runs `ctest -R '^<name>$' --output-on-failure` from `build/tests`, each reporting "out of 1".

| Criterion | Test | Exit with change | Exit with the change reverted / mutated |
| --- | --- | --- | --- |
| A multi-track test runs freezes concurrently, reports one task with progress/cancel and creates one undo entry | `BatchFreezeTest` (`rendersConcurrentlyAsOneTaskAndUndoesAsOneStep`: barrier needs >=2 child renders running at once, one task, progress 0..1, undo depth +1, one undo thaws all, save/open round trip; `theTrackMenuOpensTheSelectionAndFreezesTheTickedTracks`: the GUI action + dialog click); `TrackRenderMenuTest`; `ControlFreezeTracksTranscript` (real tripleoscillator renders over the socket: one task in task.list, done, both frozen per track.get_state, depth +1, one undo) | 0 / 0 / 0 (test-17-restored-*, test-15-1..3, test-14-*) | 8 / 8 / 8 reverted (test-16-reverted-*); BatchFreezeTest 8 with `parallelLimit()` forced to 1 (test-10-serial: "the render failed (exit 7)") and 8 with the checkpoint merge removed (test-11-nomerge: undo depth 3) |
| Cancel and partial failure restore every selected track to its original state | `BatchFreezeTest` (`cancelAfterOneTrackRenderedRestoresEveryTrack` via task.cancel after one render finished; `aFailedRenderAfterAnotherSucceededRestoresEveryTrack`; `aTargetChangedDuringTheRenderAppliesNothing`; `refusesABatchItCannotFreezeWholeAndStartsNothing`) - no track frozen, undo depth unchanged, staged/scratch files gone, a pre-existing output file kept; `ControlFreezeTracksTranscript` (real cancel through task.cancel, refusals) | 0 / 0 | 8 / 8 (test-16-reverted-*) |

Neighbours with the change, exit 0 each (test-12-*): ControlRenderJobTest, RenderTaskLifecycleTest, TaskCenterTest,
BackgroundTasksGuiTest, ReversibilityContractTest, ReversibilityUndoTest, ControlCommandsSnapshot,
ControlFreezeCommandsTranscript. BatchFreezeTest was also run three times in a row (test-2-1..3, test-15-1..3): exit 0
each time.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored.** Reversed the non-test hunks of
  src/core/ControlCommandsFreeze.cpp, src/gui/TrackRenderMenu.cpp, include/TrackRenderMenu.h and
  src/core/ControlReversibilityTableLive.cpp (`git diff af010bc3cc HEAD -- <files> | git apply -R`, APPLY=0), rebuilt
  (`build-16-reverted.log`, EXIT=0): BatchFreezeTest exit 8 (7 of 9 functions fail: "no command 'freeze.tracks'"),
  TrackRenderMenuTest exit 8, ControlFreezeTracksTranscript exit 8. Restored with `git checkout HEAD -- <files>`,
  rebuilt (`build-17-restored.log`, EXIT=0): 0 / 0 / 0. The BatchFreeze sources stayed in place for the revert (the
  tests do not compile without them), so the engine's own semantics were also mutated and restored: concurrency
  (`parallelLimit()` = 1 -> BatchFreezeTest exit 8, a child times out at the barrier) and the single step (no
  `mergeCheckpointsSince` -> exit 8, undo depth 3).
- **Every acceptance criterion is proven by a test I ran.** Table above: BatchFreezeTest 0, TrackRenderMenuTest 0,
  ControlFreezeTracksTranscript 0 (it ran, not skipped: "instrument: tripleoscillator", all 14 checks "ok", test-4).
- **One undo step per edit; honest reversibility class.** `freeze.tracks` is `true_inverse`
  (src/core/ControlReversibilityTableLive.cpp): the completed batch is one ProjectJournal edit whose Track checkpoints
  are merged (`closeEdit` in src/core/BatchFreeze.cpp); proven by undo depth +1 and one undo thawing every track
  (BatchFreezeTest, transcript). A cancelled/failed/stale batch records nothing (depth unchanged, tested).
  `task.list` / `task.cancel` are unchanged and `not_mutating`. ReversibilityContractTest exit 0 with the published
  histogram updated (`rows=934 true_inverse=460 ...`).
- **Nothing added runs on the audio thread.** All batch code runs on the UI thread (QProcess signals, queued calls) or a
  QThreadPool worker (`finishPreparedRender`: file trim/hash, no Engine or Track access). The only engine touch is the
  existing `Track::freezeTo`, called on the UI thread, which takes `requestChangesGuard` as it always has; no
  audio-thread function was changed.
- **Old projects load and render as before; new data round-trips.** No format change: the batch writes the existing
  per-track `frozenAudio`/`frozenStart`/`frozenEnd` attributes; batches are session state. BatchFreezeTest saves the
  batch-frozen project, opens it, and requires the three tracks frozen with audio loaded and the fourth not (exit 0).
- **Portability.** Qt 5.15-safe APIs only (QProcess/QOverload, QThreadPool, QListWidget, QDialog::open,
  qEnvironmentVariable >= 5.10); no Qt-6-only API, no std::filesystem, no `small`/`near`/`far`/`interface`/`min`/`max`
  identifiers; the test's windows.h include carries NOMINMAX and its POSIX `kill` is under `#if !Q_OS_WIN`, as in
  ControlRenderJobTest. Only built here with GCC/Qt on Linux - the hosted matrix was not run.
- **Gates.** `feature_check.py` cannot write its log directory from this lane (it creates
  `<worktree>/../zene-feat-q050-327-feature-check`, outside the write-jail: PermissionError), and `submit_check.py`
  therefore shows "gates FAIL" with no FAIL line. I ran a copy whose only change is the log directory
  (`/tmp/q050-327-fc/logs`; same commands, same baseline cache): fork-sources, all-sources-reproduce,
  no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS, RESULT PASS
  (`feature-check-4.log`, exit 0). The loop's own run of the unmodified script at session end should reproduce it.
- **Snapshot / canary.** `snapshot_live.py` (EXIT=0): 925 ids / 78 groups, wasm.* out; README canary updated;
  `tests/doc-figures-gate.py` PASS; ControlCommandsSnapshot exit 0.
- **Not done / limits.** Whole-track batches only; the take decode in `Track::freezeTo` still happens on the UI thread
  at apply time (as the existing queued freeze does); TSan not available here - race reasoning is in the feature
  record. Build note: ccache's configured temporary_dir is outside the jail, so builds used `CCACHE_TEMPDIR=/tmp/...`.
