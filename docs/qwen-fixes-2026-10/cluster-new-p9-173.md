# Cluster new-p9-173: stale file search results

Verdict: **FIXED**

`FileSearchJob` carries the initiating generation with each match. `FileBrowser` advances the generation for every search edit and discards a queued result if its generation is no longer current. The fix is in [`FileSearchJob.h`](../../include/FileSearchJob.h:63), [`FileSearchJob.cpp`](../../src/gui/FileSearchJob.cpp:47), and [`FileBrowser.cpp`](../../src/gui/FileBrowser.cpp:223) / [the result handler](../../src/gui/FileBrowser.cpp:251).

## Regression proof

The regression in [`FileSearchJobTest.cpp`](../../tests/src/gui/FileSearchJobTest.cpp:67) uses the actual search UI and queued `foundMatch` connection. Its event filter waits for the first search's queued result call, starts a second search before that call reaches `onSearchMatch`, and checks that the stale file is absent. This works with the original API, where `onSearchMatch(const QString&)` was a private method rather than a Qt meta-object slot. With the original product code, the queued result is inserted and the assertion reports actual count `1`, expected `0`. With generation tagging restored, the old result is discarded.

The r1 production fix was already committed as `7980fcd16`, so the worktree diff for those product files was empty. To perform the required reversal proof without rewriting history, `fix.patch` was generated from that commit's product-file delta, reversed for the RED run, then reapplied for GREEN.

| Evidence | Exact command | Exit | Result / log |
|---|---|---:|---|
| Save committed product delta | `git diff 7980fcd16^ 7980fcd16 -- include/FileBrowser.h src/gui/FileBrowser.cpp include/FileSearchJob.h src/gui/FileSearchJob.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-173.r2/logs/fix.patch` | 0 | `fix.patch` |
| Reverse product fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-173.r2/logs/fix.patch` | 0 | `apply-reverse.log` |
| Build original product API and regression | `cmake --build build --target zene FileSearchJobTest` | 0 | `build-3.log` |
| RED, from `build/tests` | `ctest -R '^FileSearchJobTest$' --output-on-failure` | 8 | `test-3.log`: `Actual (results->topLevelItemCount()): 1`, expected `0`; 1/1 CTest test failed |
| Reapply product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-173.r2/logs/fix.patch` | 0 | `apply-forward.log` |
| Build fixed product and regression | `cmake --build build --target zene FileSearchJobTest` | 0 | `build-4.log` |
| GREEN, from `build/tests` | `ctest -R '^FileSearchJobTest$' --output-on-failure` | 0 | `test-4.log`: 1/1 CTest test passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log` |

The affected CTest family contains one test, `FileSearchJobTest`; the final GREEN run reports 1/1 passed. No full suite was run.

An earlier test draft used `indexOfSlot("onSearchMatch(quint64,QString)")`; on the original code it failed at the signature check because the old handler was not a registered slot. That run (`ctest -R '^FileSearchJobTest$' --output-on-failure`, exit 8, `test-2.log`) was not treated as RED evidence; the event-filter regression above replaced it.

Build/test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-173.r2/logs`. The `fff` grep tool was unavailable in this session, so logs were searched with `rg` for build errors, assertion details, CTest counts, and gate failures.

## Files in this commit

- `tests/src/gui/FileSearchJobTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-173.md`
