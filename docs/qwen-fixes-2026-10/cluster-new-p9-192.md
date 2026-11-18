# Cluster new-p9-192

Verdict: **FIXED**

`src/tracks/InstrumentTrack.cpp:784` clamped the transposed key to `NumKeys`, even though valid keys end at `NumKeys - 1`. With the track base note one semitone below `DefaultKey`, transposing the top key produced 128. `sendNoteOn()` ignores that out-of-range key. The clamp now ends at `NumKeys - 1`.

`tests/src/tracks/HeadlessGuiGuardTest.cpp:81` covers the one-semitone upward shift through `applyMasterKey()`. On the original production code it reported `Actual (shifted.key()): 128`; with the fix it expects and gets `NumKeys - 1`.

## Verification

All build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-192.r1/logs`. The listed command is the exact build/test/gate command; its stdout and stderr were redirected to the named log, which also records the unpiped exit code.

| Stage | Command and working directory | Exit | Log |
|---|---|---:|---|
| Regression build before fix (worktree) | `cmake --build build --target HeadlessGuiGuardTest` | 0 | `build-1.log` |
| RED before fix (`build/tests`) | `ctest -R '^HeadlessGuiGuardTest$' --output-on-failure` | 8 | `test-1.log` |
| Fresh `zene` build before fix (worktree) | `cmake --build build --target zene` | 0 | `build-2.log` |
| RED after fresh build (`build/tests`) | `ctest -R '^HeadlessGuiGuardTest$' --output-on-failure` | 8 | `test-2.log` |
| GREEN after fix (worktree) | `cmake --build build --target zene HeadlessGuiGuardTest` | 0 | `build-3.log` |
| GREEN after fix (`build/tests`) | `ctest -R '^HeadlessGuiGuardTest$' --output-on-failure` | 0 | `test-3.log` |
| Reverse only product fix (worktree) | `git diff -- src/tracks/InstrumentTrack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-192.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-192.r1/logs/fix.patch` | 0 | command result |
| Rebuild without fix (worktree) | `cmake --build build --target zene HeadlessGuiGuardTest` | 0 | `build-4.log` |
| RED without fix (`build/tests`) | `ctest -R '^HeadlessGuiGuardTest$' --output-on-failure` | 8 | `test-4.log` |
| Reapply product fix (worktree) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-192.r1/logs/fix.patch` | 0 | command result |
| Rebuild with fix restored (worktree) | `cmake --build build --target zene HeadlessGuiGuardTest` | 0 | `build-5.log` |
| GREEN with fix restored (`build/tests`) | `ctest -R '^HeadlessGuiGuardTest$' --output-on-failure` | 0 | `test-5.log` |
| Complexity gate (worktree) | `bash tests/complexity-gate.sh --check` | 0 | `gate-5.log` |
| File length gate (worktree) | `bash tests/file-length-gate.sh --check` | 0 | `gate-6.log` |

Affected-family count: ctest ran **1 test target** (`HeadlessGuiGuardTest`); it passed after the fix. The required reverse-and-reapply proof reproduced the failure without the fix and passed with it restored.

## Files in the commit

- `src/tracks/InstrumentTrack.cpp`
- `tests/src/tracks/HeadlessGuiGuardTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-192.md`
