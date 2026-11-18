# Cluster new-p9-252

Verdict: **FIXED**.

`LfoGraph::toggleAmountModel()` now returns when `castModel<EnvelopeAndLfoParameters>()` is null. The graph can be clicked before binding or after its model is removed; `paintEvent()` already supports this unbound state. The regression is in `tests/src/gui/ModulatorPanelTest.cpp:82`, and the guard is at `src/gui/instrument/LfoGraph.cpp:189`.

## Verification evidence

All CTest commands ran from `build/tests`. The affected family matched one registered CTest target (`ModulatorPanelTest`); green runs reported 1/1 tests passed.

| Stage | Command | Exit | Evidence log |
| --- | --- | ---: | --- |
| RED before fix | `ctest -R ModulatorPanelTest --output-on-failure` | 8 | `test-1.log`: regression slot reached, then SIGSEGV signal 11 |
| GREEN with fix | `ctest -R ModulatorPanelTest --output-on-failure` | 0 | `test-2.log`: 1/1 passed |
| Reverse patch | `git diff -- src/gui/instrument/LfoGraph.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-252.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-252.r1/logs/fix.patch` | 0 | Patch removed |
| RED without fix | `ctest -R ModulatorPanelTest --output-on-failure` | 8 | `test-3.log`: regression slot again received SIGSEGV signal 11 |
| Reapply patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-252.r1/logs/fix.patch` | 0 | Patch restored |
| GREEN after restore | `ctest -R ModulatorPanelTest --output-on-failure` | 0 | `test-4.log`: 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-5.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-6.log` |

The build command `cmake --build build --target zene ModulatorPanelTest --parallel 12` exited 0 before the first red run, after the fix, after reversing the patch, and after restoring it (logs `build-3.log` through `build-6.log`); each command built or confirmed both targets up to date. The exploratory command `cmake --build build --target zene LfoGraphTest --parallel 12` exited 2 (`build-7.log`) because the configured tree has no standalone `LfoGraphTest` target; the regression was added to the existing instrument GUI test without reconfiguring.

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-252.r1/logs`.

## Files committed

- `src/gui/instrument/LfoGraph.cpp`
- `tests/src/gui/ModulatorPanelTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-252.md`
