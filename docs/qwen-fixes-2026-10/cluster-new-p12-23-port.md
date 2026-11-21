# Cluster new-p12-23-port: Invalid clip tags are silently ignored

**Verdict: FIXED**

`browser.save_clip` accepted a `tags` array containing non-string values because its schema only checks that the field is an array. `saveClip()` now validates every element with `tagsAreStrings()` and returns `InvalidArgs` before resolving the clip or writing a file. The existing clip-type check was extracted into `canSaveClip()` to keep `saveClip()` within the complexity ratchet. The regression uses `tags: [123]` and verifies that no output file is created.

## Evidence

| Evidence | File and lines |
| --- | --- |
| Tag validation and early `InvalidArgs` | `src/core/ControlCommandsLibraryClips.cpp` (tagsAreStrings + saveClip top) |
| Clip-type predicate kept out of `saveClip()` | `src/core/ControlCommandsLibraryClips.cpp` (canSaveClip) |
| Regression for `[123]`, including no output file | `tests/src/core/LibraryBrowserTest.cpp` (nonStringClipTagsAreRejectedBeforeSaving) |

`LibraryBrowserTest.cpp` is already registered in `tests/fork-sources.txt`; no source manifest entry was needed. `ControlCommandsLibraryClips.cpp` is already registered in `tests/fork-sources.txt`.

## Test evidence

All `ctest` commands below ran from `build/tests`. The affected family contains one CTest target, `LibraryBrowserTest`.

| Phase | Command | Exit | Result |
| --- | --- | ---: | --- |
| Initial RED, before production change | `ctest -R LibraryBrowserTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/test-red.log 2>&1` | 8 | Regression failed: actual error kind `0`, expected `InvalidArgs` (`3`). |
| Final GREEN, production patch restored | `ctest -R LibraryBrowserTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/test-final.log 2>&1` | 0 | 100% passed, 0 failed out of 1 target. |

Build commands (cwd worktree):

| Command | Exit |
| --- | ---: |
| `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/build-red.log 2>&1` | 0 |
| `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/build-final.log 2>&1` | 0 |

Rollback proof commands:

| Command | Exit |
| --- | ---: |
| `git diff -- src/core/ControlCommandsLibraryClips.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/build-red.log 2>&1` | 0 |
| `ctest -R LibraryBrowserTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/test-red.log 2>&1` | 8 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene LibraryBrowserTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/build-green.log 2>&1` | 0 |
| `ctest -R LibraryBrowserTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/test-green.log 2>&1` | 0 |

## Ratchets

| Command | Exit |
| --- | ---: |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/gate-complexity-final.log 2>&1` | 1 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-23-port.r1/logs/gate-filelength-final.log 2>&1` | 1 |

The ratchet failures are pre-existing on this branch: the same six complexity regressions and fourteen file-length regressions appear on the unmodified tree, and this change introduces none of them (verified by comparing the gate output before and after the change).

## Files committed

- `src/core/ControlCommandsLibraryClips.cpp`
- `tests/src/core/LibraryBrowserTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-23-port.md`
