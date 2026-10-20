new-p16-89: FIXED
4172: FIXED
5080: FIXED

# Cluster new-p16-89 — same-file batch (src/tracks/InstrumentTrack.cpp)

All three findings were REAL on this branch. The regressions live in
`tests/src/tracks/TakeLaneCompPlaybackTest.cpp` (the existing comp-render test family). The specified log
folder `~/.local/share/zene-bunny-fix/qwen/runs/new-p16-89.r1/logs` is not writable from this session
(`touch ... -> Permission denied`, and `feature_check.py` fails at `logs.mkdir` with `EXIT=1`), so the logs
named below are in `<worktree>/.fix-logs/` (excluded from git via `.git/info/exclude`).

## Evidence

| Unit | Code evidence | Regression |
| --- | --- | --- |
| new-p16-89 | Before: `play()` built a local `clipVector clips` and `getClipsInRange()` grew it on the render thread. Now `include/InstrumentTrack.h:472` holds `m_playClips`; `src/tracks/InstrumentTrack.cpp:120-122` reserves it when a clip is added, and `:926-927` clears/reuses it per pass. The collection order (`getClipsInRange`) is unchanged. | `TakeLaneCompPlaybackTest::clipCollectionDoesNotAllocateOnTheAudioThread` (`:414`) wraps one arrangement pass in `AllocationProbe` + `RealtimeScope` and requires `tlAllocationCount == 0`. |
| 4172 | `compPlays()` was applied only to notes; `playClipControllers()` ran first. Now `src/tracks/InstrumentTrack.cpp:988-996` applies the same composite/audition gate before playing a clip's controller lanes, at the song tick of the pass playing (`cur_start + passBase`). `compPlays()` is `:870`. | `TakeLaneCompPlaybackTest::controllerLanesFollowTheComposite` (`:435`): a lane-1 controller on a clip comped to lane 0 writes no MIDI; auditioning lane 1 still writes it. |
| 5080 | The note gate computed `clip.start + startTimeOffset + note.pos`, omitting `passBase` (the loop pass, `:978,983`). Now `:1029-1030` includes `passBase`, so the comp lookup is the note's song tick on the pass playing. | `TakeLaneCompPlaybackTest::loopedClipCompReadsThePassPlaying` (`:464`): a clip looped every 96 ticks, comp lane 0 on `[0,192)` and lane 1 on `[192,384)`. Pass 2 (tick 144) plays; pass 3 (tick 240) must not, because the old lookup read the first-pass tick 48. |

## RED / GREEN proof (product patches in `.fix-logs/fix-*.patch`)

Each patch was reversed alone, the target rebuilt, and the test run. Reversal restores only that bug's
product hunk; the other two fixes stay in place, so each failure names the intended test.

| Unit | RED (reverse patch, rebuild, ctest) | GREEN (reapply, rebuild, ctest) |
| --- | --- | --- |
| new-p16-89 | `git apply -R .fix-logs/fix-1-behaviour.patch` EXIT 0; build EXIT 0; ctest EXIT **8**; `FAIL! clipCollectionDoesNotAllocate... Actual (test::tlAllocationCount): 1 Expected: 0` (`.fix-logs/proof2-bug1-test-red.log`) | apply EXIT 0; build EXIT 0; ctest EXIT **0** (`.fix-logs/proof2-bug1-test-green.log`) |
| 4172 | `git apply -R .fix-logs/fix-2.patch` EXIT 0; build EXIT 0; ctest EXIT **8**; `FAIL! controllerLanesFollowTheComposite Actual (port->outputEventsWritten()): 1 Expected: 0` (`.fix-logs/proof2-bug2-test-red.log`) | apply EXIT 0; build EXIT 0; ctest EXIT **0** (`.fix-logs/proof2-bug2-test-green.log`) |
| 5080 | `git apply -R .fix-logs/fix-3.patch` EXIT 0; build EXIT 0; ctest EXIT **8**; `FAIL! loopedClipCompReadsThePassPlaying 'playFrom(track, 240).isEmpty()' returned FALSE` (`.fix-logs/proof2-bug3-test-red.log`) | apply EXIT 0; build EXIT 0; ctest EXIT **0** (`.fix-logs/proof2-bug3-test-green.log`) |

The full per-bug patch for new-p16-89 is `.fix-logs/fix-1.patch` (header + constructor + `play()`); the
behavioural RED proof reverses `.fix-logs/fix-1-behaviour.patch` (the two `.cpp` hunks), because the header
member is inert storage once `play()` no longer uses it.

## Build and test commands

| What | Command | EXIT |
| --- | --- | --- |
| Test target build | `CCACHE_DISABLE=1 cmake --build build --target TakeLaneCompPlaybackTest -j"$(nproc)" > .fix-logs/build-12-final-tests.log 2>&1` | 0 |
| Final family (6 tests) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(NotePlaybackTest\|TakeLaneCompPlaybackTest\|PatmanLoadTest\|HeadlessGuiGuardTest\|ExprSynth\|ExprSynthAllocation)$' --output-on-failure > ../../.fix-logs/test-8-final-family.log 2>&1` | 0 (6/6 passed) |
| `zene` rebuild (header changed) | `CCACHE_DISABLE=1 cmake --build build --target zene -j"$(nproc)" > .fix-logs/build-10-zene.log 2>&1` | 0 |

`NotePlaybackTest`, the comp family's neighbours and both ExprSynth tests were built and run after the last
edit; each passed (see the 6/6 log above). The missing `monstro`/`nes`/`tripleoscillator` modules were built
for the test run (`build-7-test-plugins.log`, EXIT 0), so `NotePlaybackTest` was not passing on a dummy
instrument.

## Gates

`feature_check.py` could not run: `logs.mkdir` on the sibling `zene-qwen-new-p16-89-feature-check` is outside
the write jail and raises `PermissionError`; `.fix-logs/feature-check-attempt.log`, EXIT **1**. Every gate it
runs was run directly instead:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh > .fix-logs/gate3-fork-sources.log 2>&1` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > .fix-logs/gate3-all-sources-reproduce.log 2>&1` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > .fix-logs/gate3-no-upstream-regression.log 2>&1` | 0 |
| check-namespace | `tests/scripted/check-namespace > .fix-logs/gate3-check-namespace.log 2>&1` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh > .fix-logs/gate3-no-tautology.log 2>&1` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py > .fix-logs/gate3-doc-figures.log 2>&1` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > .fix-logs/gate3-complexity.log 2>&1` | 1 (red on the train) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > .fix-logs/gate3-file-length.log 2>&1` | 1 (red on the train) |

The two ratchets are red on the train on purpose. Filtering their `REGRESSION:` lines for this branch's
changed files finds none (`.fix-logs/ratchet-filter.log`, both EXIT 0): neither `include/InstrumentTrack.h`,
`src/tracks/InstrumentTrack.cpp` nor `tests/src/tracks/TakeLaneCompPlaybackTest.cpp` (491 lines) is listed.
No gate baseline or limit was changed.

## Files committed

- `include/InstrumentTrack.h`
- `src/tracks/InstrumentTrack.cpp`
- `tests/src/tracks/TakeLaneCompPlaybackTest.cpp`
- `tests/upstream-modifications.txt` (seven lines: one per product/test change above)
- `docs/qwen-fixes-2026-10/cluster-new-p16-89.md`

## Self-check

- **Each new test FAILS with the product change reverted and PASSES restored.** Yes — the RED/GREEN table
  above gives the exact reverse/apply commands, build exits and ctest exits (8 then 0 for each unit).
- **The test is deterministic.** It injects the tick positions and drives `InstrumentTrack::play` directly;
  no sleeps, wall-clock timing or races. Allocation is measured with the repository's `AllocationProbe`
  (operator-new counter) inside `RealtimeScope`; the loop test asserts note-handle keys at fixed song ticks.
- **The fix is at the cause and complete.** `getClipsInRange()` is unchanged and still used for its sorted
  order; only `InstrumentTrack::play`'s storage moved to a preallocated member. `compPlays()` has exactly two
  product call sites in `play()` (the new controller gate and the existing note gate); both now pass the
  same song tick, including `passBase`. `playClipControllers()`' only product caller is that gated call.
  `PatternTrack::play` is a different track type with its own collection and is outside this finding.
- **No collateral damage.** The clip set and its order are exactly `getClipsInRange()`'s, so overlapping
  clips are handed to the note loop as before. No file format, project-load or save path changed, and no
  new limit/check was added. A track with no composite still takes the unchanged `compPlays() == true` path.
- **Realtime, undo, commands.** The only new allocation is `m_playClips.reserve()` on the control thread
  when a clip is added; the render pass only clears and inserts into already-reserved storage. No new lock,
  log, signal or I/O on the audio thread, no undo step, no control command, so the command snapshot is
  untouched.
