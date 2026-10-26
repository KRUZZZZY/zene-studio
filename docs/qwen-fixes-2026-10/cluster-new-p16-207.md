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

No behaviour changes for valid indices. For an out-of-range index the removed side effect (a new empty clip) was never audible, because the new clip has no sample.

## Regression test

`tests/src/tracks/SampleClipWarpTest.cpp`, slot `aSingleClipIndexTheTrackDoesNotHoldCreatesNothing` (line 654). It has three parts:
- an index the track holds (`0`) plays (`held == true`);
- an index one past the end (`1`) and a far one (`7`) both return `false`, and `numOfClips()` stays `1`;
- the two out-of-range calls allocate nothing (`test::tlAllocationCount == 0`, via the existing `AllocationProbe`).

The test is deterministic. It has no timing and no races. The play handles are drained with the file's existing `drainPlayHandles` helper.

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
| **RED (relocated test, fix reverted)** | `git diff -- src/tracks/SampleTrack.cpp > fix.patch && git apply -R fix.patch`; rebuild `zene SampleClipWarpTest`; `ctest -R '^SampleClipWarpTest$'` | build 0; ctest **8** | `'!pastEnd' returned FALSE`, `SampleClipWarpTest.cpp(671)`; 17 passed, 1 failed |
| **GREEN (relocated test, fix applied)** | `git apply fix.patch`; rebuild; ctest | build 0; ctest **0** | 100% tests passed, 1 of 1 |
| Family after fix | rebuild `SampleClipRubberBandTest SampleClipStretchTest SampleClipWarpModesTest TakeLaneCompPlaybackTest EngineRenderBenchmarkTest NoteBackgroundAudioTest SampleClipWarpModeMenuTest SampleClipWindowTest SampleClipWarpTest`; `ctest -R '^(SampleClip.*\|TakeLaneCompPlaybackTest\|EngineRenderBenchmarkTest\|NoteBackgroundAudioTest\|SampleClipWarpModeMenuTest)$'` | build 0; ctest **0** | **9 of 9** tests executed, 100% passed |

A first family run (EXIT=8) listed `SampleClipRubberBandTest` as `Not Run`, because I had not built it. I built it and reran (EXIT=0, 9/9, `test-family-2.log`). The final family run is `test-family-3.log`.

Gate:
- Official command `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: EXIT=1, a `PermissionError` before any gate ran. The script writes its logs to `<worktree>/../zene-qwen-new-p16-207-feature-check`, which is outside the write-jail (`gate-feature-1.log`).
- So I ran a copy of the same script (`runs/new-p16-207.r1/gate/feature_check_logdir.py`) with only the log directory changed, pointing at `runs/new-p16-207.r1/logs/feature-check`. The gate logic, the limits and the baselines are the same ones in `qwen/gate-baselines` (copied in). Command: `python3 -I .../gate/feature_check_logdir.py <worktree>`. EXIT=0 (`gate-feature-3.log`):
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

(Filled in after `submit_check.py`.)
