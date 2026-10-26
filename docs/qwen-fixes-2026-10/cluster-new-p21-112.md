Verdict: FALSE-POSITIVE (new-p21-112)

# Cluster new-p21-112: `recordCreatedTracks` undo with a null or stale container

new-p21-112: FALSE-POSITIVE

## Decision and evidence

The finding says the undo lambda at `src/core/ControlTrackCreation.cpp:137` can dereference a null or destroyed
container. It cannot be reached by any product path on this branch. The only way to reach it is a caller passing a
null container to the public API, which the header's own contract forbids.

### 1. The intended contract says the container is always the song or the PatternStore

`include/ControlTrackCreation.h:43-47` documents `recordCreatedTrack(track, container)` as the step for a track
"created in @a container rather than the song: a Pattern Editor copy lives in the PatternStore". The
`CreatedTrack` comment at `include/ControlTrackCreation.h:49` says the container is "the container it was created in
(the song, or the PatternStore)". A null container is therefore a contract violation by the caller, not a state the
undo step is expected to handle.

### 2. Every production caller passes a live song or PatternStore

- Six `recordCreatedTrack(track)` callers use the one-argument overload, which passes `Engine::getSong()`
  (`src/core/ControlTrackCreation.cpp:103-105`): `src/core/ControlCommandsBrowserMedia.cpp:323`,
  `src/core/ControlCommandsAudioSlice.cpp:259`, `src/core/ControlCommandsAudioAnalysis.cpp:118`,
  `src/core/ControlCommandsClipImportFile.cpp:294`, `src/core/ControlCommandsRecorder.cpp:112`,
  `src/core/ControlCommandsVideo.cpp:176`.
- `src/gui/editors/TrackContainerView.cpp:627` records only when `m_tc == Engine::getSong()`.
- `src/gui/editors/PatternEditor.cpp:162` captures with `song` (`Engine::getSong()`, non-null as used on the line
  above). `src/gui/editors/PatternEditor.cpp:209` captures with `model()`, which is `m_ps` set from the constructor
  argument at `src/gui/editors/PatternEditor.cpp:50-52`, the engine's PatternStore.

No call site passes `nullptr`. Searching `src` and `tests` for `recordCreatedTrack(...nullptr` and
`captureCreatedTrack(...nullptr` returns nothing.

### 3. The song and PatternStore outlive every undo step that can run

- `src/core/Engine.cpp:70` creates the journal, `:72` the song, `:74` the PatternStore.
- `src/core/Engine.cpp:96-123` tears down in this order: `stopAllJournalling()` at `:101`, PatternStore deleted at
  `:110`, journal deleted at `:121`, song deleted at `:123`. Journalling stops before the containers are freed, and
  no command or undo can run after `Engine::destroy()` begins.
- `src/core/ProjectJournal.cpp:399-401` (`clearJournal`) only clears the undo and redo stacks. It does not run the
  stored actions, so a stored `Recorded::container` is never dereferenced by a clear.
- `src/core/ControlReversibility.cpp:147-151`: `addUndoStep` returns before storing anything when the journal is
  null. Nothing can be recorded in the window where the song or PatternStore might be null and the journal is live.

A destroyed container therefore cannot be reached by a later undo, and the one-argument overload's
`Engine::getSong()` is non-null whenever the journal is live.

### 4. The finding's suggested guard would hide a programming error

Skipping a null-container entry inside the undo lambda (the finding's fix) would make a step that does nothing still
consume an undo entry. That is a dishonest undo under the "undo stays one honest step" rule. Silently dropping the
record at capture time would be the same problem. No change is made.

## Commands and exit codes

No product file changed, so there is no RED or GREEN run and no regression test was added (see Self-check).

Logged under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-112.r1/logs/`:

- `rg -n "recordCreatedTracks?\(.*nullptr|captureCreatedTrack\(.*nullptr" src tests` (evidence-2.log): no matches,
  `EXIT=1`.
- `rg -n "recordCreatedTrack\(|captureCreatedTrack\(" src include tests` (evidence-1.log): the caller list above.
- `rg -n "stopAllJournalling|deleteHelper\( &s_(patternStore|projectJournal|song)|s_projectJournal = new|s_song = new|s_patternStore = new" src/core/Engine.cpp`
  (evidence-3.log): the ordering at lines 70-74 and 101-123, `EXIT=0`.
- Gates and submit check: see Self-check.

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it: N/A. This is a FALSE-POSITIVE closure
  with no product change, so there is nothing to revert. The independent QA can reproduce the claim by calling
  `control::recordCreatedTrack(track, nullptr)` followed by undo; that is the only path that crashes, and it is a
  contract violation rather than a product path.
- The test is deterministic: N/A (no test added).
- The fix is at the cause and complete: N/A (no fix). The cause the finding names is not reachable by a product path.
- No collateral damage: N/A (no code change). The current callers in the evidence list keep their behaviour.
- Verdict evidence (code or contract): see sections 1-3 above (`include/ControlTrackCreation.h:43-49`,
  `src/core/Engine.cpp:70-123`, `src/core/ProjectJournal.cpp:399-401`, `src/core/ControlReversibility.cpp:147-151`).
