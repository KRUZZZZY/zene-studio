# Cluster new-p8-10: browser index query tag types

**Verdict: FIXED**

`browser.index_query` declared `tags` as an array at
[`src/core/ControlCommandsBrowserIndex.cpp:237`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-10/src/core/ControlCommandsBrowserIndex.cpp:237), but the schema does not validate array elements. The old filter at lines 128–129 converted each element with `toString()`, so a number became an empty string and the command returned a query result instead of `invalid_args`.

The handler now checks every tag value before loading or filtering the index and returns `InvalidArgs` for non-strings at [`src/core/ControlCommandsBrowserIndex.cpp:134`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-10/src/core/ControlCommandsBrowserIndex.cpp:134). The regression is [`tests/src/core/ControlBrowserCommandsTest.cpp:221`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-10/tests/src/core/ControlBrowserCommandsTest.cpp:221); it sends `tags: ["drums", 4]` through `ControlRegistry` and expects `InvalidArgs`.

## Evidence

The affected CTest family is `ControlBrowserCommandsTest`: **1 CTest target**, containing 11 QtTest cases (the RED run reported 10 passing and the new case failing). The same command passed after the fix and after restoring the production patch during rollback proof.

| Check | Command | Exit |
|---|---|---:|
| Initial RED attempt | `ctest -R ControlBrowserCommandsTest --output-on-failure` from `build/tests` | 8 |
| Rebuilt `zene` and test target | `cmake --build build --target zene ControlBrowserCommandsTest` | 0 |
| RED after rebuilding `zene` | `ctest -R ControlBrowserCommandsTest --output-on-failure` from `build/tests` | 8 |
| Fixed build | `cmake --build build --target zene ControlBrowserCommandsTest` | 0 |
| GREEN | `ctest -R ControlBrowserCommandsTest --output-on-failure` from `build/tests` | 0 |
| Reverse production patch | `git diff -- src/core/ControlCommandsBrowserIndex.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-10.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-10.r1/logs/fix.patch` | 0 |
| Build with fix reversed | `cmake --build build --target zene ControlBrowserCommandsTest` | 0 |
| Rollback RED | `ctest -R ControlBrowserCommandsTest --output-on-failure` from `build/tests` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-10.r1/logs/fix.patch` | 0 |
| Build after restoring fix | `cmake --build build --target zene ControlBrowserCommandsTest` | 0 |
| Rollback-proof GREEN | `ctest -R ControlBrowserCommandsTest --output-on-failure` from `build/tests` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 |

Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-10.r1/logs` (`build-1.log` through `build-5.log`, `test-1.log` through `test-5.log`, and `gate-*.log`). The failing assertion was `ControlBrowserCommandsTest::indexQueryRejectsNonStringTags`: `!result.ok` returned false. The rollback run reproduced that assertion; both GREEN runs passed 1/1 CTest targets.

## Files in the commit

- `src/core/ControlCommandsBrowserIndex.cpp`
- `tests/src/core/ControlBrowserCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-10.md`
