# Cluster new-p8-256

Verdict: **FIXED**

## Evidence

- `readOnly()` now fills `AafLossReport::trackGainClamps` from the parsed model before calling `summary()`: `src/core/ControlCommandsAafImport.cpp:210`.
- `aafPopulateTrackGainClampLoss()` counts requested fader values outside `MinVolume..MaxVolume`, using the same percentage conversion as the builder. The builder uses its live `FloatModel` limits at `src/core/AafImportBuild.cpp:138-141`; `SampleTrack` initializes those limits from `MinVolume` and `MaxVolume` at `src/tracks/SampleTrack.cpp:53`.
- The import path continues to copy the builder count into the shared loss report at `src/core/ControlCommandsAafImport.cpp:281`. `AafLossReport::toJson()` serializes it at `src/core/AafImportModel.cpp:94`.
- `tests/src/core/AafImportTest.cpp:358-372` verifies the serialized count is 1 for a 250% gain and 0 for a 100% gain through the same report population helper used by dry-run. The real fixture command checks confirm `aaf_read` and `aaf_import` return zero for its in-range track at lines 374-389 and 398-410. The builder test verifies the over-range import count and its serialized loss value at lines 250-281.

Affected family: `AafImportTest`, one registered CTest case (`Test #211`, 1/1 executed). With production hunks reversed, QtTest reported 12 passed and the serialized dry-run assertion failed (`actual 0`, expected `1`). The restored run passed 1/1.

## Commands and results

All build commands ran from the worktree root. All CTest commands ran from `build/tests`. Build and test output is in the numbered logs shown.

| Stage | Command | Exit |
|---|---|---:|
| Save and reverse production patch | `git diff -- include/AafImport.h src/core/AafImportModel.cpp src/core/ControlCommandsAafImport.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r2/logs/fix.patch` | 0 |
| RED build | `cmake --build build --target zene AafImportTest -- -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r2/logs/build-9.log 2>&1` (captured `EXIT=0`) | 0 |
| RED test | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r2/logs/test-10.log 2>&1` (cwd `build/tests`) | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r2/logs/fix.patch` | 0 |
| GREEN build | `cmake --build build --target zene AafImportTest -- -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r2/logs/build-10.log 2>&1` (captured `EXIT=0`) | 0 |
| GREEN test | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r2/logs/test-11.log 2>&1` (cwd `build/tests`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r2/logs/gate-3.log 2>&1` (captured `EXIT=0`) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r2/logs/gate-4.log 2>&1` (captured `EXIT=0`) | 0 |

The `fff` grep tool was not exposed in this environment (`command -v fff` returned no path); numbered logs were checked with targeted `rg` searches.

## Files committed

- `include/AafImport.h`
- `src/core/AafImportModel.cpp`
- `src/core/ControlCommandsAafImport.cpp`
- `tests/src/core/AafImportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-256.md`
