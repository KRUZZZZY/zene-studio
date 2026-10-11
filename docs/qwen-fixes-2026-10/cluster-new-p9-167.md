# Cluster new-p9-167

Verdict: FIXED

`loadedTiming()` in `src/core/GrooveTemplate.cpp:232-236` now applies the legacy exact-half clamp only to even step widths. For odd widths, the truncated integer half-width is strictly inside the boundary and survives loading. The existing even-width legacy-load regression remains at `tests/src/core/GrooveTemplateTest.cpp:415`; the new extraction/save/load regression is at `tests/src/core/GrooveTemplateTest.cpp:169`.

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| Regression build | `cmake --build build --target GrooveTemplateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/build-1.log 2>&1` | 0 |
| RED before product fix | `ctest -R '^GrooveTemplateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/test-1.log 2>&1` | 8 |
| RED assertion | `GrooveTemplateTest::extractionSaveLoadRoundTripsOddStepOffsets`: loaded `+1`, expected `+2` | — |
| Initial rebuild | `cmake --build build --target zene GrooveTemplateTest ControlGrooveCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/build-2.log 2>&1` | 0 |
| Initial focused family | `ctest -R '^(GrooveTemplateTest|ControlGrooveCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/test-2.log 2>&1` | 0 |
| Create reverse patch | `git diff -- src/core/GrooveTemplate.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/fix.patch` | 0 |
| Reverse patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/fix.patch` | 0 |
| Rebuild with fix reversed | `cmake --build build --target zene GrooveTemplateTest ControlGrooveCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/build-3.log 2>&1` | 0 |
| RED with fix reversed | `ctest -R '^GrooveTemplateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/test-3.log 2>&1` | 8 |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/fix.patch` | 0 |
| Final rebuild | `cmake --build build --target zene GrooveTemplateTest ControlGrooveCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/build-4.log 2>&1` | 0 |
| GREEN focused family | `ctest -R '^(GrooveTemplateTest|ControlGrooveCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/test-4.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/gate-3.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r2/logs/gate-4.log 2>&1` | 0 |

The focused family ran 2 registered ctest tests; both passed after the fix. The reversed-fix run failed at the new round-trip assertion as required. The even-width legacy test remains in the same `GrooveTemplateTest` binary.

The session did not expose an `fff grep` tool or CLI, so log matches were searched with `rg`.

## Files committed

- `src/core/GrooveTemplate.cpp`
- `tests/src/core/GrooveTemplateTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-167.md`
