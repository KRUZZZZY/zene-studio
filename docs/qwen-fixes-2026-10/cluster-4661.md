# Cluster 4661: NaN variation validation

**Verdict: ALREADY-FIXED**

`validateWalk()` rejects non-finite variation before checking the `0..1` range at
[`src/core/ChordProgression.cpp:323`](../../src/core/ChordProgression.cpp:323).
The regression assertion supplies `quiet_NaN()` and verifies rejection and a
variation error at [`tests/src/core/ChordProgressionTest.cpp:192`](../../tests/src/core/ChordProgressionTest.cpp:192).
Both changes are already present in commit `977718c8c` (`fix(chord): a NaN
variation no longer passes validation`). No product or test code was changed
for this cluster.

## Evidence

| Check | Command | Result |
| --- | --- | --- |
| RED regression | Not run. The branch already contains the fix and regression; per procedure step 2, this was classified ALREADY-FIXED and skipped to the report. | N/A |
| Build existing test target | `cmake --build build --target ChordProgressionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4661.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4661.r1/logs/build-1.log; printf 'BUILD_EXIT=%s\n' "$rc"` | EXIT=0 |
| GREEN family test | From `build/tests`: `ctest -R '^ChordProgressionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4661.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4661.r1/logs/test-1.log; printf 'CTEST_EXIT=%s\n' "$rc"` | EXIT=0; 1/1 CTest tests passed (Test #30) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4661.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4661.r1/logs/gate-1.log; printf 'COMPLEXITY_EXIT=%s\n' "$rc"` | EXIT=0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4661.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4661.r1/logs/gate-2.log; printf 'FILE-LENGTH_EXIT=%s\n' "$rc"` | EXIT=0 |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-4661.md`
