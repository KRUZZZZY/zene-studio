# Cluster new-p12-184

Verdict: FIXED

The stale deletion failure remains reported by `rotateProjectRevision` in
`src/core/ProjectRevisions.cpp:129-136`. When that refusal occurs after rev0 was
retained, `saveTransaction` now records `previous_revision_replaced: 0` but sets
`reversible: false` and stores no inverse. Its mechanism explains that restore
would be refused by the same rotation failure. The save result still reports
`revision_kept: true` and the stale path in `revision_skipped`.

The regression in `tests/src/core/RevisionTimelineTest.cpp:483-517` creates a
non-empty directory at `.rev3`, saves over the project, and inspects the recorded
transaction through `control.transactions`. It asserts that no inverse is
offered and that `control.undo` refuses without changing the saved file.

## Evidence

Build and test logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs`.

| Check | Exact command | Exit and evidence |
| --- | --- | --- |
| Build | `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/build-3.log 2>&1` (same command for `build-4.log` through `build-6.log`) | 0 for each build. |
| RED on original product code | `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/test-3.log 2>&1` | 8; 10 Qt cases passed and the new case failed because the recorded save transaction was still reversible. |
| Initial GREEN | `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/test-4.log 2>&1` | 0 after the fix. |
| Reverse-patch proof | `git diff -- src/core/ControlCommandsProjectFiles.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/fix.patch` | 0; the same build command exited 0. Then `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/test-5.log 2>&1` exited 8 on the same assertion. |
| GREEN after restoring the fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/fix.patch` | 0; the same build command exited 0. Then `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/test-6.log 2>&1` exited 0. |
| Affected-family count | `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/test-7.log 2>&1` | 0; 1/1 registered ctest passed, with 11 QtTest cases passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/gate-4.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/gate-5.log 2>&1` | 0 |
| Fork-source manifest gate | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-184.r2/logs/gate-3.log 2>&1` | 0 |

The first two test drafts were discarded: `cd build/tests && ctest -R
'^RevisionTimelineTest$' --output-on-failure` with output in `test-1.log` exited
8 because the case changed the Engine's project path before a later test; the
same command with output in `test-2.log` exited 0 but inspected the private
result field that the registry strips. The valid regression was moved to the
final test slot and reads the recorded transaction instead. Those drafts are not
counted as regression proof.

## Files committed

- `src/core/ControlCommandsProjectFiles.cpp`
- `tests/src/core/RevisionTimelineTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-184.md`
