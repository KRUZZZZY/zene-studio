# Cluster review-faa175bfa — Failed handoff recursively deletes preexisting destination media

Verdict: **ALREADY-FIXED** (no code change needed; the fix and its regressions are on this
branch at HEAD)

## Finding

`discardPartialBundle` (`src/core/ControlCommandsHandoff.cpp:158-163`) removes the
destination `media` folder with `removeRecursively()`. The finding claims the preflight
checks only the session copy and `HANDOFF.json` and therefore lets a handoff proceed into
a folder that already holds a `media` folder, whose contents the rollback then destroys.

## Evidence

- `src/core/ControlCommandsHandoff.cpp:143-147` — `mediaTaken()` refuses the media slot when
  anything occupies it: a folder, a file, or a symlink (including a dangling one).
- `src/core/ControlCommandsHandoff.cpp:182-186` — `handoff()` runs that preflight **before**
  `Song::saveProjectFile` (`:187`), before `collectProjectMediaInto` (`:195`) and therefore
  before either `discardPartialBundle` call (`:197`, `:203`). So at rollback time the media
  slot provably did not exist, and everything under it was created by this hand off.
- Fix commit already in this branch's history: `6a97b9c21` "fix(rework faa175bfa): a failed
  hand off could remove a media folder or a link's target that it did not write" (the fix for
  this very cluster). It added `mediaTaken` + the preflight branch.
- Regressions named by the finding ("regress preexisting media survival under failed
  handoff") are present and unweakened:
  - `tests/src/core/LibraryProjectsTest.cpp:420-433`
    `aHandOffNeverWritesIntoOrRemovesAnExistingMediaFolder` — a media folder holding
    `keep.txt` must make the command `Refused`, with `keep.txt` still on disk.
  - `tests/src/core/LibraryProjectsTest.cpp:436-449`
    `aHandOffNeverRemovesWhatAMediaLinkPointsAt` — a media link to a folder elsewhere: the
    file outside the hand-off folder must survive.
  - `tests/src/core/LibraryProjectsTest.cpp:404-416`
    `aFailedHandOffLeavesNoBundleBehind` — the artifact-cleanup assertion (no manifest, no
    `*.mmpz`/`*.mmp` left) plus the preexisting media file left untouched. Untouched.
  - `tests/src/core/LibraryProjectsTest.cpp:375-400`, `:303-327`, `:331-371` — the other
    destination-folder protections (`takenFile`, dangling links, `<copy>.new`). Untouched.

## Proof the existing regressions catch the bug (fix reverted, then restored)

Only the product hunk of `6a97b9c21` was reversed (tests untouched):

```
git show 6a97b9c21 -- src/core/ControlCommandsHandoff.cpp > logs/fix.patch && git apply -R logs/fix.patch
cmake --build build --target LibraryProjectsTest                       # EXIT=0
cd build/tests && ctest -R '^LibraryProjectsTest$' --output-on-failure  # EXIT=8
```

Failing assertions without the fix (logs/test-2-red.log):

```
FAIL!  : LibraryProjectsTest::aHandOffNeverWritesIntoOrRemovesAnExistingMediaFolder()
   Actual   (refused.errorKind)        : 0
   Expected (ControlErrorKind::Refused): 5
FAIL!  : LibraryProjectsTest::aHandOffNeverRemovesWhatAMediaLinkPointsAt()
   'QFileInfo::exists(sentinel)' returned FALSE. (a rollback removed the file the media link points at)
Totals: 15 passed, 2 failed, 0 skipped, 0 blacklisted
```

The second failure is the finding's exact claim measured: the rollback deleted a file
outside the hand-off folder. Restored with `git apply logs/fix.patch`, rebuilt, all 17 cases
pass. So HEAD is green and the pre-fix code was red: the bug was real and is fixed.

## Commands run (copied verbatim, EXIT codes as seen)

```
cmake --build build --target LibraryProjectsTest zene > logs/build-1.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^LibraryProjectsTest$' --output-on-failure \
    > logs/test-1.log 2>&1                                                    # EXIT=0  (100% tests passed out of 1)
# RED: reverse-apply the product hunk of 6a97b9c21, rebuild, rerun
cmake --build build --target LibraryProjectsTest > logs/build-2.log 2>&1        # EXIT=0
cd build/tests && ctest -R '^LibraryProjectsTest$' --output-on-failure \
    > logs/test-2-red.log 2>&1                                                 # EXIT=8  (2 failed)
# GREEN: re-apply the hunk, rebuild zene too, rerun
cmake --build build --target LibraryProjectsTest zene > logs/build-3.log 2>&1  # EXIT=0
cd build/tests && ctest -R '^LibraryProjectsTest$' -V \
    > logs/test-3-green.log 2>&1                                              # EXIT=0  (Totals: 17 passed, 0 failed)
bash tests/complexity-gate.sh --check > logs/gate-complexity-1.log 2>&1         # EXIT=1
bash tests/file-length-gate.sh --check > logs/gate-filelength-1.log 2>&1        # EXIT=1
```

Affected family: `LibraryProjectsTest` — 17 test cases executed, 17 passed.

## Gates: honest note (PRE-EXISTING failures, not caused by this review)

Both ratchet gates report EXIT=1 on this branch's untouched HEAD (`git status --porcelain`
empty; no source file of mine is in either report):

- complexity: 6 `REGRESSION: new function over target` lines for functions in
  `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`,
  `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`,
  `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`
  (logs/gate-complexity-1.log).
- file length: 14 `REGRESSION: new file over 500 lines` lines, all pre-existing Zene test
  files (logs/gate-filelength-1.log).

No gate limit, baseline or assertion was touched. These regressions belong to other commits
on the branch and are outside this single-bug review; they need their own fix.

## Files committed

`docs/qwen-fixes-2026-10/cluster-review-faa175bfa.md` (this report) only. No product or test
file changed: the behaviour the finding describes is already correct at HEAD
(`6a97b9c21`), and its regressions (`LibraryProjectsTest`) already cover it.