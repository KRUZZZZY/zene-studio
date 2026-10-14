# Cluster new-p10-185

Verdict: **FIXED**

## Evidence

- `plugins/Lb302/Lb302.cpp:231-232`: the constructor now connects `AudioEngine::sampleRateChanged` to both `filterChanged()` and `decayChanged()`.
- `plugins/Lb302/Lb302.cpp:299-303`: `decayChanged()` calculates `m_fs.envdecay` using `outputSampleRate()`.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:582`: `lb302DecayTracksSampleRateChanges()` renders paired active notes, changes the sample rate from 48 kHz to 24 kHz mid-note, and checks the instrument against a paired instance whose decay slot is explicitly refreshed.
- `tests/upstream-modifications.txt:1054`: records the reason for changing the inherited LB302 source. The existing product test is already registered in `tests/all-sources.txt`.

The active-note regression failed with the production connection removed: maximum sample delta `0.0452831686`. It passed with the connection restored. The affected CTest family is `PluginPortsMigrationTest`; CTest executed 1 test target (`1/1`).

## Commands and results

Commands below are copied with their working directory and exit code. Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs`.

| Purpose | Exact command and working directory | Exit / result |
|---|---|---|
| Initial RED before the first production edit | From `build/tests`: `QT_QPA_PLATFORM=offscreen ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/test-1.log' 2>&1; echo EXIT=$?` | 8; assertion failed, max delta `0.235386521` |
| Remove production fix for final regression proof | Worktree root: `git diff -- plugins/Lb302/Lb302.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/fix.patch'; echo EXIT=$?` | 0 |
| Rebuild without fix | Worktree root: `cmake --build build --target zene PluginPortsMigrationTest -- -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/build-6.log' 2>&1; echo EXIT=$?` | 0 |
| Final RED, same active-note regression | From `build/tests`: `QT_QPA_PLATFORM=offscreen ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/test-5.log' 2>&1; echo EXIT=$?` | 8; assertion failed, max delta `0.0452831686` |
| Restore production fix | Worktree root: `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/fix.patch'; echo EXIT=$?` | 0 |
| Final rebuild with fix | Worktree root: `cmake --build build --target zene PluginPortsMigrationTest -- -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/build-7.log' 2>&1; echo EXIT=$?` | 0 |
| Final GREEN | From `build/tests`: `QT_QPA_PLATFORM=offscreen ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/test-6.log' 2>&1; echo EXIT=$?` | 0; 1/1 test target passed |
| Complexity gate | Worktree root: `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/gate-6.log' 2>&1; echo EXIT=$?` | 0 |
| File-length gate | Worktree root: `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/gate-7.log' 2>&1; echo EXIT=$?` | 0 |
| Inherited-file registration gate | Worktree root: `bash tests/no-upstream-regression-gate.sh > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-185.r1/logs/gate-8.log' 2>&1; echo EXIT=$?` | 0 |

The `fff grep` tool was not available in this session; I searched the requested logs with `rg`, one term per search.

## Files in the fix commit

- `plugins/Lb302/Lb302.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-185.md`
