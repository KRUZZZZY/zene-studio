# Cluster new-p12-24

**Verdict: FIXED**

`src/core/ControlCommandsLibraryClips.cpp:179-188` now refuses a symlink destination before writing, including when `overwrite` is true. This keeps `QSaveFile` from replacing the link and prevents undo from removing the saved path without restoring the original link. The existing-file/overwrite check was moved to `existsWithoutOverwrite` so `targetPath` remains within the complexity limit.

`tests/src/core/LibraryBrowserTest.cpp:119-141` creates a readable MIDI target and symlink, invokes `browser.save_clip` with overwrite enabled, and verifies the command is refused while the link and target contents remain intact.

## Evidence

CTest commands below ran from `build/tests`. The affected family is one CTest target; the final verbose run reports all 15 Qt test functions passing.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED build | `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/build-red-1.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"` | 0 | Rebuilt before the original-code regression run. |
| RED test | `ctest -R LibraryBrowserTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/test-red-1.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"` | 8 | `aSymlinkDestinationIsRefused` failed because the original implementation returned success. |
| Reverse production patch | `git diff -- src/core/ControlCommandsLibraryClips.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/fix.patch; reverse_status=$?; printf 'EXIT=%s\n' "$reverse_status"` | 0 | Reversed the final production patch for the proof run. |
| Reversed build | `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/build-reversed-3.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"` | 0 | Rebuilt with the fix reversed. |
| Reversed test | `ctest -R LibraryBrowserTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/test-reversed-3.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"` | 8 | The same assertion failed again. |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/fix.patch; apply_status=$?; printf 'EXIT=%s\n' "$apply_status"` | 0 | Restored the final fix. |
| GREEN build | `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/build-green-4.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"` | 0 | Final rebuild after restoring the patch. |
| GREEN test | `ctest -R LibraryBrowserTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/test-green-verbose-2.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"` | 0 | 1/1 CTest target passed; all 15 Qt test functions passed. |
| Initial complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/complexity-gate-1.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"` | 1 | `targetPath` reached CCN 11; the helper extraction below corrected it. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/complexity-gate-4.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"` | 0 | Passed; `targetPath` is no longer a new CCN regression. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-24.r1/logs/file-length-gate-4.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"` | 0 | Passed. |

The first complexity-gate run after adding the symlink check exited 1 because `targetPath` reached CCN 11. Extracting the existing overwrite predicate fixed the regression; the final gate run above exited 0. Log search used `rg` because no `fff` command/tool was available in this session.

## Files committed

- `src/core/ControlCommandsLibraryClips.cpp`
- `tests/src/core/LibraryBrowserTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-24.md`
