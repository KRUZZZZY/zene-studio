# Cluster new-p8-217

**Verdict: FIXED**

Lua strings were converted with NUL-terminated APIs on both sides of the JSON bridge. `fromLuaString` now uses `lua_tolstring`'s byte length, and `pushString` uses `lua_pushlstring`; object keys and command IDs also use length-aware conversions. See [ScriptCommandBridge.cpp](../../src/core/ScriptCommandBridge.cpp:56), including Lua table key conversion at line 148, JSON string output at line 206, and command ID input at line 276. The regression at [ScriptCommandBridgeTest.cpp](../../tests/src/core/ScriptCommandBridgeTest.cpp:110) checks Lua-to-command and command-result-to-Lua strings containing embedded NUL bytes.

## Build and test evidence

All build and test output was redirected to new numbered files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs`. CTest commands ran from `build/tests`.

| Stage | Command | Exit |
| --- | --- | ---: |
| Initial regression draft build (`build-1.log`) | `cmake --build build --target ScriptCommandBridgeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/build-1.log 2>&1` | 0 |
| Initial draft run (`test-1.log`): Lua runtime error because `tostring` was called with no value; corrected before using the regression as evidence | `ctest -R '^ScriptCommandBridgeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/test-1.log 2>&1` | 8 |
| RED build (`build-2.log`) | `cmake --build build --target ScriptCommandBridgeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/build-2.log 2>&1` | 0 |
| RED (`test-2.log`): expected `true 3 0`, got `true 1 -1` | `ctest -R '^ScriptCommandBridgeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/test-2.log 2>&1` | 8 |
| GREEN build (`build-3.log`) | `cmake --build build --target zene ScriptCommandBridgeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/build-3.log 2>&1` | 0 |
| GREEN (`test-3.log`) | `ctest -R '^ScriptCommandBridgeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/test-3.log 2>&1` | 0 |
| Inverse-patch capture | `git diff -- src/core/ScriptCommandBridge.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/fix.patch` | 0 |
| Inverse-patch apply | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/fix.patch` | 0 |
| Inverse build (`build-4.log`) | `cmake --build build --target zene ScriptCommandBridgeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/build-4.log 2>&1` | 0 |
| Inverse RED (`test-4.log`): same regression failed with `true 1 -1` | `ctest -R '^ScriptCommandBridgeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/test-4.log 2>&1` | 8 |
| Patch restore | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/fix.patch` | 0 |
| Restored build (`build-5.log`) | `cmake --build build --target zene ScriptCommandBridgeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/build-5.log 2>&1` | 0 |
| Restored GREEN (`test-5.log`) | `ctest -R '^ScriptCommandBridgeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/test-5.log 2>&1` | 0 |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/gate-1.log 2>&1` | 0 |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-217.r1/logs/gate-2.log 2>&1` | 0 |

The affected CTest family ran 1/1 target. The `ScriptCommandBridgeTest` QtTest suite has 8 cases; the RED run showed 7 passing and the regression failing, and both GREEN runs passed the target.

The requested `fff grep` tool was not exposed in this session, so log checks used `rg` with individual search terms.

## Files in commit

- `src/core/ScriptCommandBridge.cpp`
- `tests/src/core/ScriptCommandBridgeTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-217.md`
