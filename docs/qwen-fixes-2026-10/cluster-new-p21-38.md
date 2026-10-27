Verdict: FALSE-POSITIVE

new-p21-38: keyTrack deletes cloned-track clips without removing them from the track's clip list

## Claim

`src/core/ControlCommandsNoteMoves.cpp:105` (in `keyTrack`) does `delete clips[i]` on the non-twin clips of a
cloned track without first removing them from `copy->getClips()`, so the clone is said to keep dangling `Clip*`
entries that can be dereferenced later.

## Why it is not real

The clip's own destructor removes it from its track's clip vector, so the `delete` leaves no pointer behind.

- `src/core/Clip.cpp:260-268`: `Clip::~Clip()` emits `destroyedClip()` and then calls
  `getTrack()->removeClip(this)`.
- `src/core/Track.cpp:1044-1052`: `Track::removeClip()` finds the clip in `m_clips` and erases it (`m_clips.erase(it)` at line 1052).
- The clones' clips are linked to the clone: `Clip::Clip(Track*)` stores the track (`src/core/Clip.cpp:55-57`), and the
  clone is built from its own saved state (`src/core/Track.cpp:588-595`), so `getTrack()` on a clone's clip is the clone.
- The same self-unlinking is what the codebase relies on elsewhere. `Track::deleteClips()` at
  `src/core/Track.cpp:1064-1069` is `while (!m_clips.empty()) { delete m_clips.front(); }`. That loop only terminates
  if each deleted clip removes itself from `m_clips`. The lines in `keyTrack` use the same idiom, and the comment at
  `ControlCommandsNoteMoves.cpp:105` ("a clip leaves its track as it goes") says so.
- `Clip::hasTakeGroup()` (`include/Clip.h:199`) is non-virtual, so the call from the base destructor is not a virtual
  call on a half-destroyed derived object.

Existing coverage already pins the observable result. `tests/src/core/NoteToolsTest.cpp:370-412`
(`aSplitKeepsOnlyTheClonedCounterpartOfTheNamedClip`) asserts `madeTrack->getClips().size() == 1` for every generated
track (line 399), and `front()` is the twin. If a deleted clip still sat in the clone's vector, that assertion would fail.

## Commands run and results

| Step | Command | EXIT |
| --- | --- | --- |
| Build, first attempt (failed: ccache could not create its temp file in the shared `temporary_dir`, `Permission denied`) | `cmake --build build --target zene NoteToolsTest -j2 > .../logs/build-1.log 2>&1; echo EXIT=$?` | 2 |
| Build, retry with `CCACHE_TEMPDIR` pointed at a writable scratch dir under `$TMPDIR` (shared ccache.conf not edited) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p21-38 cmake --build build --target zene NoteToolsTest -j2 > .../logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Umbrella test family, run from build/tests | `ctest -R NoteToolsTest --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | 0 (1 of 1 test passed) |
| Split cases run directly, from build/tests | `./NoteToolsTest aSplitKeepsOnlyTheClonedCounterpartOfTheNamedClip aSplitTrackIsFoundAgainAfterItsRemovalIsUndone aSplitIsRebuiltAfterItsSourceTrackIsRemovedAndUndone notesMoveAndSplit > .../logs/test-2-split-cases.log 2>&1; echo EXIT=$?` | 0 (initTestCase, the four cases, and cleanupTestCase all PASS; Totals: 6 passed, 0 failed) |
| Gate, official script, as written | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1; echo EXIT=$?` | 1 (`PermissionError` creating `<worktree>-feature-check/`, which is outside this lane's write-jail; no gate ran) |
| Gate, same script with only the output folder and cache root pinned (`logs/feature_check_logdir.py`; gate checks, thresholds and the cached base listing for `639e1d23e` unchanged) | `python3 .../logs/feature_check_logdir.py <worktree> > .../logs/gates-2.log 2>&1; echo EXIT=$?` | 0 (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length: all PASS; RESULT: PASS; 1 changed file) |
| Completion check, before the commit | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-38 > .../logs/submit-1.log 2>&1; echo EXIT=$?` | 1 (FAIL: no commit on the branch yet; FAIL: report untracked; FAIL: gates, because submit_check calls the official feature_check.py and hits the same `PermissionError`; PASS: report present, tests item) |

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-38.r1/logs/` (`build-1.log`, `build-2.log`,
`test-1.log`, `test-2-split-cases.log`).

## RED / GREEN

No product change is made, so there is no RED/GREEN pair and no `fix.patch` (step 4b is not applicable). The
existing split tests already pass on the unmodified branch and check the invariant the finding says is broken.
No new test was added: a test that passes before and after a non-fix would not be regression proof, and
inventing a behaviour change would go against the finding's own evidence.

## Scope

No product file changed. This report is the only file in the commit.

## Self-check

The product change is none, so the regression-test questions (RED/GREEN, determinism) do not apply. Answers to the
items that do apply:

- **Regression test fails with the change reverted and passes with it:** N/A, no product change. The existing
  split tests pass on the unmodified branch (test-1.log EXIT=0, test-2-split-cases.log EXIT=0).
- **Test is deterministic:** the existing tests use no wall-clock or sleeps; I added no test.
- **Fix at the cause, complete:** N/A. The cause is absent: `Clip::~Clip` (`src/core/Clip.cpp:260-268`) removes the
  clip from its track (`src/core/Track.cpp:1044-1052`), and `Track::deleteClips` (`src/core/Track.cpp:1064-1069`)
  depends on the same behaviour.
- **No collateral damage:** N/A, no change. The verdict is FALSE-POSITIVE, so the code at `ControlCommandsNoteMoves.cpp:105`
  is left as it is.
- **Verdict evidence:** `src/core/Clip.cpp:260-268`, `src/core/Track.cpp:1044-1052`, `src/core/Track.cpp:1064-1069`,
  `tests/src/core/NoteToolsTest.cpp:370-412` (assertion at line 399).

Gates: the official `feature_check.py` cannot write its log folder from this lane (outside the write-jail), so the gate
result above (gates-2.log, EXIT=0) comes from the same script with only the log folder and cache root pinned.
`submit_check.py` calls the official script and so still reports the gates item as FAIL. That is the environment
limit, not a gate result. Re-run it from an environment that can write `<worktree>-feature-check/`.

Environment note: the shared ccache config (`/home/kruzzzzy/.cache/ccache-bunny/ccache.conf`) points its temp dir at
`dsh/home/ccache-tmp-r6`, which this lane cannot write. Builds used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p21-38`
for that invocation only; the shared config was not changed.
