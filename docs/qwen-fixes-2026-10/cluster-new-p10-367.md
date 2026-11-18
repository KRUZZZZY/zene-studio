# Cluster new-p10-367

Verdict: **FIXED**

`src/gui/SendButtonIndicator.cpp:26` now returns before touching mixer routing when a mouse press is not `Qt::LeftButton`. Left-click toggling is unchanged. The regression at `tests/src/gui/MixerModesTest.cpp:234` sends right, middle, and left mouse presses to an indicator backed by an existing sender-to-return route: right and middle preserve the route, and left removes it.

The route assertion failed on the original handler and failed again when the production patch was reversed. It passed after the patch was reapplied. `tests/upstream-modifications.txt` records the inherited production-file change.

## Verification

All build commands ran from the worktree root. All CTest commands ran from `build/tests`. The affected family executed one CTest target (`MixerModesTest`); the original and reverse-patch runs each report 13 QtTest cases passed and the regression case failed, while fixed runs pass 1/1 CTest targets.

### Builds

| Log | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| `build-1.log` | `cmake --build build --target zene MixerModesTest -j2 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/build-1.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 2 | Initial test fixture used a deprecated Qt mouse-event constructor; corrected. |
| `build-2.log` | `cmake --build build --target zene MixerModesTest -j2 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/build-2.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Built the test against the original production code. |
| `build-3.log` | `cmake --build build --target zene MixerModesTest -j2 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/build-3.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 2 | Production handler needed a complete `QMouseEvent` type; added its include. |
| `build-4.log` | `cmake --build build --target zene MixerModesTest -j2 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/build-4.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Built the fixed code and test. |
| `build-5.log` | `cmake --build build --target zene MixerModesTest -j2 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/build-5.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Rebuilt after reversing the production patch. |
| `build-6.log` | `cmake --build build --target zene MixerModesTest -j2 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/build-6.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Rebuilt after reapplying the production patch. |

### RED, GREEN, and reverse-patch proof

Each command below ran as written, with output redirected to its named log and exit captured unpiped.

| Evidence | Exact command | Exit |
| --- | --- | ---: |
| Original RED | `ctest -R MixerModesTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/test-1.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Initial GREEN | `ctest -R MixerModesTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/test-2.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Reverse production patch | `git diff -- src/gui/SendButtonIndicator.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Reverse-patch RED | `ctest -R MixerModesTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/test-3.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Final GREEN | `ctest -R MixerModesTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/test-4.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |

The two RED logs fail at `MixerModesTest::sendIndicatorOnlyTogglesOnLeftPress`, where the right press has removed the existing send. The GREEN logs report 100% tests passed, 1/1.

### Ratchet gates

| Exact command | Exit |
| --- | ---: |
| `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/gate-1.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-367.r1/logs/gate-2.log' 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |

The `fff grep` tool was not available in this session; required log terms were searched with `rg`.

## Files committed

- `src/gui/SendButtonIndicator.cpp`
- `tests/src/gui/MixerModesTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-367.md`
