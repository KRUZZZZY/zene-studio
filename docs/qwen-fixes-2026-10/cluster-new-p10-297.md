# Cluster new-p10-297

Verdict: **FIXED**

## Evidence

- `src/core/PortMapping.cpp:174-177` now rounds half the granted lease up, clamps the renewal delay to at least one second, and retains the 3600-second cap.
- `tests/src/core/PortMappingCodecTest.cpp:197-210` feeds `PortMapping` a successful one-second lease and checks that its renewal timer is active with a 1000 ms interval. The original code failed with actual interval `0` and expected `1000`.
- `include/PortMapping.h:174` grants the existing test class access to the private success path and timer so the regression stays deterministic.

The affected ctest family is `PortMappingCodecTest`: 1 registered ctest entry ran. The initial RED run reported 11 Qt cases passed and the new regression failed; each GREEN run passed 1/1 ctest entries.

For the inverse proof, `fix.patch` contains only the behavioral change in `src/core/PortMapping.cpp`; the test access declaration remains applied so the regression can compile and fail on the original behavior.

## Commands and results

All builds ran from the worktree root. All ctest commands ran from `build/tests`. Output was redirected to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs`.

| Purpose | Command | Exit |
|---|---|---:|
| Build regression target before fix | `cmake --build build --target PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| RED on original behavior | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 8 |
| Build `zene` and regression target after fix | `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| GREEN after fix | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Save behavioral fix patch | `git diff -- src/core/PortMapping.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Reverse behavioral fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Rebuild for inverse proof | `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| RED with fix reversed | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 8 |
| Reapply behavioral fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Rebuild after restoring fix | `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| GREEN after restoring fix | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-297.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |

The `fff` grep tool was unavailable in this environment, so build, test, and gate logs were checked with bounded one-term `rg` searches for `error`, `FAILED`, and `Assertion`; test totals and the regression failure line were queried separately. No build or gate errors were found.

## Files committed

- `include/PortMapping.h`
- `src/core/PortMapping.cpp`
- `tests/src/core/PortMappingCodecTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-297.md`
