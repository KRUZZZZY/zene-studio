# M0 render jobs — cluster 917

Branch: `design/m0-render-jobs`, based on `c405233f8`

Scope: asynchronous bounce/freeze/flatten/range-bounce lifecycle

## Problem

`BounceInPlace::runCliRender()` starts the shipped CLI renderer and synchronously waits up to 30 seconds for start and 600 seconds for completion (`src/core/BounceInPlace.cpp:171-185`); `renderTracks()` consumes that result before returning (`:337-370`). The track menu calls `bounce.in_place` and `freeze.track` through `runBusy()` directly from its action (`src/gui/TrackRenderMenu.cpp:46-64,69-75`). The control handlers for bounce/freeze, `track.flatten`, and `range.bounce` likewise wait for the result before returning (`src/core/ControlCommandsFreeze.cpp:129-148,162-225`, `src/core/ControlCommandsTrackFlatten.cpp:74-121`, `src/core/ControlCommandsRangeBounce.cpp:90-120`). Freeze, flatten, and range bounce then mutate tracks and create undo state from that result. A one-line conversion of `waitForFinished()` to an asynchronous start would therefore either lose the result or apply it after the target has changed. The blocker and caller-ordering were independently confirmed in G-behaviour and its review (links below).

## Chosen design

Extend the existing main-thread-owned `RenderQueue` with a bounce job kind. `render.queue_bounce` prepares an immutable job input for a project edit on the UI/control thread, starts a `QProcess` without waiting, and completes through Qt event-loop signals. The output-only single-track bounce uses the existing `render.queue_add` command with `operation: "bounce"` and `track`, whose contract is already `not_mutating`. The child renders the same serialized session copy with the same tracks unmuted, WAV format, sample rate, and range. Keep the existing render math and CLI arguments. A job retains stable ids, signatures and copied operation data, never `Song*`, `Track*`, `Clip*`, or other live model pointers.

Each accepted job records an operation, stable target ids, the requested output and range, a per-target state signature, and any operation-specific data needed later. `range.bounce` also snapshots its selected ids, range endpoints, and original mute values. `track.flatten` keeps its existing bounded track XML pre-image for undo. Freeze-region clip mute data is derived only after completion from the still-matching live target. The session snapshot and target signatures are made on the main thread, then the job drops all live model references before process start.

Use a unique per-job temporary directory for the serialized project and intermediate render. Render to a unique staged output; do not expose a partial file at the requested destination. For a range render, trim to a staged file beside the destination. After child success, perform WAV validation, frame count, and SHA-256 work on a worker using only paths and copied scalar/timeline data; return the value result to the main thread. The staged file is installed on the main thread only after the stale-target check, with any previous destination retained for rollback until the model edit succeeds. Preserve the previous destination on failure, cancellation, timeout, or stale refusal. Clean the per-job directory and staged output on every terminal path.

The completion callback is the only place that may apply a project mutation. On the main thread it resolves each stable id again, checks the target signature and, for `range.bounce`, the range selection and tempo state. It refuses as stale if a target was removed, replaced, or edited. It then applies the freeze/flatten/landing change inside a `ProjectJournal` edit bracket, merges all checkpoints and actions from that callback into one step, and records the completed command against that step. If validation or application fails, restore the prior destination and record no undo. `bounce` is output-only and records no undo; it still refuses to publish a stale result so the returned file corresponds to the current target at completion.

### Job and command surface

- `render.queue_bounce` accepts one edit operation (`freeze_track`, `freeze_region`, `bounce_land`, `flatten`, or `range_bounce`) and that operation's existing arguments. It validates the document identity, target and snapshot synchronously, then returns a queue job id without waiting for the child. For a bounce that only writes a track output file, use `render.queue_add` with `operation: "bounce"` and `track`; no project edit or undo step is recorded.
- The existing `render.queue_list` and `render.queue_report` expose state, operation, target ids, phase, indeterminate progress, terminal error and the result report. `render.queue_cancel` cancels by job id; `app.task_cancel` and the task indicator use the same cancellation path. `render.queue_clear` frees terminal job records.
- Each bounce job registers with `TaskCenter`. The existing Tasks indicator displays queued/running/failed/cancelled/done and exposes Cancel; the track menu starts the asynchronous command and returns immediately instead of holding a wait cursor for the render. The child renderer has no reliable percentage today, so rendering reports `progress: -1`, not a guessed percentage. Task and job reports expose starting, rendering, finalizing and applying phases.
- Keep `bounce.in_place`, `freeze.track`, `freeze.region`, `bounce.land`, `track.flatten`, and `range.bounce` synchronous with their existing result contracts as the compatibility path for agents that need the old reply shape. They continue to call the synchronous `BounceInPlace` path. `ControlRegistry::invoke()` has no deferred completion callback, and `ControlServer::dispatchLine()` immediately serializes the returned result; `mastering.run` changes its contract to an ACK plus polling rather than replying later. These handlers add no nested event loop or `processEvents()` while waiting, but the wait still blocks the calling thread and freezes the GUI if invoked there. Use the render queue for a responsive UI; removing the legacy wait requires a future transport-level deferred-reply design.

## Invariants and ownership

| Object/data | Owner and thread | Allocation and release |
|---|---|---|
| Job table, ids, state, callbacks, `QProcess`, phase timer | `RenderQueue`, main/UI event-loop thread | Allocate on accepted start; retain terminal metadata until `render.queue_clear`; process and timer deleted after terminal cleanup |
| Live `Song`/track/range reads and serialized snapshot | Main/UI thread only | Read/capture before start; no model pointer crosses the start boundary |
| Child process | OS process launched by `RenderQueue` | Start asynchronously; kill on cancel/timeout; finalize only from `finished`/startup-error handling |
| Trim/hash inputs and buffers | Worker pool, copied immutable values only | Allocate in finalization; release when its completion callback returns |
| Temporary project, intermediate WAV, staged destination | The job record owns paths; filesystem I/O is outside the audio callback | Unique per job; remove on failure/cancel/stale, or after successful destination commit |
| Undo pre-image/action | `ProjectJournal` on the main thread | Allocate only after successful render and stale check; retain under existing journal byte/depth caps; free when the journal evicts/clears it |
| Task row | `TaskCenter`, main thread | Create at job acceptance; update by posted/main-thread calls; terminal rows follow its existing 20-finished-task cap |

There is at most one active render-queue child per application instance. Keep at most `RenderQueue::MaxJobs` (256) records total across render, conversion and bounce jobs, including active and terminal records. At capacity, refuse before snapshot preparation, output writes or model mutation; return a typed refusal, increment `render_queue_capacity_refusals`, and expose the count through `control.ping` and `render.queue_list`. Never evict a result silently. `render.queue_clear` frees terminal records and makes room. The existing `TaskCenter` finished-row cap remains its separate UI-history bound.

Cancellation is a state transition, not a blocking wait: mark the job as cancelling, kill the child, and let the ordinary process-finished callback reap it and remove staged files. Cancellation during worker finalization is checked again on the main thread before apply. A cancelled, failed, timed-out, or stale job never applies a model change and never creates an undo step. Phase and current-job guards prevent completion, cancellation and timeout callbacks from applying or cleaning the same job twice. The existing 30-second child-start and 600-second render-life bounds become nonblocking timers.

## Realtime and audible-behaviour argument

The parent job APIs are called from the GUI/control event loop. Session serialization, file I/O, process start/stop, task reporting, trimming, hashing, and model/undo changes all stay off the audio callback. The queue must not be called from an audio callback. No new lock, allocation, thread start, syscall, wait, or Qt signal is added to any parent audio-thread path. The CLI child uses the existing offline render path unchanged; the parent only observes its process events. No realtime allowlist entry is needed because no audio-thread code changes.

The child sees the same saved-project snapshot and mute edits that `BounceInPlace::renderTracks()` uses now. Keep sample rate, WAV encoding, range-to-frame rounding, tail behavior, and track/fader/send processing unchanged. Move when the result is consumed, not how the audio is produced. The only new refusal is an explicit stale-target/capacity/cancellation outcome before a model change can be applied. For a successful unchanged target, render samples and the resulting freeze/flatten/landed model state remain the same; compare before/after renders bit-for-bit where the current engine permits, otherwise state the measured tolerance.

## Compatibility, failures, and verification plan

The old synchronous commands preserve their existing result shape and undo behavior for agents that rely on their replies, with the GUI-blocking limitation above. The GUI uses the queue and the async edit command acknowledges at start and exposes completion through the job id. Startup failure, child crash/nonzero exit, timeout, cancellation, invalid WAV, stale document/target, and full capacity are distinct terminal/refusal results; none is reported as success. Qt 5.15 compatibility uses the existing `QProcess` signal overload pattern (`QOverload<int, QProcess::ExitStatus>`) and `QTimer`; do not rely on Qt-6-only APIs without a version guard.

The round-two regressions cover asynchronous acknowledgement while the child remains active; task and agent cancellation with direct child-termination and scratch-directory cleanup checks; child failure and unchanged output; stale document reload; one-step undo at the 100-step cap; synchronous compatibility; and capacity refusal. The existing audio comparison checks that successful output matches `bounce.in_place`. Qt 6.4.2 was built and tested here; Qt 5.15 was unavailable. When a command schema changes, regenerate the full snapshot from a live instance and keep its descriptions intact (the ~1 MB snapshot is exempt from evidence caps).

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

## Round 2 review decisions

### Document identity

The track id and XML fingerprint do not distinguish the original project from a reload of the same file. Add a process-unique `Song` document identity, assign it when a `Song` is constructed, and advance it when `clearProject()` replaces or empties the document. Capture that scalar with the bounce context and compare it before target checks or output installation. This covers accepted project loads, `project.new`, direct project clearing, and replacing the `Song` object without retaining a live model pointer. A mismatch fails the job and removes its scratch/staged data; it does not install the output or mutate the new document.

### Undo coalescing at the cap

`undoDepth()` is not a stable mark: a push at the 100-step cap evicts the oldest entry and leaves the depth unchanged. Capture `ProjectJournal::topStepSerial()` immediately before the completion edit and call `mergeCheckpointsSince(serial)` after `endEdit()`. The serial window remains correct when the cap evicts entries. The regression fills all 100 slots, queues a range bounce over multiple tracks, and verifies that one undo restores all source mutes and removes the landed track.

### Honest operation classes

`render.queue_bounce` remains `true_inverse` and accepts only operations that record a project edit. Move output-only track bounce to the existing `render.queue_add` command as `operation: "bounce"` with a `track`; that command is already `not_mutating` and its output is an artefact, like `render.render`. This keeps the live command count at 823 while giving the output-only operation the correct class. Update the live snapshot schema/description and README canary to 823 ids / 76 groups. A new command id was considered and rejected because it adds no capability beyond the existing output queue and would change the required command-count canary.

### Cancellation proof

The fake render child writes its PID to a test-owned marker before sleeping. The cancellation test waits for that marker, cancels through the task surface, then verifies the OS process no longer exists, the per-job temporary directory is gone, the existing destination bytes are unchanged, and no model edit was applied. The marker path is unique to the test and is not part of product output.

### Legacy synchronous replies

`ControlRegistry::invoke()` returns `ControlResult` synchronously. `ControlServer::dispatchLine()` invokes it and immediately builds that request's response; neither API accepts a completion callback or retains a request id/reply sink. `mastering.run` is asynchronous only because it changes its reply to an ACK and has a poll command, which would break the legacy bounce/freeze/flatten/range result contracts. Per the round-2 instruction, keep those compatibility handlers synchronous and do not add a nested event loop or `processEvents()` to their child wait. The wait blocks its calling thread (and therefore still freezes the GUI if dispatched on the GUI thread); callers that need a responsive UI must use the render queue. This limitation is explicit and unresolved until the control transport gains deferred replies.

### Realtime and behavior

The identity is read and advanced only on the document/UI thread. Queue preparation, journal merging, cancellation, file cleanup, and the legacy child wait remain outside the audio callback. No audio-thread allocation, lock, wait, syscall, or Qt signal is added. The serialized render inputs and child arguments are unchanged, so successful render audio remains governed by the existing audio-comparison test; only stale-document refusal, operation classification, and the full-cap undo result change.

### Round 2 verification

Add cases for reloading/replacing the project while a child runs, one-step range-bounce undo at the 100-step cap, and direct process/temp-directory cleanup assertions on cancellation. Run the focused render-job and compatibility tests, the four required source gates, and `python3 tests/doc-figures-gate.py`. The old synchronous contract is recorded as a limitation rather than claimed fixed because the current registry/transport cannot send a later reply.
