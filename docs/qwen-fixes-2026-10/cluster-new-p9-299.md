# Cluster new-p9-299

Verdict: **FIXED**

## Evidence

- `plugins/MultitapEcho/MultitapEchoControls.cpp:177` now recalculates filters from tap 0 through `m_steps.value() - 1`. The control allows 32 steps at `plugins/MultitapEcho/MultitapEchoControls.cpp:41`.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:174` builds a deterministic 32-step echo, and `tests/src/plugins/PluginPortsMigrationTest.cpp:773` compares its render after a rate change with a fresh instance at the new rate. The assertion is at line 798.
- `tests/upstream-modifications.txt:641` registers the inherited production-file change and its regression.

## Test evidence

The logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-299.r1/logs`.
Build commands ran from the worktree; ctest commands ran from `build/tests`.

| Check | Exact command | Exit |
|---|---|---:|
| Initial regression helper build (caught an unqualified `base64` namespace, then corrected) | `cmake --build build --target zene PluginPortsMigrationTest` | 2 |
| Pre-fix build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 |
| Original-code RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 |
| Fixed build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 |
| Fixed GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 |
| Save and reverse production patch | `git diff -- plugins/MultitapEcho/MultitapEchoControls.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-299.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-299.r1/logs/fix.patch` | 0 |
| Reverted build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 |
| Reverted RED proof | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-299.r1/logs/fix.patch` | 0 |
| Restored build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 |
| Restored GREEN proof | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 |

The RED run reported 8 passed and 1 failed: the new sample-rate regression. The reverted RED proof failed at the same assertion. Each GREEN run passed the single selected CTest target (`PluginPortsMigrationTest`, CTest #313). The affected family therefore ran 1 CTest target; the QtTest executable has 9 cases, with the RED summary showing 8 passing and the regression failing.

## Discovery note

The suggested `MultitapEchoControls` and `MultitapEchoControlDialog` ctest names do not exist in this configured tree. From `build/tests`, `ctest -N -R '^MultitapEcho(Controls|ControlDialog)$'` exited 0 and reported `Total Tests: 0`; the regression was added to the existing plugin migration test target instead. An initial `ctest -N -R '^MultitapEcho(Controls|ControlDialog)$'` discovery call from the worktree root exited 97 and ran no tests.

The requested `fff` grep tool was unavailable in this environment, so logs were searched with `rg` for the requested error and failure terms.

## Files committed

- `plugins/MultitapEcho/MultitapEchoControls.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-299.md`
