# Cluster new-p12-255 — Concurrent downloads overwrite the same partial file (round 4 rework)

**Verdict: BLOCKED.** The rename-loser cleanup at `StemModelStore.cpp:454` is correct, but I cannot create a deterministic regression test that exercises this path on Linux.

## Analysis

The rename-loser path is hit when:
1. Thread A calls `QFile::remove(finalPath)` (line 445)
2. Thread B calls `QFile::rename(partB, finalPath)` and succeeds (installing the file)
3. Thread A calls `QFile::rename(partA, finalPath)` and **fails**
4. Thread A calls `verifiedInPlace(spec, destDir)` which returns **true**
5. Thread A calls `QFile::remove(partPath)` (the fix at line 454)

On Linux, `QFile::rename` uses the `rename(2)` syscall which is **atomic**. If the destination doesn't exist, the rename always succeeds. If the destination exists, the rename atomically replaces it. This means step 3 almost never fails on Linux.

For step 3 to fail and step 4 to return true, the following must happen:
- Thread A removes the final file (line 445)
- Thread B creates the final file between A's remove and A's rename
- Thread A's rename fails even though the destination now exists

On Linux, this scenario is virtually impossible because:
1. The window between A's remove and A's rename is tiny (microseconds)
2. Even if B creates the file in that window, A's rename would atomically replace it (not fail)

## Attempts

I tried several approaches to create a deterministic test:

1. **Concurrent downloads with many threads/iterations**: The existing test already does this (10 iterations, 2 threads). QA reported it passes 10/10 times without the fix. Increasing to 50 iterations and 10 threads is unlikely to help.

2. **LD_PRELOAD to intercept rename/unlink**: Created a shared library to intercept syscalls and force the rename to fail. However:
   - Timing is extremely difficult to control
   - The download function must create the staging file before interception
   - The final file must exist when `verifiedInPlace` is called
   - Multiple attempts failed due to timing issues

3. **Manual file system manipulation**: Tried to manually create the conditions (staging file + final file) and call the download function. However, the download function is a single atomic operation that can't be paused mid-execution.

## Conclusion

The rename-loser cleanup at line 454 is correct defensive programming for cross-platform scenarios (Windows, network file systems, etc.), but on Linux with local file systems, this path is essentially unreachable through normal concurrent downloads.

The existing test `testConcurrentSuccessfulDownloadsLeaveNoStagingFile` provides probabilistic coverage, but cannot deterministically exercise the rename-loser path on Linux.

## Evidence table

| Check | Command (cwd) | Exit | Result |
|---|---|---:|---|
| Production code | `git show 99a9a1cba:src/core/StemModelStore.cpp \| sed -n '450,456p'` | 0 | Shows the fix: `QFile::remove(partPath);` at line 454 |
| Existing tests | `ctest -R '^StemModelStoreTest$'` (build/tests) | 0 | 21 passed, 0 failed |
| Full suite | `ctest -R '^Stem'` | 0 | 4/4 passed |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-255.md` (this report)

## Recommendation

The production fix at `StemModelStore.cpp:454` should remain as it provides correct cleanup for the rename-loser path on platforms where `rename()` is not atomic (Windows) or in edge cases (network file systems, permission issues). However, a deterministic regression test for this specific path on Linux is not feasible without significant infrastructure changes (custom file system wrappers, extensive mocking, etc.).

The existing concurrent download test provides reasonable coverage for the more common scenario where both downloads succeed without hitting the rename-loser path.
