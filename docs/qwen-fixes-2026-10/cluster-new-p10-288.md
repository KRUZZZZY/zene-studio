FALSE-POSITIVE

Cluster new-p10-288 (Luna BUG 20): "A malformed project can make `PatternClip` dereference a failed cast".

The finding says a project loader constructs a `PatternClip` under a non-pattern track, so the unchecked `dynamic_cast` in `patternIndex()` returns null. On this branch the `PatternClip(Track*)` constructor is only reached through `PatternTrack::createClip()`, which passes a `PatternTrack`, and the loader never asks a non-pattern track for a `PatternClip`. The cast cannot fail, so there is nothing to fix.

## Evidence (file:line on `bugs/qwen-new-p10-288`, base `37eaf07557`)

1. The reported code is as described, but unreachable from a bad project:
   - `src/core/PatternClip.cpp:38-41`: the constructor calls `patternIndex()` in its first statement.
   - `src/core/PatternClip.cpp:106-108`: `return dynamic_cast<PatternTrack*>(getTrack())->patternIndex();` with no null check.
   - `include/PatternClip.h:40`: the constructor is public and takes a `Track*`.

2. The only construction of `PatternClip(Track*)` is in the pattern track's own factory:
   - `src/tracks/PatternTrack.cpp:174-178`: `Clip* PatternTrack::createClip(...) { auto pc = new PatternClip(this); ... }`. `this` is a `PatternTrack`, so the cast in `patternIndex()` succeeds.
   - A repo-wide search for `PatternClip(` in `src`, `include` and `tests` finds no other constructor call. The remaining hits are `PatternClip clip(&patternTrack);` at `tests/src/tracks/AutomationTrackTest.cpp:385`, where `patternTrack` is a `PatternTrack`, and `patternTrack->createClip(TimePos(0))` at `tests/src/gui/ArrangerTimelineTest.cpp:511`.
   - `PatternClip::clone()` (`include/PatternClip.h:55-58`) uses the copy constructor, which does not call `patternIndex()`.

3. The project loader does not construct clips from the element name. It asks the track:
   - `src/core/Track.cpp:183`: `Clip* clip = track.createClip(TimePos(0));` is virtual dispatch on the track that owns the element.
   - `src/core/Track.cpp:154-157`: the documented contract is that `Track::createClip()` returns one of the four clip classes, and the loader accepts a clip element of any of those names.
   - The return value per track class: `InstrumentTrack::createClip` builds a `MidiClip` (`src/tracks/InstrumentTrack.cpp:1091-1093`), `SampleTrack` a `SampleClip` (`src/tracks/SampleTrack.cpp:324-326`), `AutomationTrack` an `AutomationClip` (`src/tracks/AutomationTrack.cpp:59-61`), and `TrackFolder` returns null (`src/tracks/TrackFolder.cpp:179-181`), which `src/core/Track.cpp:184-190` already handles by preserving the element.
   - So a `<patternclip>` element under an `<instrumenttrack>` is loaded as a `MidiClip` through `restoreState`. The `PatternClip` constructor is not called for it, and no cast is performed.

4. `Clip(track)` stores the track before the derived body runs (`src/core/Clip.cpp:55-57`, `m_track( track )` in the initializer list), so `getTrack()` is valid inside the constructor.

5. Existing coverage of the same class of bug: `tests/src/core/UnclaimedElementsTest.cpp:497-...` (`aClipElementUnderAFolderTrackIsPreservedNotCrashing`, cluster 5426) covers a clip element under a track that cannot create one. That is the case the loader already handles. No test is added here because the reported state cannot be produced.

## Not verified

- I did not run a load of a crafted `.mmp` file under an instrument track. The verdict rests on the reading above (items 2 and 3). Nothing in the source constructs `PatternClip` outside `PatternTrack::createClip`.
- The write-jail blocks `feature_check.py` from creating its log directory next to the worktree (`zene-qwen-new-p10-288-feature-check`, `PermissionError`, see `logs/001-feature-check-pre.log`). I ran each of its gate commands directly from the worktree instead, with the same command lines. The `complexity` and `file-length` ratchets were not compared against a base listing, because the wrapper's base cache lives outside the worktree.

## Commands run (cwd: the worktree; logs in the LOG FOLDER `new-p10-288.r1/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| Gate wrapper (blocked) | `python3 .../qwen/feature_check.py <worktree>` (log `001-feature-check-pre.log`) | 1 (`PermissionError` creating `<worktree>-feature-check`) |
| fork-sources | `bash tests/fork-sources-gate.sh` (log `010`) | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` (log `011`) | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` (log `012`) | 0 |
| check-namespace | `tests/scripted/check-namespace` (log `013`) | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` (log `014`) | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` (log `015`) | 0 |
| complexity (ratchet, raw) | `bash tests/complexity-gate.sh --check` (log `016`) | 1 (train-wide red; 6 `REGRESSION` lines; none in `PatternClip.cpp`) |
| file-length (ratchet, raw) | `bash tests/file-length-gate.sh --check` (log `017`) | 1 (train-wide red; 23 items; none in `PatternClip.cpp`) |

The branch has no changed files versus its base, so no ratchet item can be "new in a file the branch changed"; the two ratchet exits are the train's standing red, not caused by this report. No regression test was written, because the report is not a fix (procedure step 2: not-real findings skip to the report).

## Self-check

Filled in after running `submit_check.py` (see below).
