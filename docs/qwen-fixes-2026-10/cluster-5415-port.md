FIXED

## Verdict and cause

**REAL.** The render thread reads the frozen take in `Track::playFrozenTake()`
(`src/core/Track.cpp:1543`, buffer read at `src/core/Track.cpp:1567`) from both
`InstrumentTrack::play` (`src/tracks/InstrumentTrack.cpp:945`) and
`SampleTrack::play` (`src/tracks/SampleTrack.cpp:160`). `AudioEngine::renderNextPeriod()`
holds `m_changeMutex` for the whole period (`src/core/AudioEngine.cpp:1224`).

`freezeTo()` (`src/core/Track.cpp:1362`), `unfreeze()` (`src/core/Track.cpp:1414`) and
`clearFrozenTake()` (`src/core/Track.cpp:1435`) already publish their writes under
`requestChangesGuard()` (the same recursive mutex: `src/core/AudioEngine.cpp:1557`).
`loadFrozenTake()` did not: it wrote `m_frozen.path`/`m_frozen.startTicks`/... first and
assigned `m_frozenBuffer` before taking the guard, so a project load while audio played
could expose a take with a new path/window and an old or null buffer, or a decoded buffer
with half-written metadata. This is the port branch's remaining cluster-5415 writer.

The fix stages the take's metadata and decoded buffer in control-thread locals and
publishes `m_frozen`, `m_frozenBuffer`, the tempo-map fallback and the voice capacity in
one guarded section (`src/core/Track.cpp:1483-1506`). The file read remains outside the
lock. Parse failures leave the already-cleared take untouched; a missing/empty take file
still keeps the frozen metadata, publishes a null buffer and reports the same
`collectError` text as before.

## Regression test matrix

- **Normal path:** `FrozenTakeHandoffTest::loadedTakeIsPublishedUnderTheRenderLock`
  (`tests/src/core/FrozenTakeHandoffTest.cpp:282`) restores a project-style `<track>`
  element through `Track::loadSettings`; the take is not visible at the post-decode
  boundary, and after the guard is released it is frozen with its audio ready and the
  expected path/end tick.
- **Guard path:** the existing `Track::setFrozenTakeLockProbeForTest` seam parks the load
  after the file read and before `requestChangesGuard()`; the test asserts the take is
  not visible then, and also while the test itself holds the render guard.
- **Failure path:** parse-invalid and inverted-region elements still leave no take and
  unmute the clips the file recorded; the missing-file path still publishes frozen
  metadata with a null buffer. These are unchanged and covered by
  `FrozenTakeRoundTwoTest` and `FreezeRegionRepairTest` in the family run.
- **Exhaustion/reconfiguration:** take voice capacity is still recomputed from the loaded
  take and the current loop inside the guarded publication; pooled-voice tests remain
  green.
- **Audio-thread constraints:** no render-path code changed; the load runs on the
  loading thread, and the only publication visible to the render thread is now inside
  the same mutex the period holds. The test seam is `LMMS_TESTING`-only.
- **Platforms:** built and run on this Linux host with the configured `build` tree,
  Qt 6.12.0.

## RED / GREEN and reverse-patch proof

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/5415-port.r8/logs/`.

| Step | Exact command | Exit |
|---|---|---|
| RED build (seam + test, buggy load) | `cmake --build build --target FrozenTakeHandoffTest -j4` (`build-2-red.log`) | 0 |
| RED run | `ctest -R '^FrozenTakeHandoffTest$' --output-on-failure` in `build/tests` (`test-2-red.log`) | **8** (`!publishedBeforeGuard` returned FALSE at `FrozenTakeHandoffTest.cpp:332`) |
| GREEN build | `cmake --build build --target FrozenTakeHandoffTest -j4` (`build-3-green.log`) | 0 |
| GREEN run | `ctest -R '^FrozenTakeHandoffTest$' --output-on-failure` in `build/tests` (`test-3-green.log`) | 0 |
| Capture product diff | `git diff -- src/core/Track.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/5415-port.r8/logs/fix.patch` | 0 |
| Reverse product diff | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/5415-port.r8/logs/fix.patch` | 0 |
| Rebuild without fix | `cmake --build build --target FrozenTakeHandoffTest -j4` (`build-4-red-reverse.log`) | 0 |
| Run without fix | `ctest -R '^FrozenTakeHandoffTest$' --output-on-failure` in `build/tests` (`test-4-red-reverse.log`) | **8** (`!publishedBeforeGuard` returned FALSE) |
| Restore product diff | `git apply /home/kruzzzzy/.config/kads/zene/state/runs/5415-port.r8/logs/fix.patch` | 0 |
| Rebuild with fix | `cmake --build build --target FrozenTakeHandoffTest -j4` (`build-5-green-restored.log`) | 0 |
| Run with fix | `ctest -R '^FrozenTakeHandoffTest$' --output-on-failure` in `build/tests` (`test-5-green-restored.log`) | 0 |
| Product binary | `cmake --build build --target zene -j4` (`build-6-zene.log`) | 0 |
| C++ freeze family | `ctest -R 'Frozen|Freeze' -E 'Transcript' --output-on-failure` in `build/tests` (`test-8-family-cpp.log`) | 0 (6/6 passed) |
| Gate table | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gate-2-final.log`) | see log |

The test is event-driven: it waits on the product's own lock-boundary probe and the
engine guard, not on a sleep. The only bounded wait (5 s) is a hang guard.

## Realtime, undo and commands

No render-path code changed: `loadFrozenTake` runs on the control/loading thread, reads
the file before the guard, and publishes under `requestChangesGuard`. No control command,
command id, result schema or undo step was added or changed. Old projects still read the
same `frozenAudio` / `frozenStart` / `frozenEnd` / `frozenMuted` / `frozenRegion`
attributes, and a rejected take still resets exactly as before.

## Registration

`src/core/Track.cpp` is an inherited file already declared in
`tests/upstream-modifications.txt` for cluster 5415; its reason now also names the guarded
`loadFrozenTake` publication. No new product file was added.

## Self-check

- **The regression fails with the product change reverted and passes with it:** reversed
  `src/core/Track.cpp` run -> exit **8**, `!publishedBeforeGuard` returned FALSE; restored
  run -> exit **0**. Commands and logs are in the table above.
- **The test is deterministic:** it uses `Track::setFrozenTakeLockProbeForTest` (the
  product's lock-boundary event) and the engine's change guard; no sleeps or wall-clock
  ordering. The 5 s `wait_for` is only a hang guard.
- **The fix is at the cause and complete:** the four writers of the take state are
  `freezeTo`, `unfreeze`, `clearFrozenTake` and `loadFrozenTake`; all now publish under
  `requestChangesGuard`. Both playback callers (`InstrumentTrack::play`,
  `SampleTrack::play`) reach the take through `playFrozenTake`/`frozenTakeSilencesSource`,
  which run inside `renderNextPeriod`'s held `m_changeMutex`. `loadFrozenTake` has one
  caller (`Track::loadTrack`, `src/core/Track.cpp:866`).
- **No collateral damage:** the saved attributes, reset-on-absence, invalid-window refusal
  and missing-file warning are unchanged; the full C++ freeze family (6 tests) passes.
- **Verdict evidence:** REAL, with the reader/writer lines in "Verdict and cause"; this is
  not an intended `AGENTS.md` "Verified facts" behaviour.
- **Sibling commands:** this fix adds or edits no control command, command id or result
  schema. The command files reported by the completion check come from the train's
  existing changes since the gate base, not from this commit; no `ControlCommands*`,
  `ControlReversibilityTable*` or `ControlRegistry*` path differs between `HEAD~1` and
  `HEAD`, so no command sibling is affected. The only product path changed by this fix is
  `src/core/Track.cpp`.

## Other findings

`ctest -R 'Frozen|Freeze'` (the full family) reports `ControlFreezeCommandsTranscript` as
**Failed** while its own output says `PASS: no audible instrument in this build: Skipped,
never Passed` (`test-7-family.log`). The test exits 77 when the build has no audible
instrument, but that test has no `SKIP_RETURN_CODE 77` property (the two sibling
transcripts do). This is an existing test-registration issue, unrelated to the frozen-take
publication fix; the C++ family run excludes nothing else and passes 6/6.
