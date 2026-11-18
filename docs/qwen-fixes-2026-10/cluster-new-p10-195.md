# Cluster new-p10-195: FIXED

The finding is real. In [LadspaPortDialog.cpp](../../plugins/LadspaBrowser/LadspaPortDialog.cpp), lines 42-55 identify LADSPA minimum, low, middle, high, and maximum defaults as bound-derived. Lines 109-125 now scale those defaults by the same output sample rate as their bounds. Fixed defaults such as 440 Hz keep their declared scale.

The regression is in `tests/src/gui/LadspaPortDialogTest.cpp`: it reads both values from the displayed Ports table, using a sample-rate-dependent middle default and a sample-rate-dependent fixed 440 Hz default. Before the fix, the middle value displayed as `0.5` while the scaled range expected `22050`; the fixed default remained `440`.

## Evidence

All CTest commands below ran from `build/tests`. CTest executed one target (`1/1`) in the affected family.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| First test-target build | `cmake --build build --target zene ladspabrowser LadspaPortDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/build-1.log 2>&1` | 2 | The target rule was not available during CMake regeneration. |
| Test compile retry | `cmake --build build --target zene ladspabrowser LadspaPortDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/build-2.log 2>&1` | 2 | The test needed concrete `AudioEngine` and LADSPA manager includes. |
| Test compile retry | `cmake --build build --target zene ladspabrowser LadspaPortDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/build-3.log 2>&1` | 2 | Removed constness from the manager pointer used by its non-const getters. |
| Baseline build | `cmake --build build --target zene ladspabrowser LadspaPortDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/build-4.log 2>&1` | 0 | Built the test against the original dialog before RED. |
| Original behavior (RED) | `ctest -R LadspaPortDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/test-1.log 2>&1` | 8 | Failed at the bound-derived default assertion: actual `0.5`, expected `22050`. |
| Fix build | `cmake --build build --target zene ladspabrowser LadspaPortDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/build-5.log 2>&1` | 0 | `zene`, the LADSPA browser plugin, and the regression target rebuilt. |
| Fixed behavior (GREEN) | `ctest -R LadspaPortDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/test-2.log 2>&1` | 0 | Passed, `1/1` CTest target. |
| Remove product patch | `git diff -- plugins/LadspaBrowser/LadspaPortDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/fix.patch` | 0 | Applied the inverse patch. |
| Inverse-proof build | `cmake --build build --target zene ladspabrowser LadspaPortDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/build-6.log 2>&1` | 0 | Rebuilt with the production fix removed. |
| Inverse proof (RED) | `ctest -R LadspaPortDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/test-3.log 2>&1` | 8 | The same assertion failed: actual `0.5`, expected `22050`. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/fix.patch` | 0 | Restored the production fix. |
| Restored build | `cmake --build build --target zene ladspabrowser LadspaPortDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/build-7.log 2>&1` | 0 | Rebuilt with the fix restored. |
| Restored behavior (GREEN) | `ctest -R LadspaPortDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/test-4.log 2>&1` | 0 | Passed, `1/1` CTest target. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/gate-1.log 2>&1` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/gate-2.log 2>&1` | 0 | Passed. |
| Source manifest reproduction | `bash tests/all-sources-reproduce.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/verify-1.log 2>&1` | 0 | Passed. |
| Upstream modification registration | `bash tests/no-upstream-regression-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/verify-2.log 2>&1` | 0 | Passed. |
| Source registration gate | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-195.r1/logs/verify-3.log 2>&1` | 0 | Passed. |

## Files committed

- `plugins/LadspaBrowser/LadspaPortDialog.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `tests/src/gui/LadspaPortDialogTest.cpp`
- `tests/src/plugins/SampleRateDependentLadspa.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-195.md`
