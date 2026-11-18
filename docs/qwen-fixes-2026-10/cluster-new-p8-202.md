# Cluster new-p8-202

**Verdict: FIXED**

`differingTags()` previously retained a `QJsonObject` for every differing tag in a temporary array and only then copied up to 24 entries into the returned array. It now increments the full difference count while constructing JSON entries only while the returned array has fewer than `MaxComparedTags` items.

## Evidence

- [RevisionTimelineCompare.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-202/src/core/RevisionTimelineCompare.cpp:143): the helper initializes the bounded result array and total; lines 150-154 count every difference and construct/append a JSON object only inside the bound.
- [RevisionTimelineTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-202/tests/src/core/RevisionTimelineTest.cpp:299): compares documents with 32 distinct differing tags, checks that the total is 32 and the reported entries are the first 24, and checks that construction remains inside the bound. The source check is needed because returned JSON is identical before and after the fix, while the temporary allocation is otherwise unobservable.
- [fork-sources.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-202/tests/fork-sources.txt:2024): registers the fork-authored test source.

Affected ctest family: `RevisionTimelineTest`, **1 test run, 1 passed**. All test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-202.r1/logs`.

| Phase | Command and working directory | Exit | Log |
|---|---|---:|---|
| Initial build, before fix | `cmake --build build --target zene RevisionTimelineTest -- -j12` (worktree) | 0 | `build-1.log` |
| RED, before fix | `QT_QPA_PLATFORM=offscreen ctest -R '^RevisionTimelineTest$' --output-on-failure` (`build/tests`) | 8 | `test-1.log`; failed at `bound >= 0` |
| Build after fix | `cmake --build build --target zene RevisionTimelineTest -- -j12` (worktree) | 0 | `build-2.log` |
| Intermediate check iteration | `QT_QPA_PLATFORM=offscreen ctest -R '^RevisionTimelineTest$' --output-on-failure` (`build/tests`) | 8 | `test-2.log`; the new source check incorrectly expected `QJsonObject` text before its `reported.append` call; corrected afterward |
| Corrected GREEN | `cmake --build build --target zene RevisionTimelineTest -- -j12` then `QT_QPA_PLATFORM=offscreen ctest -R '^RevisionTimelineTest$' --output-on-failure` (`build/tests`) | 0 / 0 | `build-3.log`, `test-3.log` |
| Remove-fix proof | `git diff -- src/core/RevisionTimelineCompare.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-202.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-202.r1/logs/fix.patch` | 0 | `fix.patch` saved in the log folder |
| Rebuilt without fix | `cmake --build build --target zene RevisionTimelineTest -- -j12` (worktree) | 0 | `build-4.log` |
| RED, fix removed | `QT_QPA_PLATFORM=offscreen ctest -R '^RevisionTimelineTest$' --output-on-failure` (`build/tests`) | 8 | `test-4.log`; failed at `bound >= 0` |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-202.r1/logs/fix.patch` | 0 | — |
| Rebuilt with fix reapplied | `cmake --build build --target zene RevisionTimelineTest -- -j12` (worktree) | 0 | `build-5.log` |
| GREEN, fix reapplied | `QT_QPA_PLATFORM=offscreen ctest -R '^RevisionTimelineTest$' --output-on-failure` (`build/tests`) | 0 | `test-5.log`; ctest reports 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` (worktree) | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` (worktree) | 0 | `gate-2.log` |

The `fff grep` tool was not available in this session; the saved logs were searched with one-term `rg` searches for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/RevisionTimelineCompare.cpp`
- `tests/src/core/RevisionTimelineTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-202.md`
