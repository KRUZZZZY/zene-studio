# Cluster new-p12-82: ExprSynth copy ownership

**Verdict: FIXED**

`ExprSynth::~ExprSynth()` deletes both owned expression pointers ([ExprSynth.cpp:736](../../plugins/Xpressive/ExprSynth.cpp#L736)), while the class previously allowed implicit shallow copies. Copy construction and copy assignment are now deleted ([ExprSynth.h:101](../../plugins/Xpressive/ExprSynth.h#L101)). The regression checks both type traits in `PluginPortsMigrationTest` ([PluginPortsMigrationTest.cpp:222](../../tests/src/plugins/PluginPortsMigrationTest.cpp#L222)). The production header change is declared in [upstream-modifications.txt:1054](../../tests/upstream-modifications.txt#L1054).

The suggested `ExprSynth` and `Xpressive` ctest names are not registered in this configured tree. The existing `PluginPortsMigrationTest` includes the Xpressive instrument and serves as the affected test family.

## Regression evidence

| Run | Command | Exit |
| --- | --- | ---: |
| Pre-fix build | `cmake --build build --target PluginPortsMigrationTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| RED before fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) | 8 |
| GREEN build | `cmake --build build --target zene xpressive PluginPortsMigrationTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| GREEN test | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) | 0 |

The required patch reversal also reproduced the bug. Reversing only the production header patch exited 0; rebuilding exited 0; the same ctest then failed on `ExprSynth` copy-constructibility (exit 8). Reapplying the patch exited 0, and the final build and ctest passed (both exit 0).

| Proof step | Command | Exit |
| --- | --- | ---: |
| Save and reverse production patch | `git diff -- plugins/Xpressive/ExprSynth.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Reversed build | `cmake --build build --target zene xpressive PluginPortsMigrationTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| RED with patch reversed | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Final build | `cmake --build build --target zene xpressive PluginPortsMigrationTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Final test | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) | 0 |

The affected ctest family had **1 registered test** (`PluginPortsMigrationTest`, CTest test #313); it passed after the fix. Both required ratchets passed:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — exit 0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-82.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — exit 0.

## Files committed

- `plugins/Xpressive/ExprSynth.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-82.md`
