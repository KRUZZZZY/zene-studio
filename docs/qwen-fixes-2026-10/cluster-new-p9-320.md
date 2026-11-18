# Cluster new-p9-320: Nine Button Selector default

**Verdict: FIXED**

`plugins/Vibed/NineButtonSelector.cpp:39-42,49,66` clamps the constructor default to `[0, 8]` before initializing the model and indexing `m_buttons`. Values below the range select button 0; values above it select button 8.

The regression is `invalidDefaultSelectsNearestButton` in `tests/src/core/PluginLogoResourceTest.cpp:126-154`. It constructs selectors with `-1` and `9`, then checks that exactly the endpoint button's model is selected. The file under `tests/reference/Vibed` is frozen Part C reference source; this configured build has no `NineButtonSelector` CTest target, so the regression runs in the existing `PluginLogoResourceTest` target (one CTest test).

## Regression evidence

| Check | Command | Result |
|---|---|---|
| RED, fix reversed | `ctest -R '^PluginLogoResourceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-320.r1/logs/test-6.log 2>&1; result=$?; echo EXIT=$result` (run from `build/tests`) | `EXIT=8`; `invalidDefaultSelectsNearestButton` hit SIGSEGV |
| GREEN, fix restored | `ctest -R '^PluginLogoResourceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-320.r1/logs/test-7.log 2>&1; result=$?; echo EXIT=$result` (run from `build/tests`) | `EXIT=0`; 1/1 CTest tests passed |
| Final CTest | `ctest -R '^PluginLogoResourceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-320.r1/logs/test-8.log 2>&1; result=$?; echo EXIT=$result` (run from `build/tests`) | `EXIT=0`; 1/1 CTest tests passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-320.r1/logs/gate-5.log 2>&1; result=$?; echo EXIT=$result` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-320.r1/logs/gate-6.log 2>&1; result=$?; echo EXIT=$result` | `EXIT=0` |

The RED/GREEN proof used these patch commands:

```sh
git diff -- plugins/Vibed/NineButtonSelector.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-320.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-320.r1/logs/fix.patch; result=$?; echo EXIT=$result
# EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-320.r1/logs/fix.patch; result=$?; echo EXIT=$result
# EXIT=0
```

Builds for the RED run, restored GREEN run, and final verification each used `cmake --build build --target zene vibedstrings PluginLogoResourceTest` with output redirected to `build-9.log`, `build-10.log`, and `build-11.log` respectively; each exited `0`.

The test compiles the production selector implementation into the existing GUI test target. A test-only `Q_OBJECT` shim avoids changing the configured CMake graph; the selector signal is unused by this constructor regression.

`fff grep` was unavailable in the provided tools and shell. Build and test logs were searched with `rg` instead.

## Files in the commit

- `plugins/Vibed/NineButtonSelector.cpp`
- `tests/src/core/PluginLogoResourceTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-320.md`
