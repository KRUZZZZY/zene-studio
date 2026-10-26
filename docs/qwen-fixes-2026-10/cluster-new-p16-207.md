Verdict: FIXED (the stale-index half is real; the null-cast half is FALSE-POSITIVE on this tree)

# Cluster new-p16-207: single-clip index in SampleTrack::play

Finding: "Invalid single-clip index can crash sample playback" (`src/tracks/SampleTrack.cpp`, `play()`, `_clip_num >= 0`).

## Decision

The finding makes two claims. They get different verdicts.

1. **"`getClip(_clip_num)` can return null, so `sClip` is dereferenced null."** FALSE-POSITIVE on this tree.
   - `Track::getClip()` never returns null for an index past the end. It builds a clip and returns it: `src/core/Track.cpp:1091-1100` (`return createClip( clipNum * TimePos::ticksPerBar() );`).
   - `SampleTrack::createClip()` always returns a `SampleClip`: `src/tracks/SampleTrack.cpp:318-323`.
   - Every clip that enters a SampleTrack goes through that same `createClip()`. Project load does it in `claimClipOrPreserveChild` (`src/core/Track.cpp:183-194`). The other construction sites build a `SampleClip` for a sample track: `src/core/StemTrackBuilder.cpp:92`, `src/core/SampleClip.cpp:330`. Clips of other types are built on their own tracks: `MidiClip` on `InstrumentTrack` (`src/gui/editors/PianoRoll.cpp:5850-5891`, `firstTrack` is an `InstrumentTrack*`), `AutomationClip` on the global automation track (`src/core/AutomationClip.cpp:1439-1452`), `PatternClip` on `PatternTrack` (`src/tracks/PatternTrack.cpp:149`).
   - So the `dynamic_cast` cannot fail for a SampleTrack. The code already relies on that invariant at `src/tracks/SampleTrack.cpp:223-227`, which dereferences the same kind of cast with no check. I did not add a null check, so there is no dead branch to cover.

2. **"A stale or out-of-range nonnegative index reaches `getClip()` and misbehaves."** REAL, and the fix is the bounds check. The finding's "crash" is not the mechanism, though. An out-of-range index does not crash. `getClip()` builds a new `SampleClip` at that index and appends it to the track (`src/core/Track.cpp:1016-1018`, `addClip`, via the `Clip` constructor `src/core/Clip.cpp:73`). It also `printf`s (`src/core/Track.cpp:1097`). That runs on the audio thread (`SampleTrack::play` is called from `PatternStore::play` per period). The track then schedules the new clip for playback. The defect is a model mutation, an allocation and a log on the audio thread, plus a clip the GUI can see appear mid-play. Those are the realtime and data-race hazards that the finding's "crash" describes at a higher level.
   - The sibling `InstrumentTrack::play` already has the guard, with the same reasoning in its comment: `src/tracks/InstrumentTrack.cpp:951-956` (`if (_clip_num < numOfClips())`, "getClip() would construct a MidiClip ... on the audio thread").

## The fix

`src/tracks/SampleTrack.cpp:185-190`: before `getClip(_clip_num)`, `if( _clip_num >= numOfClips() ) { return false; }`, mirroring `InstrumentTrack::play`. A stale or past-the-end index plays nothing and creates nothing. An index the track holds behaves exactly as before. `play()` returning `false` matches `InstrumentTrack` for the same case.

No behaviour changes for valid indices. For an out-of-range index the removed side effect is a new empty clip. The new clip has no sample, so I expect no audible output from the old path, but I did not measure it.

## Regression test

`tests/src/tracks/SampleClipWarpTest.cpp`, slot `aSingleClipIndexTheTrackDoesNotHoldCreatesNothing` (line 655). Matrix:
- an index one past the end (`1`) and a far one (`7`) both return `false` (`!pastEnd`, `!farPast`);
- `numOfClips()` stays `1` after both (no clip is built);
- the two out-of-range calls allocate nothing (`test::tlAllocationCount == 0`, via the existing `AllocationProbe`).

Not in this test: the index the track holds (`0`). That call queues a play handle, and in this render-only engine the only synchronous way to drop it, `AudioEngine::clearNewPlayHandles()`, does not delete handles that are not pooled, so asserting it would leak a handle in the test. The guard returns only when `_clip_num >= numOfClips()`, so an index the track holds runs the same code as before (the diff is the evidence). An existing test should cover that path; I did not verify which one does.

Determinism: no clock, no sleep, no thread. `Engine::init(true)` is render-only, so no audio thread adopts the handles a wrong pass would queue. `clearNewPlayHandles()` runs on the test thread and is the same synchronous seam `tests/src/core/NoteCurvesAcrossTest.cpp:332` uses. The earlier draft used `drainPlayHandles()`, which waits 4×30 ms with `QTest::qWait`; I removed that wait for this reason.

Why this file: my first draft went into `tests/src/tracks/SampleClipWindowTest.cpp`. That file is 494 code lines at HEAD, and the file-length gate fails any file over 500 code lines, so the 20 added lines pushed it to 514 and the gate failed (`NEW: new file over 500 lines: tests/src/tracks/SampleClipWindowTest.cpp (514)`). I reverted that draft and moved the test to `SampleClipWarpTest.cpp` (392 code lines), which already drives `SampleTrack::play` on a real clip. No new files were created, so no registration is needed.

## Evidence (exact commands and EXIT codes)

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-207.r1/logs/`. Run from `build/tests` for ctest. `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp` is set for every build: the default ccache temp dir under `dsh/home` returns "Permission denied" in this sandbox, and the first build failed with that (`build-red-1.log`, EXIT=2). Nothing in the code caused it.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Build, first try (ccache temp dir) | `cmake --build build --target zene SampleClipWindowTest -j4` | 2 | ccache permission error, not a compile failure |
| Build, retry | same, with `CCACHE_TEMPDIR` | 0 | ok |
| RED on first draft (window test, fix absent) | `ctest -R '^SampleClipWindowTest$'` | 8 | `'!pastEnd' returned FALSE`, `SampleClipWindowTest.cpp(776)`; `called Track::getClip( 1 ), but Clip 1 doesn't exist` (test-red-1.log lines 24-26) |
| GREEN on first draft (fix present) | same | 0 | 100% |
| 4b revert (first draft) | `git apply -R fix.patch` | 0 | |
| 4b RED (first draft) | rebuild, `ctest -R '^SampleClipWindowTest$'` | 8 | fails as above |
| 4b restore (first draft) | `git apply fix.patch` | 0 | |
| 4b GREEN (first draft) | rebuild, ctest | 0 | 100% |
| Relocated test, build with fix reverted, first try | `cmake --build build --target zene SampleClipWarpTest -j4` | 2 | compile error: `asNumber` not declared at `SampleClipWarpTest.cpp:674`; the ctest after it (EXIT=0) ran a stale binary and is discarded |
| Relocated test, compile fix (`N(...)`) | rebuild | 0 | |
| Relocated draft (with in-range call), RED | `git diff -- src/tracks/SampleTrack.cpp > fix.patch && git apply -R fix.patch`; rebuild; ctest `^SampleClipWarpTest$` | build 0; ctest **8** | `'!pastEnd' returned FALSE`, `SampleClipWarpTest.cpp(671)`; 17 passed, 1 failed (`test-red-warp-2.log`) |
| Relocated draft, GREEN | `git apply fix.patch`; rebuild; ctest | build 0; ctest **0** | 100% (`test-green-warp.log`) |
| **FINAL RED** (test without in-range call; fix reverted via `git diff HEAD~1 HEAD -- src/tracks/SampleTrack.cpp > fix.patch && git apply -R fix.patch`) | rebuild `zene SampleClipWarpTest`; `ctest -R '^SampleClipWarpTest$'` | build 0; ctest **8** | `'!pastEnd' returned FALSE`, `SampleClipWarpTest.cpp(668)`; 17 passed, 1 failed (`test-red-v3.log`) |
| **FINAL GREEN** (`git apply fix.patch`; rebuild; ctest) | same | build 0; ctest **0** | 100% tests passed, 1 of 1 (`test-green-v3.log`) |
| **Family** (after the final fix) | rebuild `SampleClipRubberBandTest SampleClipStretchTest SampleClipWarpModesTest TakeLaneCompPlaybackTest EngineRenderBenchmarkTest NoteBackgroundAudioTest SampleClipWarpModeMenuTest SampleClipWindowTest`; `ctest -R '^(SampleClip.*\|TakeLaneCompPlaybackTest\|EngineRenderBenchmarkTest\|NoteBackgroundAudioTest\|SampleClipWarpModeMenuTest)$'` | build 0; ctest **0** | **9 of 9** tests executed, 100% passed (`test-family-4.log`) |
| Stability | `ctest -R '^SampleClipWarpTest$' --repeat until-fail:3` | 0 | 100%, no failure (`test-repeat-v3.log`) |

A first family run (EXIT=8, `test-family-1.log`) listed `SampleClipRubberBandTest` as `Not Run`, because I had not built it. I built it and reran (EXIT=0, 9/9, `test-family-2.log`). The final family run is `test-family-4.log`.

Gate:
- Official command `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: EXIT=1, a `PermissionError` before any gate ran. The script writes its logs to `<worktree>/../zene-qwen-new-p16-207-feature-check`, which is outside the write-jail (`gate-feature-1.log`).
- So I ran a copy of the same script (`runs/new-p16-207.r1/gate/feature_check_logdir.py`) with only the log directory changed, pointing at `runs/new-p16-207.r1/logs/feature-check`. The gate logic, the limits and the baselines are the same ones in `qwen/gate-baselines` (copied in). Command: `python3 -I .../gate/feature_check_logdir.py <worktree>`. EXIT=0 (`gate-feature-4.log`, final tree):
  - PASS fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures;
  - PASS complexity ("no new item in your files");
  - PASS file-length ("no new item in your files") — this is the one that failed on the first draft (`gate-feature-2.log`, EXIT=1);
  - RESULT: PASS.

Files committed (explicit paths): `src/tracks/SampleTrack.cpp`, `tests/src/tracks/SampleClipWarpTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p16-207.md`.

No new control commands, no header changes, no new files. Undo: the fix changes only what `play()` does for an out-of-range index, and the removed clip creation was an audio-thread side effect, so undo history is unaffected.

## Notes and limits

- I ran only the playback family (`SampleClip*`, `TakeLaneCompPlaybackTest`, `EngineRenderBenchmarkTest`, `NoteBackgroundAudioTest`, `SampleClipWarpModeMenuTest`). I did not run the full `ctest`, and I did not run the pattern-store tests. The change only affects `play()` with `_clip_num >= 0` on a SampleTrack whose index is past its clips, so a pattern-store test that depends on a sample track growing a clip during play would be the one to watch. I found no such test by searching, but I did not run one.
- `Track::getClip()` itself is unchanged, so it still builds a clip for an out-of-range index when another caller passes one. One such caller is `src/core/TrackContainer.cpp:539` (`level.appendClip(track->getClip(clipNum))`). I did not audit that caller, and it is outside this finding.

## Self-check

`submit_check.py` (`logs/submit-check-1.log`, EXIT=1) passes the commit, clean-tree, report and tests-changed rows. It fails one row, the gate, for the reason below.

**Gate row: FAIL, environment only.** `submit_check.py` runs `feature_check.py` unchanged, and that script creates its log directory at `<worktree>/../<name>-feature-check`, which is `projects/zene-studio-program/zene-qwen-new-p16-207-feature-check`. That directory's parent is not writable from this worktree (write-jail): `touch .../zene-studio-program/x-probe` → `Permission denied`, and `gate-official-final.log` shows `PermissionError: [Errno 13]` on that mkdir, EXIT=1. The same happens on the HEAD tree. I did not change the gate or `submit_check.py`. The gate's checks run and pass: the copy in `runs/new-p16-207.r1/gate/` differs only in the log path, and it exits 0 on the final tree (`gate-feature-4.log`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS). The fix needs the parent directory made writable, or the gate run from outside the jail, to make the official row pass.

**1. Regression test fails with the change reverted and passes with it.** Yes. Final RED: `git diff HEAD~1 HEAD -- src/tracks/SampleTrack.cpp > fix.patch && git apply -R fix.patch`, rebuild, `ctest -R '^SampleClipWarpTest$'` → EXIT=8, `'!pastEnd' returned FALSE` at `tests/src/tracks/SampleClipWarpTest.cpp:668`, 17 passed / 1 failed (`test-red-v3.log`). Final GREEN: `git apply fix.patch`, rebuild, ctest → EXIT=0, 100% (`test-green-v3.log`).

**2. Deterministic.** No clock, sleep or thread. The engine is render-only (`Engine::init(true)`, `SampleClipWarpTest.cpp` `initTestCase`), so nothing adopts the queued handles on another thread. The test drops them with `AudioEngine::clearNewPlayHandles()` on the test thread, the same seam `tests/src/core/NoteCurvesAcrossTest.cpp:332` uses. Allocations are counted with the existing `test::tlCountAllocations` probe and asserted to be 0. Repeat check: `ctest -R '^SampleClipWarpTest$' --repeat until-fail:3` → EXIT=0 (`test-repeat-v3.log`).

**3. Fix at the cause; every caller and sibling checked.**
- The cause is the unbounded `getClip()` call in `SampleTrack::play`. `Track::getClip()` builds and appends a clip for an out-of-range index (`src/core/Track.cpp:1091-1100`, `addClip` at `:1016-1018`).
- Sibling: `InstrumentTrack::play` already has the same guard (`src/tracks/InstrumentTrack.cpp:951-956`), so the two now match. Callers that pass `_clip_num >= 0` are `PatternStore::play` (`src/core/PatternStore.cpp:62`, from `PatternTrack::play`, `src/tracks/PatternTrack.cpp:99,131`) and `Song::processNextBuffer` (`src/core/Song.cpp:680`, `track->play(..., clipNum)`, with `clipNum` set at `src/core/Song.cpp:540` to the current pattern and at `:548` to a MIDI clip's index). The guard treats all of them the same way: an index the track does not hold plays nothing.
- Other `getClip` callers with out-of-range indices are outside this finding. One is `src/core/TrackContainer.cpp:539`, not audited.

**4. No collateral damage.**
- Project load: no change. The guard is in `play()`, not in load or save. Clip loading goes through `claimClipOrPreserveChild` (`src/core/Track.cpp:183-194`), unchanged.
- Valid indices: the guard returns only when `_clip_num >= numOfClips()`. An index the track holds runs the same code as before.
- A pattern-store sample track with fewer clips than patterns: a pass for a missing pattern now returns `false` and creates no clip. Before, it created an empty clip and the pass returned `true`, which the RED run showed (`'!pastEnd' returned FALSE`). That the old path was silent is reasoned from the new clip having no sample; I did not measure it. I did not run the pattern-store tests.
- Family: 9/9 playback tests pass (`test-family-4.log`).

**Verdict evidence: ALREADY-FIXED / FALSE-POSITIVE claims.** The null-cast half of the finding is FALSE-POSITIVE on this tree. The evidence is in the Decision section: `SampleTrack::createClip` returns only `SampleClip` (`src/tracks/SampleTrack.cpp:318-323`), every clip-construction site that reaches a sample track builds a `SampleClip` (`src/core/StemTrackBuilder.cpp:92`, `src/core/SampleClip.cpp:330`, `src/core/Track.cpp:183-194`), and the other clip types are built on their own tracks (`src/gui/editors/PianoRoll.cpp:5850-5891`, `src/core/AutomationClip.cpp:1439-1452`, `src/tracks/PatternTrack.cpp:149`).
