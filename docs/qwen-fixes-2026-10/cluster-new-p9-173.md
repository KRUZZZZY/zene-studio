# Cluster new-p9-173: stale file search results

Verdict: **FIXED**

`FileSearchJob` now attaches the initiating query generation to each match. `FileBrowser` advances its generation on every search edit, including clearing the query, and ignores queued matches whose generation is no longer active.

Evidence:

- [`FileSearchJob.h`](../../include/FileSearchJob.h:63) and [`FileSearchJob.cpp`](../../src/gui/FileSearchJob.cpp:47) carry the generation through the worker; matches emit it at line 96.
- [`FileBrowser.cpp`](../../src/gui/FileBrowser.cpp:223) advances the generation; the queued result handler checks it at lines 251–253. The slot and generation member are declared in [`FileBrowser.h`](../../include/FileBrowser.h:87) and [line 126](../../include/FileBrowser.h:126).
- [`FileSearchJobTest.cpp`](../../tests/src/gui/FileSearchJobTest.cpp:40) queues a stale result to the actual browser slot and verifies the visible results stay empty.

## Regression evidence

`ctest -R FileSearchJobTest --output-on-failure` selects one CTest test. It failed on the original code, passed with the fix, failed again with the production patch reversed, and passed after that patch was restored. The two red runs failed at the queued stale-match delivery assertion in `FileSearchJobTest.cpp:69`.

| Run | Command and working directory | Exit | Log |
|---|---|---:|---|
| RED, original code | `ctest -R FileSearchJobTest --output-on-failure` (`build/tests`) | 8 | `test-1.log` |
| GREEN, fixed code | `ctest -R FileSearchJobTest --output-on-failure` (`build/tests`) | 0 | `test-2.log` |
| RED, patch reversed | `ctest -R FileSearchJobTest --output-on-failure` (`build/tests`) | 8 | `test-3.log` |
| GREEN, patch restored | `ctest -R FileSearchJobTest --output-on-failure` (`build/tests`) | 0 | `test-4.log` |

The test target and `zene` rebuilt with exit 0 after the fix, after patch reversal, and after patch restoration (`build-4.log`, `build-5.log`, `build-6.log`). The final CTest runs each report `1/1` passed.

The build tree did not initially know the newly registered test target: `cmake --build build --target FileSearchJobTest` exited 2 (`build-1.log`). Building the existing `BrowserTagMenuTest` target exited 0 (`build-2.log`) and ran the build system's normal regeneration check; the new test target then built with exit 0 (`build-3.log`). No manual configure command was run.

## Gates

| Gate | Command | Exit | Log |
|---|---|---:|---|
| Complexity | `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log` |
| File length | `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log` |

Build, test, and gate logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-173.r1/logs`. The `fff` grep tool was not available in this session; log searches used `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `include/FileBrowser.h`
- `include/FileSearchJob.h`
- `src/gui/FileBrowser.cpp`
- `src/gui/FileSearchJob.cpp`
- `tests/src/gui/FileSearchJobTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-173.md`
