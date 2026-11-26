FALSE-POSITIVE

# Cluster new-p16-218 — "Warped audio cells use the song tempo instead of their scene's tempo"

## Decision: FALSE-POSITIVE

The finding says audio preparation receives `song->getTempo()` "so warping can use the wrong target
tempo". The song tempo is not the wrong target: it is the tempo the transport and every other cell in
the row actually run at, and it is the tempo the warp is documented to use. A scene's tempo override
has no playback effect anywhere in this tree — it is modelled, validated, saved and reported, and
nothing applies it to the clock. Warping a cell to the override while the transport stayed at the song
tempo would put that cell out of sync with its own row, which is the defect the requested change would
introduce, not a fix.

## Evidence (this session; line numbers on this branch, base 3436a808b)

**What the code does.** `src/core/SessionPlayback.cpp:217` passes `song->getTempo()` to
`sessionaudio::prepare`; that is the only call site of `prepare` outside tests
(`include/SessionAudioCells.h:102` declares it, `src/core/SessionAudioCells.cpp:248` defines it, and
`:260` bakes `bakeParamsFor(slot, tempo)` — the tempo is the warp *target*, so a 60 bpm scene override
would double the clip's length). `publishSessionCell` (`:204`) has exactly two callers —
`publishSessionSlotContent` (`:228`, `:237`) and `syncSessionLauncher`
(`src/core/SessionTransitions.cpp:107`) — so there is no second path that could disagree.

**Nothing applies a scene's tempo override to playback.** `Scene::tempoEnabled()` / `Scene::tempo()`
(`include/SessionModel.h:223-226`) are read in only three places, none of them a render or clock path:

- `src/core/ControlCommandsSessionShared.h:341-342` — the state reply (`tempo_enabled`, `tempo`);
- `src/core/SessionClip.cpp:250` and `:275` — save and load of the `<scene tempo=…>` attribute;
- `include/SessionModel.h:248` — `isModified()`.

`session.set_scene` writes the `Scene` model only (`src/core/ControlCommandsSession.cpp:225`
`applySceneOverrides`, registered at `:420`, handler `:455-470`: capture, edit, one journal
transaction), and `session.launch_scene` (`src/core/ControlCommandsSessionLaunch.cpp:187-241`) launches
the row's cells and sets the scene-launch state — neither touches `Song`'s tempo.

**The contract says song tempo.** `docs/SESSION-AUDIO-RECORD.md:69-73`: the warp is "pitch-preserving
WSOLA to the song's tempo", with the stated limit "the warp follows the song tempo as of the last
publish". `docs/050-features/Q050-013.md:30` (the feature record that introduced the warp), under
**Known limits**: "The warp uses the song tempo at publish time (every launch publishes); a tempo change
under a cell that already plays applies at its next launch." The override itself is documented as not
applied: `docs/SESSION-PLAYBACK-SLICE0.md:100` "**Scene tempo / time-signature overrides** are not
applied on launch", and `docs/SESSION-SCHEDULER.md:354-355` lists "Scene launch and per-scene
tempo/time-signature application (SPEC §4.1)" as out of scope. `tests/src/core/SessionCommandArgsTest.cpp:267`
records the same fact in the test for the override's range: it was "reported as an override that nothing
could play".

**The behaviour is pinned by a passing test.** `warpFollowsTheSongTempoKeepingPitch`
(`tests/src/core/SessionAudioCellPlaybackTest.cpp:256-283`) checks the baked length and the rendered
pitch at 60 and 240 bpm against a 120 bpm clip, through the real launch path. Green this session (below).

## Behavioural probe (temporary, reverted before commit)

A FALSE-POSITIVE is only worth the closure if the behaviour is observed, so I added one temporary slot
to `SessionAudioCellPlaybackTest` (`zzProbeSceneTempoOverrideAndTheWarp`): song tempo 120, a 1 s 400 Hz
warped clip (`warp_bpm` 120) in cell (0,0), `session.set_scene` `{scene: 0, tempo: 60}` accepted, then
`session.launch_slot` and a read of the clip the scheduler hands the renderer, then
`session.launch_scene` and a read of the transport tempo. The probe asserts nothing about the finding —
it prints — so it cannot pass or fail on the question; it records what the engine does.

`logs/probe-1-scene-tempo.log`, EXIT=0:

```
QDEBUG : ...::zzProbeSceneTempoOverrideAndTheWarp() PROBE sceneOverride enabled=1 tempo=60.0 songTempo=120.0
QDEBUG : ...::zzProbeSceneTempoOverrideAndTheWarp() PROBE publishedClipFrames=44100 sourceFrames=44100 warpTarget120=44100 warpTarget60=88200 songTempoAfter=120.0
QDEBUG : ...::zzProbeSceneTempoOverrideAndTheWarp() PROBE songTempoAfterSceneLaunch=120.0
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 514ms
```

Read: the override is stored and enabled (60 bpm against a 120 bpm song), the published clip is 44100
frames — the 120 bpm bake (a 60 bpm warp target would be 88200) — and after a full scene launch the
transport is still at 120. The cell is warped to the tempo the transport runs at; the override changes
nothing to warp to.

The probe was removed with `git checkout -- tests/src/core/SessionAudioCellPlaybackTest.cpp`
(EXIT=0, `git status --porcelain` empty), and the tree was rebuilt from the reverted sources
(`logs/build-2-clean.log`, BUILD_EXIT=0), so the binary that ran the family test below is the clean
tree's.

## Test matrix (what was checked instead of a fix)

| Path | What it would have to show | Result |
|---|---|---|
| normal path | a warped audio cell in a scene with a tempo override, launched: the clip handed to the renderer | probe: 44100 frames = the song-tempo bake, not 88200 |
| failure path | `prepare` returning a null clip (unreadable file, no track) is untouched by this finding; the tempo argument only feeds `bakeParamsFor` | `src/core/SessionAudioCells.cpp:248-262`, unchanged |
| exhaustion path | the 256 settings-variant cap is per (clip, settings) — the tempo enters the variant key, so a scene tempo would only add variants, never free them | `theVariantLimitCountsVariantsNotLoadedClips`, `src/core/SessionAudioCells.cpp:260` |
| reconfiguration path | tempo change / scene edit under a playing cell | `src/core/SessionTransitions.cpp:107` republishes on the next launch; the documented limit (`Q050-013.md:30`) |
| audio-thread constraints | nothing new added on the audio thread by this report: no product file changed | n/a — `git diff` for the commit is docs only |
| platform | no code changed, so no Qt 5 / MSVC consequence | n/a |

## Commands, with the exit codes I saw

All under worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-218`,
logs under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-218.r1/logs`.

| # | Command | Exit |
|---|---|---|
| 1 | `cmake --build build --target SessionAudioCellPlaybackTest -j4 > …/logs/build-1-probe.log 2>&1` | 0 (`EXIT=0` in the log) |
| 2 | `cd build/tests && ./SessionAudioCellPlaybackTest zzProbeSceneTempoOverrideAndTheWarp > …/logs/probe-1-scene-tempo.log 2>&1` | 0 |
| 3 | `git checkout -- tests/src/core/SessionAudioCellPlaybackTest.cpp` | 0 |
| 4 | `cmake --build build --target SessionAudioCellPlaybackTest zene -j4 > …/logs/build-2-clean.log 2>&1` | 0 (`BUILD_EXIT=0`) |
| 5 | `cd build/tests && ctest -R SessionAudioCellPlaybackTest --output-on-failure > …/logs/test-1-SessionAudioCellPlaybackTest.log 2>&1` | 0 (`TEST_EXIT=0`, 1/1 passed) |
| 6 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > …/logs/gates-1.log 2>&1` | 0 (`GATES_EXIT=0`, `RESULT: PASS`) |
| 7 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p16-218` (before the report) | 1, 4 FAIL — all of them "missing report/commit"; the gates item PASSed |
| 8 | same `feature_check.py`, after the report was committed (`logs/gates-2.log`) | 0, `RESULT: PASS` (1 changed file) |
| 9 | same `submit_check.py`, after the report was committed (`logs/submit-2.log`) | 0, `RESULT: PASS - submit` |

Gates table (`logs/gates-1.log`): fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression
PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item),
file-length PASS (no new item), RESULT: PASS.

## Change

No product change, so no regression test to pin one: the verdict is that the requested change is the
defect. This commit adds this report only (`git show --stat HEAD`). The behaviour the finding calls
wrong is already covered by `warpFollowsTheSongTempoKeepingPitch`, which is green on this tree; adding
a second test that must pass without a product change would not prove anything a reviewer could use.

## Self-check

- **"The regression test FAILS with your product change reverted and PASSES with it."** There is no
  product change — FALSE-POSITIVE. The evidence is the observation above (probe, EXIT=0) and the code
  and contract references; `warpFollowsTheSongTempoKeepingPitch` is the existing test that pins the
  behaviour and it passes (`logs/test-1-…log`, TEST_EXIT=0).
- **"The test is deterministic."** The probe used no wall clock, sleep or thread: it drove the engine's
  periods itself (`AudioEngine::renderNextPeriod`, the file's existing `period()` helper) and read the
  scheduler's published clip and `Song::getTempo()`. The library test cited
  (`warpFollowsTheSongTempoKeepingPitch`) is likewise period-driven, with `transport.set_tempo` as the
  injected value.
- **"The fix is at the cause and complete."** Nothing was changed; the completeness question for the
  finding is answered by the single call site of `sessionaudio::prepare`
  (`src/core/SessionPlayback.cpp:217`) and its two callers (`:228/:237`, `SessionTransitions.cpp:107`),
  and by the fact that `Scene::tempo()` has no consumer outside save/load, state and `isModified()`.
- **"No collateral damage."** No file under `src/`, `include/` or `plugins/` is touched; old projects
  load and save exactly as before (`tests/src/core/SessionModelTest.cpp:200-206`,
  `ProjectVersionTest.cpp:275-276`, `ScenesUpconvertTest.cpp:275-276` were not modified and the gates
  pass). No limit or check was added.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract."** `src/core/SessionPlayback.cpp:217` with `docs/SESSION-AUDIO-RECORD.md:69-73` and
  `docs/050-features/Q050-013.md:30` (the warp's target is the song tempo, by design), plus
  `docs/SESSION-PLAYBACK-SLICE0.md:100` and `docs/SESSION-SCHEDULER.md:354-355` (the scene override is
  not applied on launch), plus the consumer list above.

## Other findings

- The scene tempo override has no playback effect at all (it is stored, validated, saved and reported,
  and nothing applies it). That is documented as the unimplemented per-scene tempo application
  (`docs/SESSION-SCHEDULER.md:354-355`), not a regression. When that feature is built, the audio bake
  must follow the tempo the transport actually takes, which is why warping to the override now would
  be wrong; that work is the feature's, not this bug's.
- `docs/SESSION-SCHEDULER.md:354` is stale in one respect: `session.launch_scene` now exists
  (`src/core/ControlCommandsSessionLaunch.cpp:187`) — the row-launch half is implemented, the
  per-scene tempo/time-signature application is not. Left alone here.
