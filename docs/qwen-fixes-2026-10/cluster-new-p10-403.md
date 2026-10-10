# Cluster new-p10-403

**Verdict: FIXED**

The historical `src/core/SurroundExport.cpp:99` and `channelModeFromName()` are absent on this branch. The corresponding public parser is `surround::formatFromName()` in [MixerSurround.cpp](../../src/core/MixerSurround.cpp:261). It dereferenced its output pointer after a valid name matched. It now returns `false` immediately for a null output pointer. The regression in [SurroundTest.cpp](../../tests/src/core/SurroundTest.cpp:196) supplies the valid name `5.1` and a null pointer.

The affected CTest family contains **1 CTest test** (`SurroundTest`, test #140). Both red runs segfaulted with the guard absent; both green runs passed 1/1.

## Evidence

Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs`. Build commands ran from the worktree root; CTest commands ran from `build/tests`.

| Step | Exact command | EXIT | Result |
|---|---|---:|---|
| Initial test build | `cmake --build build --target SurroundTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Built regression before the production change. |
| RED before fix | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | 1/1 CTest test; segfault on the valid format and null output. |
| Build with fix | `cmake --build build --target zene SurroundTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Built app and affected test target. |
| GREEN | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | 1/1 passed. |
| Remove fix for proof | `git diff -- src/core/MixerSurround.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/fix.patch; echo EXIT=$?` | 0 | Reversed only the production guard. |
| Rebuild without fix | `cmake --build build --target zene SurroundTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt app and test before the proof run. |
| RED without fix | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | 1/1 CTest test; same segfault. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/fix.patch; echo EXIT=$?` | 0 | Reapplied the saved production guard. |
| Final build | `cmake --build build --target zene SurroundTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt app and test with the fix restored. |
| Final GREEN | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-403.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | Passed. |

## Files in this commit

- `src/core/MixerSurround.cpp`
- `tests/src/core/SurroundTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-403.md`

The source and test files are already registered in `tests/fork-sources.txt`.
