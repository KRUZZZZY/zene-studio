# Cluster new-p9-385

**Verdict: FIXED**

`RenderManager::pathForTrack()` in `src/core/RenderManager.cpp:251-267` passed `Track::FILENAME_FILTER` to `QRegularExpression`; the pattern is rejected, so forbidden characters remained in legacy per-track export paths. The method now removes control characters and the same filename-forbidden characters explicitly used by `stemFileName()`, preserving the legacy numeric prefix and empty-name behavior.

The real export regression is `StemExportTest::perTrackExportSanitizesTrackNames()` in `tests/src/core/StemExportTest.cpp:143-170`. It renders one track named `Bass/illegal` and requires `1_Bassillegal.wav`. It precreates the buggy nested destination so the original code reaches the assertion instead of failing to open its output. Before the fix, the assertion failed; after the fix, the expected root-level file exists.

## Verification evidence

| Check | Exact command | Exit/result |
|---|---|---|
| First regression build | `cmake --build build --target StemExportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/build-1.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| First regression attempt | `ctest -R '^StemExportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/test-1.log 2>&1; echo EXIT=$?` | `EXIT=8`; the renderer could not open the missing nested destination, so the test was adjusted to reach its assertion |
| Adjusted regression build | `cmake --build build --target StemExportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/build-2.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| RED, original production code | `ctest -R '^StemExportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/test-2.log 2>&1; echo EXIT=$?` | `EXIT=8`; `perTrackExportSanitizesTrackNames()` failed because `sanitizedFileExists` was false |
| First GREEN build | `cmake --build build --target zene StemExportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/build-3.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| First GREEN test | `ctest -R '^StemExportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/test-3.log 2>&1; echo EXIT=$?` | `EXIT=0`; 1/1 CTest case passed |
| Reverse-patch command | `git diff -- src/core/RenderManager.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/fix.patch; echo EXIT=$?` | `EXIT=0` |
| RED, reverse-patch proof build | `cmake --build build --target zene StemExportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/build-4.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| RED, reverse-patch proof test | `ctest -R '^StemExportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/test-4.log 2>&1; echo EXIT=$?` | `EXIT=8`; same assertion failed |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/fix.patch; echo EXIT=$?` | `EXIT=0` |
| Final rebuilt targets | `cmake --build build --target zene StemExportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/build-5.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| GREEN, final test | `ctest -R '^StemExportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/test-5.log 2>&1; echo EXIT=$?` | `EXIT=0`; 1/1 CTest case passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/complexity-gate-2.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-385.r1/logs/file-length-gate-2.log 2>&1; echo EXIT=$?` | `EXIT=0` |

The affected CTest family contains one CTest case, `StemExportTest`; the final run reports 1/1 passed. The test source is already registered in `tests/fork-sources.txt`. The inherited renderer change is recorded in `tests/upstream-modifications.txt:705`.

## Files committed

- `src/core/RenderManager.cpp`
- `tests/src/core/StemExportTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-385.md`
