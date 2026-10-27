FIXED

Cluster new-p10-291: YIN lag refinement can read past its vector (src/core/PitchTrack.cpp, finding line 67, now 68-72).

## Evidence

- Defect (before): `bestLag` allocates `cmnd` with `maxLag` entries (`src/core/PitchTrack.cpp:62`). The refinement loop
  (`:68-77`) only enters a crossing at `tau <= maxLag - 2`, but its descent `while` checked `tau + 1 < maxLag` before
  advancing, so `tau` could advance from `maxLag - 2` to `maxLag - 1` when `cmnd[maxLag-1] < cmnd[maxLag-2]`. Line 74
  then read `c = cmnd[tau + 1] = cmnd[maxLag]`, one past the end of the vector. Every advance to `maxLag - 1` took that
  read, so the defect is exactly the case the finding describes.
- Fix: `src/core/PitchTrack.cpp:71` now reads `while (tau + 2 < maxLag && cmnd[tau + 1] < cmnd[tau])`. `tau` can no
  longer leave `maxLag - 2`, so `cmnd[tau + 1]` is at most `cmnd[maxLag - 1]`. Behaviour changes only where the old
  code advanced to `maxLag - 1`, and that always performed the out-of-range read. All other inputs take the same path.
- Callers and siblings checked: `bestLag` has one caller, `pitchAt` (`:83-100`, `:98`). `pitchAt` is exported
  (`include/PitchTrack.h`) and is also called from `src/core/ControlCommandsTuner.cpp:83` and `detectNotes`
  (`src/core/PitchTrack.cpp:234`); no signature or result contract changes. `pitchAt` rejects `maxLag < 4` (`:86`), so
  the loops always have `maxLag >= 4`. The first scan (`:68`) keeps `tau + 1 < maxLag`, so `cmnd[tau + 1]` is in range
  there too. The `d` loop (`:88-96`) reads at most `frame + 2*maxLag - 2 = frame + size - 2`, inside the check at `:86`.
  `periodicityError` (used only by the boundary search, not by `bestLag`) is not on this path.
- Why the test needs a trick: the stray read returns whatever bytes follow the vector. This build is RelWithDebInfo
  (`-O2 -DNDEBUG`, no sanitizer, `build/CMakeCache.txt`), so the read is silent and its value is not stable. A test
  that only checks the output therefore cannot fail reliably. The regression instead pads each allocation made during
  the test with known bytes and checks that the answer is the same for 0xFF (NaN) and 0x00 padding. A correct
  implementation never reads the padding, so the two answers match; the buggy one depends on which padding it read.

## Regression test

- `tests/src/core/PitchTrackCmndBoundary.h:46-65` (`checkPitchNeverReadsPastCmnd`): a 1024-frame sine of period 564
  frames, `pitchAt(..., 0, 1024, 11025)` run with 0xFF and then 0x00 padding. The sine's dip first falls under the
  threshold at lag 510 (`maxLag - 2`, with `maxLag = 512`) and the lag after it is lower, so the buggy code descends to 511
  and reads `cmnd[512]`. Found offline with a Python model of the same arithmetic (not part of the build); the margins
  at the threshold are about 1 to 2 percent.
- `tests/src/core/AudioAnalysisTest.cpp:51` (include), `:146` (the test binary's `operator new` adds the padding while
  `g_fillAllocationTail` is set; with it clear the allocation is unchanged), `:233` (slot `pitchNeverReadsPastItsCmndVector`).
- Why it is in a header: `AudioAnalysisTest.cpp` was 493 code lines at base (`git show HEAD:tests/src/core/AudioAnalysisTest.cpp`).
  The file-length gate counts code lines and limits a file to 500, so the test body inline pushed it to 521 and failed.
  A separate test file would have needed a CMake reconfigure, which the lane build guard refuses ("configuring is the
  parent's job"; `logs/configure-1.log`). The header keeps the test in the existing `AudioAnalysisTest` target and
  ctest entry; only the allocation hook and a one-line slot live in the .cpp.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-291.r1/logs/`.

| Step | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target AudioAnalysisTest -j4` | 2 (ccache could not write its temp file under the shared dsh path) | build-1.log |
| Build, retry | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p10-291 cmake --build build --target AudioAnalysisTest -j4` | 0 | build-2.log |
| RED (fix reverted) | `git apply -R fix.patch`; `CCACHE_TEMPDIR=... cmake --build build --target AudioAnalysisTest -j4`; `cd build/tests && ctest -R '^AudioAnalysisTest$' -V` | build 0, **ctest 8** | build-10-red-header.log, test-10-red-header.log |
| GREEN (fix applied) | `git apply fix.patch`; rebuild as above; `cd build/tests && ctest -R '^AudioAnalysisTest$' -V` | build 0, **ctest 0** | build-11-green-header.log, test-11-green-header.log |
| 4b proof | the RED and GREEN rows above, in that order, with `fix.patch` made by `git diff -- src/core/PitchTrack.cpp` | as above | |

RED output (test-10-red-header.log): `FAIL! : AudioAnalysisTest::pitchNeverReadsPastItsCmndVector() 'nanTail == zeroTail'
returned FALSE. (pitchAt depends on the bytes past its CMND vector)`; `Totals: 23 passed, 1 failed`.

GREEN output (test-11-green-header.log): `PASS : AudioAnalysisTest::pitchNeverReadsPastItsCmndVector()`;
`Totals: 24 passed, 0 failed`. The family `^AudioAnalysisTest$` is one ctest entry and ran 24 test functions (not zero).

Earlier runs, superseded by the header move and kept for the record: the test was first inline in
`AudioAnalysisTest.cpp` (test-1-red.log: exit 8, the same assertion failing; test-2-green.log: exit 0;
test-3-unfixed-4b.log: exit 8; test-4-fixed-4b.log: exit 0). A separate `PitchTrackTest.cpp` target was tried and
withdrawn: its build failed with `No rule to make target 'PitchTrackTest'` (build-6, exit 2), and
`cmake -S . -B build` was refused by the lane guard (configure-1.log, exit 97).

Gates. `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 before running
any check: it writes its logs to `<worktree>/../<name>-feature-check`, which is outside this lane's write-jail
(`gate-1-feature-check.log`, PermissionError). I ran the same checks through `$TMPDIR/p10-291-scratch/gate_driver.py`,
which imports `feature_check.py` unchanged and calls its own `SIMPLE` and `RATCHET` lists and its helpers, writing logs
to the LOG FOLDER:

| Run | EXIT | Result |
| --- | --- | --- |
| gate-2 (test inline in AudioAnalysisTest.cpp) | 1 | file-length: `tests/src/core/AudioAnalysisTest.cpp` (521 code lines) is a NEW file over 500 |
| gate-3 (header not yet staged) | 1 | fork-sources and all-sources-reproduce: the manifest entry is not in `git ls-files` yet |
| gate-4 (header staged with `git add`, final code) | 0 | all eight rows PASS (`gate-4-table.log`) |

Other commands: `git add -- tests/src/core/PitchTrackCmndBoundary.h` (staging only). No push, no remote change, no
stash, no history rewrite.

## Files committed

- `src/core/PitchTrack.cpp` (one token, `:71`)
- `tests/src/core/AudioAnalysisTest.cpp` (include, allocation padding, slot)
- `tests/src/core/PitchTrackCmndBoundary.h` (new; the regression body; registered in `tests/fork-sources.txt` and `tests/all-sources.txt`)
- `tests/fork-sources.txt`, `tests/all-sources.txt` (one line each)
- `docs/qwen-fixes-2026-10/cluster-new-p10-291.md` (this report)

## Not changed, observed

- The parabolic refinement uses three CMND points. When they are nearly collinear the vertex can lie far outside
  the bracket. The offline model gave a refined lag of about 562 for the test signal, which was not measured in the
  build. The fix does not change this. The test checks only that the answer does not depend on the bytes past the vector.
