# Cluster new-p9-99: DrumSynth envelope token overflow

**Verdict: FIXED**

`DrumSynth::GetEnv` in [DrumSynth.cpp](../../src/core/DrumSynth.cpp#L127) previously wrote each envelope token and its terminator into `s[8]` without a length check. It now stores at most seven characters and assigns zero to an overlong coordinate or level token. The public renderer regression in [DrumSynthTest.cpp](../../tests/src/core/DrumSynthTest.cpp#L37) feeds an eight-character level and verifies that it is rejected as silence.

The new test is registered in [tests/CMakeLists.txt](../../tests/CMakeLists.txt#L641) and [tests/fork-sources.txt](../../tests/fork-sources.txt#L1947). The inherited production and test-registration changes are recorded in `tests/upstream-modifications.txt`.

## Verification evidence

The affected CTest family contains **1 test** (`DrumSynthTest`). RED and GREEN refer to the same `ctest -R DrumSynthTest` invocation from `build/tests`.

| Phase | Exact command | Exit |
|---|---|---:|
| Initial build after adding the CMake target | `cmake --build build --target zene DrumSynthTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 2 |
| Repeated build after CMake's normal regeneration | `cmake --build build --target zene DrumSynthTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| RED on original parser | `ctest -R DrumSynthTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Build with fix | `cmake --build build --target zene DrumSynthTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN with fix | `ctest -R DrumSynthTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Save fix and reverse it | `git diff -- src/core/DrumSynth.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild without fix | `cmake --build build --target zene DrumSynthTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| RED after reversing only the product patch | `ctest -R DrumSynthTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Reapply saved fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild with restored fix | `cmake --build build --target zene DrumSynthTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN after restoring fix | `ctest -R DrumSynthTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/gate-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-99.r1/logs/gate-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

The initial build invocation caused CMake's automatic regeneration after `tests/CMakeLists.txt` changed; its first Make invocation did not yet expose the new target. Repeating the same build command succeeded. No explicit configure command was run. The RED assertion was `silent returned FALSE`; after fix removal, the identical test failed on that same assertion. The `fff` grep tool was not exposed in this session, so logs were searched with `rg`.

## Files committed

- `src/core/DrumSynth.cpp`
- `tests/src/core/DrumSynthTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-99.md`
