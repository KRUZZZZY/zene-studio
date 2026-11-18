# Cluster new-p12-81

Verdict: **FIXED**

## Evidence and change

`ExprFront::m_data` is an owning raw pointer at [ExprSynth.h:58](../../plugins/Xpressive/ExprSynth.h:58), and `ExprFront::~ExprFront()` deletes it at [ExprSynth.cpp:561](../../plugins/Xpressive/ExprSynth.cpp:561). Before the fix, the implicit copy constructor and copy assignment duplicated that pointer. The class now deletes both copy operations at [ExprSynth.h:49](../../plugins/Xpressive/ExprSynth.h:49).

The regression at [PluginAudioPortsTest.cpp:178](../../tests/src/core/PluginAudioPortsTest.cpp:178) requires `ExprFront` to be neither copy-constructible nor copy-assignable. It failed on the original header, passed with the fix, failed again when the saved product patch was reversed, and passed after restoring it.

The named `ExprSynth` ctest is not registered in this configured tree. `tests/CMakeLists.txt:1533` places the Xpressive sources in the frozen Part C reference-module list; the inventory command below listed zero `ExprSynth`/`Xpressive` tests. Since reconfiguration was prohibited, the regression was added to the existing `PluginAudioPortsTest` target. Each executed ctest below, excluding the inventory-only `ctest -N` command, ran 1/1 registered targets.

## Verification

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-81.r1/logs`. The fff grep tool was not available in this session, so log matches were checked with `rg`.

| Phase | Command | Exit | Log / evidence |
|---|---|---:|---|
| Test inventory only | `ctest -N -R 'ExprSynth\|Xpressive'` | 0 | `inventory-1.log`; `Total Tests: 0` |
| Baseline build | `cmake --build build --target zene PluginAudioPortsTest -j4` | 0 | `build-1.log` |
| RED, original header | `ctest -R '^PluginAudioPortsTest$' --output-on-failure` | 8 | `test-1.log`; 1/1 ran, `exprFrontDoesNotShareOwnedData` failed because `ExprFront` was copy-constructible |
| Fixed build | `cmake --build build --target zene xpressive PluginAudioPortsTest -j4` | 0 | `build-2.log` |
| GREEN, fixed header | `ctest -R '^PluginAudioPortsTest$' --output-on-failure` | 0 | `test-2.log`; 1/1 passed |
| Reverse product patch | `git diff -- plugins/Xpressive/ExprSynth.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-81.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-81.r1/logs/fix.patch` | 0 | `proof-1.log` |
| Reversed-patch build | `cmake --build build --target zene xpressive PluginAudioPortsTest -j4` | 0 | `build-3.log` |
| RED, reversed patch | `ctest -R '^PluginAudioPortsTest$' --output-on-failure` | 8 | `test-3.log`; 1/1 ran, the same assertion failed |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-81.r1/logs/fix.patch` | 0 | `proof-2.log` |
| Final build | `cmake --build build --target zene xpressive PluginAudioPortsTest -j4` | 0 | `build-4.log` |
| GREEN, restored patch | `ctest -R '^PluginAudioPortsTest$' --output-on-failure` | 0 | `test-4.log`; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-5.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-6.log` |

## Files in the commit

- `plugins/Xpressive/ExprSynth.h`
- `tests/src/core/PluginAudioPortsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-81.md`
