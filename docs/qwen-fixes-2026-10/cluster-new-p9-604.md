# Cluster new-p9-604: a bounce can overwrite the open project file

**Verdict: FIXED** (regression proven both ways). Both ratchet gates exit 1, but they exit 1 identically on the unchanged base tree and list no entry from this change; see "Gate status" below.

## The defect

`BounceInPlace::installPreparedOutput` moves whatever is at the requested output path to a backup and renames the staged WAV onto that path (`src/core/BounceInPlace.cpp:417`, rename at `:424`). Nothing compared the output path with the session the user has open, so a bounce whose output is the open `.mmp` replaced the session with WAV bytes. The finding's "child render writes over the project" is not the exact mechanism here: the child writes to the staged path, and the install step is what destroys the session. The effect the finding describes is the same.

Both whole-track (`renderPath = stagedPath`, `:375`) and trimmed bounces end in that install, so the guard covers both. `prepareRenderTracks` is also called directly by `ControlCommandsRenderBounce.cpp:479`, so the guard sits in the shared validation.

## Fix

- `src/core/BounceInPlace.cpp:182` `namesTheSameFile` moved up into the anonymous namespace, unchanged in logic. It was defined after its new caller, and its only callers are in this file.
- `src/core/BounceInPlace.cpp:215` new `namesOpenProject(outPath)`: true when the open project's file name names the same file. It reuses `namesTheSameFile`, so a symlink or hard link to the session is refused too.
- `src/core/BounceInPlace.cpp:235` `validateRequest` refuses with "the output path is the open project file: a bounce would replace the session". It runs after the engine-presence check, so `Engine::getSong()` is never dereferenced null. `prepareRenderTracks` is untouched.

## Regression test

`tests/src/core/ControlBounceRenderTest.cpp:320` `bounceRefusesToOverwriteTheOpenProjectFile`. It saves a real session with `saveProjectFile` and `noteProjectSaved` (the save-as path), records its bytes, then calls `bounce.land` with that path as `out`. It requires a `Refused` error, unchanged track count and mute state, and session bytes identical to the saved copy. It uses the existing fake render child, so no real render runs.

## Evidence table

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-604.r1/logs`. Every command ran from the worktree unless noted. `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-new-p9-604` is needed here because ccache cannot write under `/run/user/1000` (see build-1 below).

| Check | Exact command | Result |
| --- | --- | --- |
| RED, first attempt | `cmake --build build --target zene ControlBounceRenderTest -j2 > build-1.log 2>&1` | EXIT=2: ccache "failed to create temporary file" under `/run/user/1000` (environment, not code) |
| Build retry | `CCACHE_TEMPDIR=/tmp/... cmake --build ... > build-2.log` | EXIT=2: test used the private `Song::setProjectFileName`; test corrected to `saveProjectFile` + `noteProjectSaved` |
| Test build | `CCACHE_TEMPDIR=/tmp/... cmake --build ... > build-3.log` | EXIT=0 |
| Earlier RED, setup-only | `cd build/tests && ctest -R '^ControlBounceRenderTest$' --output-on-failure > test-red-1.log` | EXIT=8: failed at setup (`projectFileName` empty), a test bug; corrected |
| **RED (no fix)** | same ctest after build-4 (`build-4.log`, EXIT=0) → `test-red-2.log` | **EXIT=8**: `bounceRefusesToOverwriteTheOpenProjectFile`, `Actual (refused.errorKind): 0`, `Expected (ControlErrorKind::Refused): 5`, `ControlBounceRenderTest.cpp:335`. The bounce reported success. The test stopped at this check, so the session-bytes check was not reached; the replacement follows from the install code (`BounceInPlace.cpp:417-424`), not from an observed byte change. |
| Fix build attempt | `build-5.log` | EXIT=2: `namesOpenProject` used before its declaration; helper moved up (see gate note) |
| Final build with fix | `build-9-final-green.log` | EXIT=0 |
| **GREEN (fix)** | `cd build/tests && ctest -R '^(ControlBounceRenderTest\|BounceInPlaceTest)$' --output-on-failure -V > test-final-green.log` | **EXIT=0**: `BounceInPlaceTest` 7 passed, 0 failed; `ControlBounceRenderTest` 12 passed, 0 failed; 2/2 ctest targets |
| Proof: fix reversed, RED | `git diff -- src/core/BounceInPlace.cpp > fix.patch; echo $?` → `git apply -R fix.patch; echo $?` → build (`build-10-reverted.log`) → `ctest -R '^ControlBounceRenderTest$' ... > test-final-red.log` | diff EXIT=0, reverse EXIT=0, build EXIT=0, **ctest EXIT=8** (11 passed, 1 failed: the new test, `refused.errorKind` 0 vs 5) |
| Proof: fix reapplied, GREEN | `git apply fix.patch` (EXIT=0) → build (`build-11-reapplied.log`, EXIT=0) → `ctest -R '^(ControlBounceRenderTest\|BounceInPlaceTest)$' -V > test-final-green-2.log` | **EXIT=0**: 7 passed + 12 passed |
| Affected family (related callers) | build `build-12-related.log` (EXIT=0); `ctest -R '^(ControlBounceRenderTest\|BounceInPlaceTest\|ControlRenderJobTest\|StemExportTest\|ProjectToolsTest\|UndoBoundsTest\|ControlVerbInverseTest)$' --output-on-failure > test-family-1.log` | **EXIT=0**: 7 of 7 ctest targets passed |
| Complexity gate, final | `bash tests/complexity-gate.sh --check > gate-complexity-final.log 2>&1; echo $?` | **EXIT=1** (see gate status) |
| File-length gate, final | `bash tests/file-length-gate.sh --check > gate-filelength-final.log 2>&1; echo $?` | **EXIT=1** (see gate status) |
| Base-tree gate comparison | `git diff -- <both files> > full-change.patch; git apply -R full-change.patch`; run both gates → `gate-*-base.log`; `git apply full-change.patch` | Base: complexity EXIT=1, file-length EXIT=1 |
| Regression-list diff, base vs change | `diff` of the `REGRESSION:` lines (`c-base.txt` vs `c-change.txt`, `f-base.txt` vs `f-change.txt`) | `DIFF_C=0`, `DIFF_F=0`: identical lists |

Test count: 2 ctest targets in the bounce family (19 subtests, `ControlBounceRenderTest` 12 + `BounceInPlaceTest` 7), and 7 ctest targets in the related set. None ran zero tests.

## Gate status

Both required ratchet gates exit **1** on the final tree, and they also exit 1 on the unchanged base tree. The regression lists are identical with and without this change (`DIFF_C=0`, `DIFF_F=0`).

- Complexity: 6 `REGRESSION` entries, none in this change. My first attempt added CCN 11 to `prepareRenderTracks` (`BounceInPlace.cpp@345-391`), and that entry is gone now that the refusal lives in `validateRequest`.
- File length: 14 `REGRESSION` entries, all in files this change does not touch (for example `src/core/TakeLane.cpp (520)`, `src/core/SmfInterchangeReader.cpp (504)`, and several `tests/src/core/*Test.cpp`).

These failures predate this cluster and are outside its one bug, so I did not change them and did not touch gate limits or baselines.

## Files changed

- `src/core/BounceInPlace.cpp`: the guard and the helper move.
- `tests/src/core/ControlBounceRenderTest.cpp`: the regression test. Both files are already registered in `tests/fork-sources.txt`.
- `tests/upstream-modifications.txt`: the cluster reason appended to the existing `src/core/BounceInPlace.cpp` entry. The file still ends with a newline.
- `docs/qwen-fixes-2026-10/cluster-new-p9-604.md`: this report.

No control command was added, so the `ControlCommandsSnapshot` canary is unchanged. No new source files were added.
