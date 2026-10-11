# Cluster new-p10-34: send pan lost on route removal

**Verdict: FIXED**

## Evidence and change

- [ControlCommandsMixerRoutes.cpp](../../src/core/ControlCommandsMixerRoutes.cpp#L316) records the removed route's pan and enabled state in `before`; its inverse args carry those values at lines 347-348.
- Lines 91-124 collect automation clips attached to the pan model from the song, pattern store, and global automation track. The undo closure at lines 321-331 restores the pan automation mode and reattaches those clips to the recreated pan model.
- [MixingRoutingTest.cpp](../../tests/src/core/MixingRoutingTest.cpp#L202) checks the `control.transactions` before state and inverse args. Its test at line 236 binds a real automation clip to send pan, removes the route, undoes, and checks that the recreated model is automated by that clip.

Both changed source files were already registered in `tests/fork-sources.txt` (source line 1185, test line 1968), so no source registry change was needed.

## Verification

| Purpose | Command | Exit | Evidence |
|---|---|---:|---|
| Regression RED before production fix | `cd build/tests && ctest -R MixingRoutingTest --output-on-failure` | 8 | `red-1.log`: both new checks failed (before pan was 0; automation was detached); 1/1 CTest target ran. |
| Regression GREEN after fix | `cd build/tests && ctest -R MixingRoutingTest --output-on-failure` | 0 | `restore-green-1.log`: 1/1 CTest target passed. |
| Test-family case count | `cd build/tests && ./MixingRoutingTest` | 0 | `family-2.log`: 12 passed, 0 failed. |
| Reverse production patch | `git diff -- src/core/ControlCommandsMixerRoutes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r2/logs/fix.patch` | 0 | Production hunks reversed; test changes remained. |
| Build with production patch reversed | `cmake --build build --target zene MixingRoutingTest` | 0 | `reverse-build-1.log`. |
| Reverse-patch regression RED | `cd build/tests && ctest -R MixingRoutingTest --output-on-failure` | 8 | `reverse-red-1.log`: both new checks failed again; 1/1 CTest target ran. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r2/logs/fix.patch` | 0 | Patch restored. |
| Final touched-target build | `cmake --build build --target zene MixingRoutingTest` | 0 | `restore-build-1.log`. |
| Final regression after patch restoration | `cd build/tests && ctest -R MixingRoutingTest --output-on-failure` | 0 | `restore-green-1.log`: 1/1 CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log`. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log`. |

The first test build exited 2 because the test needed an explicit `Song.h` include; its rerun exited 0 (`build-1.log`, `build-2.log`). The first production build exited 2 because the implementation needed an explicit `AutomationTrack.h` include; its rerun exited 0 (`build-3.log`, `build-4.log`). The first GREEN attempt exited 8 when the first pan assertion compared a serialized float to a decimal double; after changing both pan assertions to float precision, the build and test passed (`build-5.log`, `green-2.log`).

## Files committed

- `src/core/ControlCommandsMixerRoutes.cpp`
- `tests/src/core/MixingRoutingTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-34.md`
