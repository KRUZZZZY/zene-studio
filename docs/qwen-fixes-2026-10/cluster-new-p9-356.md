# Cluster new-p9-356

**Verdict: FIXED**

The detector assigned a newly opened note the left edge of its 1024-sample analysis window. At
`src/core/PitchTrack.cpp:116-125`, `onsetInWindow` scans the local RMS envelope in 128 analysis-rate
samples with 16-sample steps. `src/core/PitchTrack.cpp:161-165` uses that refined boundary only when
opening a note after silence; active pitch changes retain their previous boundary behavior. The regression
in `tests/src/core/AudioAnalysisTest.cpp:111-134` checks onset and offset timing for a 220 Hz tone
inside silence.

On the unmodified detector, the onset fixture begins at frame 3307 and reports frame 2048, 1259
frames (28.5 ms) early. The corrected detector passes the 20 ms boundary tolerance. The separate
offset check remains green.

## Evidence

| Check | Command result |
|---|---|
| RED, original implementation (`test-2.log`) | `ctest -R '^AudioAnalysisTest$' --output-on-failure` from `build/tests`; exit 8. Assertion reported onset 2048, expected 3307. |
| GREEN, fixed implementation (`test-3.log`) | Same ctest command; exit 0, 1/1 CTest target passed. |
| RED after removing only the production patch (`test-4.log`) | Same ctest command; exit 8, with the same onset assertion failure. |
| GREEN after restoring the patch (`test-5.log`) | Same ctest command; exit 0, 1/1 CTest target passed. |
| Affected family count | `AudioAnalysisTest`: 1 CTest target executed (1/1). The RED run reports 8 Qt test cases passed and the onset case failed. |
| Complexity gate | Exit 0. |
| File length gate | Exit 0. |

## Commands and exit codes

Commands below ran from the worktree unless marked **cwd: `build/tests`**. Build and test output is
in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/`.

Initial fixture trial (its 250 ms onset passed on the original code, so it was tightened to 75 ms;
this trial is not counted as regression evidence):

```sh
cmake --build build --target AudioAnalysisTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
# cwd: build/tests
ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
```

Final fixture on the original implementation, then the first fixed run:

```sh
cmake --build build --target AudioAnalysisTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
# cwd: build/tests
ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=8
cmake --build build --target zene AudioAnalysisTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
# cwd: build/tests
ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
```

Required patch-removal proof and restoration:

```sh
git diff -- src/core/PitchTrack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/fix.patch; rc=$?; echo EXIT=$rc
# EXIT=0
cmake --build build --target zene AudioAnalysisTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
# cwd: build/tests
ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/fix.patch; rc=$?; echo EXIT=$rc
# EXIT=0
cmake --build build --target zene AudioAnalysisTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
# cwd: build/tests
ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
```

Required gates:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
```

The `fff` grep tool was not available in this session; targeted log searches used `rg`, including
searches for `error`, `FAILED`, and `Assertion` in the specified log folder.

## Files committed

- `src/core/PitchTrack.cpp`
- `tests/src/core/AudioAnalysisTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-356.md`
