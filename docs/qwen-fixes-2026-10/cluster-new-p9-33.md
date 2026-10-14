# Cluster new-p9-33

Verdict: **FIXED**

`mixer.cue_master` previously accepted `name`, `level`, `output`, or `follow_listen` without a `cue` and returned success without changing a master. `readCueState` now returns `InvalidArgs` for those arguments unless `cue` is supplied. A `listen_to`-only request still works without a cue.

Evidence:

- [src/core/ControlCommandsCueMix.cpp:150](../../src/core/ControlCommandsCueMix.cpp#L150) identifies master fields; [src/core/ControlCommandsCueMix.cpp:165](../../src/core/ControlCommandsCueMix.cpp#L165) rejects them when `cue` is absent.
- [tests/src/core/HardwareIoTest.cpp:190](../../tests/src/core/HardwareIoTest.cpp#L190) covers the reported `{"name":"Phones"}` call and expects `InvalidArgs`.
- The full CTest family contains two tests: `HardwareIoTest` and `ControlHardwareIo`.

## Verification

Build and test commands were run from the project root and `build/tests`, respectively. All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Build regression test before fix | `cmake --build build --target HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Built test with original production code |
| RED before fix | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | Regression failed as expected: actual error kind 0, expected 3; 11 Qt cases passed and 1 failed |
| Build after fix | `cmake --build build --target HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Built fixed test |
| GREEN after fix | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest target passed |
| Remove final product patch | `git diff -- src/core/ControlCommandsCueMix.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/fix.patch; echo EXIT=$?` | 0 | Prepared final-patch reversion proof |
| Build with fix removed | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 | Rebuilt app and test target |
| RED with fix removed | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 8 | Same regression assertion failed |
| Restore final product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored fix |
| Build with fix restored | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 | Rebuilt app and test target |
| GREEN with fix restored | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest target passed |
| Full affected family | `ctest -R HardwareIo --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/test-7.log 2>&1; echo EXIT=$?` | 0 | 2/2 tests passed |
| Complexity gate, first implementation | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 1 | Flagged `readCueState` at CCN 11; extracted the field check into a helper |
| File-length gate, first implementation | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | No regression |
| Complexity gate, final implementation | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 | No regression |
| File-length gate, final implementation | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-33.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 | No regression |

Files committed: `src/core/ControlCommandsCueMix.cpp`, `tests/src/core/HardwareIoTest.cpp`, and this report.
