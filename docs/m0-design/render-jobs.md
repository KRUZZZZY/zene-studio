# M0 render jobs — cluster 917

Branch: `design/m0-render-jobs`, based on `c405233f8`

Scope: asynchronous bounce/freeze/flatten/range-bounce lifecycle; design only

## Problem

`BounceInPlace::runCliRender()` starts the shipped CLI renderer and synchronously waits up to 30 seconds for start and 600 seconds for completion (`src/core/BounceInPlace.cpp:171-185`); `renderTracks()` consumes that result before returning (`:337-370`). The track menu calls `bounce.in_place` and `freeze.track` through `runBusy()` directly from its action (`src/gui/TrackRenderMenu.cpp:46-64,69-75`). The control handlers for bounce/freeze, `track.flatten`, and `range.bounce` likewise wait for the result before returning (`src/core/ControlCommandsFreeze.cpp:129-148,162-225`, `src/core/ControlCommandsTrackFlatten.cpp:74-121`, `src/core/ControlCommandsRangeBounce.cpp:90-120`). Freeze, flatten, and range bounce then mutate tracks and create undo state from that result. A one-line conversion of `waitForFinished()` to an asynchronous start would therefore either lose the result or apply it after the target has changed. The blocker and caller-ordering were independently confirmed in G-behaviour and its review (links below).

## Chosen design

Add a main-thread-owned `RenderJobManager` around the existing child-process renderer. It prepares an immutable job input on the UI/control thread, starts a `QProcess` without waiting, and completes through Qt event-loop signals. The child continues to render the same serialized session copy with the same tracks unmuted, WAV format, sample rate, and range. Keep the existing render math and CLI arguments. Do not retain `Song*`, `Track*`, `Clip*`, or other live model pointers in a job.

Each accepted job records an operation, stable target ids, the requested output and range, a per-target state signature, and any operation-specific data needed later. `range.bounce` also snapshots its selected ids, range endpoints, and original mute values. `track.flatten` keeps its existing bounded track XML pre-image for undo. Freeze-region clip mute data is derived only after completion from the still-matching live target. The session snapshot and target signatures are made on the main thread, then the job drops all live model references before process start.

Use a unique per-job temporary directory for the serialized project and intermediate render. Render to a unique staged output; do not expose a partial file at the requested destination. For a range render, trim to a staged file beside the destination. After child success, perform WAV validation, frame count, and SHA-256 work on a worker using only paths and copied scalar/timeline data; return the value result to the main thread. The staged file is committed on the main thread only after the stale-target check. Preserve any prior destination on failure, cancellation, timeout, or stale refusal. Clean the per-job directory and staged output on every terminal path.

The completion callback is the only place that may apply a project mutation. On the main thread it resolves each stable id again, checks the target signature (and the range selection for `range.bounce`), and refuses as stale if a target was removed, replaced, or edited. It then rechecks the operation preconditions, commits the staged file, applies the existing freeze/flatten/landing change, and creates exactly one existing journal/structural undo step. If validation or application fails, restore the prior destination and record no undo. `bounce.in_place` is output-only and records no undo; it still refuses to publish a stale result so the returned file always corresponds to the current target at completion.

### Job and command surface

- `render.job_start` accepts an operation (`bounce`, `freeze_track`, `freeze_region`, `flatten`, or `range_bounce`) and that operation's existing arguments. It returns a job id immediately after validation, snapshot preparation, and child start.
- `render.job_status` reports the id, state, phase, status text, progress, target ids, and any terminal error. `render.job_result` returns the existing render metadata and, for mutating operations, the ids/state produced by the single completed mutation. It refuses until the job is terminal. `render.job_cancel` requests cancellation by id.
- Each job also registers with `TaskCenter`. The existing Tasks indicator displays queued/running/failed/cancelled/done and exposes Cancel through `task.cancel`; the track menu starts a job and returns immediately instead of setting a wait cursor around the whole render. Use `TaskCenter::progress()` for phase changes. The child renderer has no reliable percentage today, so rendering reports `progress: -1` (the existing indeterminate bar contract), not a guessed percentage. Report preparation, finalization, and apply phases as status changes.
- Keep the existing `bounce.in_place`, `freeze.track`, `freeze.region`, `bounce.land`, `track.flatten`, and `range.bounce` result contracts as a documented synchronous compatibility path for existing agents. They submit to the same manager and wait for that job's terminal result; the GUI never calls this path. The job lifecycle is the preferred API for agents that need prompt acknowledgements, polling, or cancellation. Cancellation and status are also available through `render.job_*`.
- A completed job remains queryable until explicitly cleared. Add `render.job_clear_finished` for that cleanup; `task.clear_finished` continues to clear only the Tasks UI history.

## Invariants and ownership

| Object/data | Owner and thread | Allocation and release |
|---|---|---|
| Job table, ids, state, callbacks, `QProcess`, phase timer | `RenderJobManager`, main/UI event-loop thread | Allocate on accepted start; retain terminal metadata until `render.job_clear_finished`; process and timer deleted after terminal cleanup |
| Live `Song`/track/range reads and serialized snapshot | Main/UI thread only | Read/capture before start; no model pointer crosses the start boundary |
| Child process | OS process launched by the manager | Start asynchronously; kill on cancel/timeout/quit; finalize only from `finished`/startup-error handling |
| Trim/hash inputs and buffers | Worker pool, copied immutable values only | Allocate in finalization; release when its completion callback returns |
| Temporary project, intermediate WAV, staged destination | The job record owns paths; filesystem I/O is outside the audio callback | Unique per job; remove on failure/cancel/stale, or after successful destination commit |
| Undo pre-image/action | `ProjectJournal` on the main thread | Allocate only after successful render and stale check; retain under existing journal byte/depth caps; free when the journal evicts/clears it |
| Task row | `TaskCenter`, main thread | Create at job acceptance; update by posted/main-thread calls; terminal rows follow its existing 20-finished-task cap |

There is at most one active render-job child per application instance. Keep at most 64 job records total, including active and terminal records. At either capacity, refuse before writing or mutating, return typed `busy`/capacity information, and increment a `render_job_capacity_refusals` counter visible through `control.ping`; never evict a result silently. `render.job_clear_finished` frees terminal records and makes room. The existing `TaskCenter` finished-row cap remains its separate UI-history bound.

Cancellation is a state transition, not a blocking wait: mark `cancel_requested`, kill the child, and let the ordinary process-finished callback reap it and remove staged files. A cancelled, failed, timed-out, or stale job never applies a model change and never creates an undo step. A single terminal-state guard prevents completion, cancellation, timeout, and shutdown callbacks from applying or cleaning the same job twice. The existing 30-second child-start and 600-second render-life bounds become nonblocking timers.

## Realtime and audible-behaviour argument

The parent job APIs are called from the GUI/control event loop. Session serialization, file I/O, process start/stop, task reporting, trimming, hashing, and model/undo changes all stay off the audio callback. The manager must not be called from an audio callback. No new lock, allocation, thread start, syscall, wait, or Qt signal is added to any parent audio-thread path. The CLI child uses the existing offline render path unchanged; the parent only observes its process events. No realtime allowlist entry is needed because no audio-thread code changes.

The child sees the same saved-project snapshot and mute edits that `BounceInPlace::renderTracks()` uses now. Keep sample rate, WAV encoding, range-to-frame rounding, tail behavior, and track/fader/send processing unchanged. Move when the result is consumed, not how the audio is produced. The only new refusal is an explicit stale-target/capacity/cancellation outcome before a model change can be applied. For a successful unchanged target, render samples and the resulting freeze/flatten/landed model state remain the same; compare before/after renders bit-for-bit where the current engine permits, otherwise state the measured tolerance.

## Compatibility, failures, and verification plan

The synchronous command path preserves its existing result shape and undo timing from the caller's perspective: it returns only after the shared job finishes. The new UI and async agent commands acknowledge at start and expose completion through the job id. Startup failure, child crash/nonzero exit, timeout, cancellation, invalid WAV, stale target, and full capacity are distinct terminal/refusal results; none is reported as success. Qt 5.15 compatibility uses the existing `QProcess` signal overload pattern (`QOverload<int, QProcess::ExitStatus>`) and `QTimer`; do not rely on Qt-6-only APIs without a version guard.

Implementation proof should add targeted cases for: asynchronous acknowledgement while the child remains active; progress/task row and GUI/task cancellation; child failure and timeout cleanup; no partial/replaced destination on cancel/failure; stale target and changed range refusal with unchanged undo depth; exactly one undo for each successful freeze/flatten/range bounce; synchronous compatibility result/schema; and concurrent/capacity refusal. Exercise both Qt 5.15 and Qt 6 builds. Add an audio comparison for unchanged successful renders. If commands or schemas change, regenerate the full commands snapshot from a live instance with `scratchpad/m0/snapshot_live.py`; keep all descriptions intact (the ~1 MB snapshot is exempt from evidence caps).

## Considered and rejected

1. **Keep the wait and only show a busy cursor.** It preserves result ordering but still blocks the GUI and cannot be cancelled from Tasks.
2. **Render in the live engine.** `ProjectRenderer`/`Song::startExport()` acts on the live audio engine and transport; it does not preserve the isolated child-render behavior this feature already relies on.
3. **Run the existing handler on a worker thread.** Its handlers read and mutate `Engine`, `Song`, `Track`, range state, and the journal. Moving those calls off the main thread would violate model thread affinity and race with edits.
4. **Infer percent complete from elapsed time or output bytes.** Render duration varies with plug-ins and session content; a guessed percentage is misleading. Use the task indicator's supported indeterminate state until the child can report a trustworthy measure.
5. **Replace the current command result with an async acknowledgement.** That would break existing agents that consume `path`, frame/hash data, landing ids, and undo metadata from the current synchronous reply. Add the explicit job API and keep the old verbs as compatibility wrappers.

## Evidence

- [G-behaviour.md](../../../zene-bunny-engine/docs/m0-blocker-fixes-2026-10-08/G-behaviour.md): cluster 917 remains blocked because callers consume the synchronous result before model/undo work.
- Independent review evidence: `/tmp/claude-1000/-home-kruzzzzy-Documents-AI-KOS-PROJECT-projects-zene-studio-program/719627a5-c809-46ec-b4a4-231af5d313ec/scratchpad/luna-review-G-behaviour/review.md` confirms the ten-minute wait and inline menu call.
- Existing patterns: `include/TaskCenter.h`, `src/core/TaskCenter.cpp`, `src/core/RenderQueue.cpp`, `src/core/StemJobManager.cpp`, and `src/core/ControlCommandsMasteringRun.cpp`.
