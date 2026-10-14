# Cluster new-p10-396: GUI-fed tuning source cap

**Verdict: FIXED**

## Finding and change

The finding was real. `SessionTuning::feedScale()` and `feedKeymap()` accepted sources longer than `MaxSourceLength`; `commit()` then truncated the recorded source after the built-from pointer had already changed. The GUI passes each edited scale or keymap description as that source.

Both feed methods now call `sourceFitsCap()` before building or changing state. An over-cap source is refused with a warning, leaving the table and built-from object untouched. The checks are at [SessionTuning.cpp:380](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-396/src/core/SessionTuning.cpp:380) and [SessionTuning.cpp:401](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-396/src/core/SessionTuning.cpp:401). Regression cases are at [SessionTuningTest.cpp:534](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-396/tests/src/core/SessionTuningTest.cpp:534) and [SessionTuningTest.cpp:562](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-396/tests/src/core/SessionTuningTest.cpp:562). The test file is recorded in `tests/upstream-modifications.txt` with its reason.

## Regression and family results

The focused RED run was performed with the product patch reversed and rebuilt. Both regression cases failed. Reapplying the same patch and rebuilding made both pass. The family ctest runs one CTest target; it has one separate existing failure in `loadScaleWhileTransportPlaysRetunesSoundingNotes()`, which cannot create its two instrument tracks in this environment. This failure occurred both before the fix and after it. The final family run reports 15 passed and 1 failed; the two focused regression cases pass.

Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs`.

| Stage | Exact command | Exit | Evidence |
|---|---|---:|---|
| Original-code family RED | `ctest -R '^SessionTuningTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-1.log; printf 'EXIT=%s\n' "$rc"` (cwd `build/tests`) | 8 | 1 CTest target; QtTest totals 13 passed, 3 failed. The two new assertions failed, along with the instrument-track slot. |
| Initial fixed family run | `ctest -R '^SessionTuningTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-2.log; printf 'EXIT=%s\n' "$rc"` (cwd `build/tests`) | 8 | 1 CTest target; 15 passed, 1 failed (the same instrument-track slot). |
| Initial focused GREEN | `./SessionTuningTest anOverCapScaleFeedLeavesTheSessionTuningUnchanged anOverCapKeymapFeedLeavesTheSessionTuningUnchanged > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-3.log; printf 'EXIT=%s\n' "$rc"` (cwd `build/tests`) | 0 | 4 passed, 0 failed |
| Focused RED after reversing fix | `./SessionTuningTest anOverCapScaleFeedLeavesTheSessionTuningUnchanged anOverCapKeymapFeedLeavesTheSessionTuningUnchanged > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-4.log; printf 'EXIT=%s\n' "$rc"` (cwd `build/tests`) | 2 | 2 regression failures |
| Focused GREEN after reapplying fix | `./SessionTuningTest anOverCapScaleFeedLeavesTheSessionTuningUnchanged anOverCapKeymapFeedLeavesTheSessionTuningUnchanged > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-5.log; printf 'EXIT=%s\n' "$rc"` (cwd `build/tests`) | 0 | 4 passed, 0 failed (includes QtTest init and cleanup) |
| Final family run | `ctest -R '^SessionTuningTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/test-6.log; printf 'EXIT=%s\n' "$rc"` (cwd `build/tests`) | 8 | 1 CTest target; 15 passed, 1 failed (the same instrument-track slot). |

## Build, patch proof, and gates

Each build command ran from the worktree root. Build output was redirected to the named log and the exit code appended there.

| Stage | Exact command | Exit |
|---|---|---:|
| Build regression against original code | `cmake --build build --target SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/build-1.log; printf 'EXIT=%s\n' "$rc"` | 0 |
| Build fixed app and test | `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/build-2.log; printf 'EXIT=%s\n' "$rc"` | 0 |
| Save product patch | `git diff -- src/core/SessionTuning.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| Rebuild with fix reversed | `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/build-3.log; printf 'EXIT=%s\n' "$rc"` | 0 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| Rebuild after reapplying | `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/build-4.log; printf 'EXIT=%s\n' "$rc"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/gate-1.log; printf 'EXIT=%s\n' "$rc"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-396.r1/logs/gate-2.log; printf 'EXIT=%s\n' "$rc"` | 0 |

The focused GREEN run before the reverse-patch proof is recorded in `test-3.log` (4 passed, 0 failed). No `fff grep` tool was exposed in this session; log output was inspected with term-limited `rg` searches.

## Files in the commit

- `src/core/SessionTuning.cpp`
- `tests/src/core/SessionTuningTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-396.md`
