# Cluster new-p12-184

Verdict: FIXED

`src/core/ProjectRevisions.cpp:63-77` now checks each stale revision removal and preserves the first failed path. At `src/core/ProjectRevisions.cpp:129-136`, rotation reports a refusal naming that path and the keep policy when pruning fails, while still reporting the revision 0 that was retained. The regression at `tests/src/core/RevisionTimelineTest.cpp:377-395` creates a non-empty directory at `.rev3`, making `QFile::remove()` fail deterministically, and checks the refusal and retained count.

## Evidence

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs`. CTest ran one registered test in the affected family (`RevisionTimelineTest`, 1/1).

| Check | Exact command and result |
| --- | --- |
| Initial regression build | `cmake --build build --target RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/build-1.log 2>&1; echo EXIT=$?` — `EXIT=0` |
| Initial RED | From `build/tests`: `ctest -R '^RevisionTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/test-1.log 2>&1; echo EXIT=$?` — `EXIT=8`; `rotationReportsFailureToRemoveStaleRevision()` failed because `refused` was false. |
| Initial GREEN build | `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/build-2.log 2>&1; echo EXIT=$?` — `EXIT=0` |
| Initial GREEN | From `build/tests`: `ctest -R '^RevisionTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/test-2.log 2>&1; echo EXIT=$?` — `EXIT=0` |
| Reverse-patch proof setup | `git diff -- src/core/ProjectRevisions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/fix.patch; echo EXIT=$?` — `EXIT=0` |
| Reverse-patch build | `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/build-3.log 2>&1; echo EXIT=$?` — `EXIT=0` |
| Reverse-patch RED | From `build/tests`: `ctest -R '^RevisionTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/test-3.log 2>&1; echo EXIT=$?` — `EXIT=8`; the same `refused` assertion failed. |
| Restore-fix command | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/fix.patch; echo EXIT=$?` — `EXIT=0` |
| Restore-fix build | `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/build-4.log 2>&1; echo EXIT=$?` — `EXIT=0` |
| Final GREEN | From `build/tests`: `ctest -R '^RevisionTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/test-4.log 2>&1; echo EXIT=$?` — `EXIT=0`; 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/gate-1.log 2>&1; echo EXIT=$?` — `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r1/logs/gate-2.log 2>&1; echo EXIT=$?` — `EXIT=0` |

The fff grep tool was unavailable in this session, so I searched the log folder with `rg` for `error`, `FAILED`, and `Assertion`; the only `FAILED` matches were the two expected RED runs.

## Files committed

- `src/core/ProjectRevisions.cpp`
- `tests/src/core/RevisionTimelineTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-184.md`
