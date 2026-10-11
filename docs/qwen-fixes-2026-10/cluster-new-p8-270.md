# Cluster new-p8-270

**Verdict: FIXED**

`fill` updates an input side's `channels` to the current capture plan but retains saved names past that count. `sideJson` emitted every retained name, so `audio.io_list` could report nonexistent inputs. Its channel loop now stops at both `side.names.size()` and `side.channels` in `src/core/AudioIoSetup.cpp:253`.

The regression in `tests/src/core/HardwareIoTest.cpp:321` stores eight input names with the two-channel capture plan, then checks the `audio.io_list` channel count against `requested`. Before the fix, it got 8 versus 2.

## Evidence

The CTest family contains one test target (`HardwareIoTest`, CTest #138); the red QtTest run executed 11 cases, with 10 passing and the regression failing.

| Check | Exact command | Exit |
|---|---|---:|
| Initial baseline build | `cmake --build build --target zene HardwareIoTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Initial RED | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/test-1.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 8 |
| GREEN build | `cmake --build build --target zene HardwareIoTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN test | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/test-2.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 0 |
| Reverse product patch | `git diff -- src/core/AudioIoSetup.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reversed build | `cmake --build build --target zene HardwareIoTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Reversed RED | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/test-3.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 8 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Restored build | `cmake --build build --target zene HardwareIoTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Restored GREEN | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/test-4.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-270.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The test log search used `rg` for `error`, `FAILED`, and `Assertion` because no fff grep tool was exposed in this session. Build, green test, and gate logs had no matches for those terms. The test log confirmed 100% passed (1/1) after the fix; both red runs reported the same 8-versus-2 mismatch.

## Files in this commit

- `src/core/AudioIoSetup.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-270.md`
