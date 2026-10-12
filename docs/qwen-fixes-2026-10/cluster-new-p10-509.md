# Cluster new-p10-509

**Verdict: FIXED**

The finding is real. Finite `valueScale` and nonzero `timeScale` can produce a non-finite ratio, and a finite ratio or product can still exceed the stored `float` range. `carryTangents()` now calculates and checks the ratio and both products in `double`; it returns failure before converting an unsafe value to `float`. `transformRange()` then returns `0` without assigning its local replacement map to the clip.

Evidence locations:

- `src/core/AutomationClipTransforms.cpp:327` calculates and validates the locked tangent scale and results.
- `src/core/AutomationClipTransforms.cpp:377` refuses the transform when carrying either tangent is unsafe.
- `tests/src/core/AutomationLanesTest.cpp:351` adds the regression slot; `tests/src/core/AutomationClipTransformTangentTestSupport.h:28` checks refusal and that the clip remains unchanged.
- `tests/fork-sources.txt:1912` and `tests/all-sources.txt:2345` register the new test support header.

## Regression evidence

The affected CTest family is `AutomationLanesTest`: one CTest target running 21 QtTest cases. The RED run recorded 20 passing cases and the regression failing because the original implementation returned `1` instead of refusing with `0`. The GREEN run passed the target, including all 21 cases.

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| RED build, fix removed | `cmake --build build --target zene AutomationLanesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-509.r1/logs/build-9.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 | Rebuilt `zene` and the affected test target. |
| RED test, fix removed | `ctest -R AutomationLanesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-509.r1/logs/test-7.log 2>&1; test_rc=$?; echo EXIT=$test_rc` (run from `build/tests`) | 8 | 20 passed; the new regression failed on the original return value `1`. |
| GREEN build, fix restored | `cmake --build build --target zene AutomationLanesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-509.r1/logs/build-10.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 | Rebuilt `zene` and the affected test target. |
| GREEN test, fix restored | `ctest -R AutomationLanesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-509.r1/logs/test-8.log 2>&1; test_rc=$?; echo EXIT=$test_rc` (run from `build/tests`) | 0 | CTest reported 1/1 target passed. |

The required fix reversal was also performed. `git diff -- src/core/AutomationClipTransforms.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-509.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-509.r1/logs/fix.patch; reverse_rc=$?; echo EXIT=$reverse_rc` exited `0`; the no-fix build and RED test above followed. Reapplying with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-509.r1/logs/fix.patch; apply_rc=$?; echo EXIT=$apply_rc` exited `0`; the GREEN build and test followed.

## Ratchets

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-509.r1/logs/gate-complexity-3.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-509.r1/logs/gate-file-length-3.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc` | 0 |

The `fff` grep tool was not available in this session (`command -v fff; fff_rc=$?; echo EXIT=$fff_rc` exited `1`). Build and test logs were filtered with `rg -n` for `error`, `FAILED`, and `Assertion` instead of reading whole logs.

## Files committed

- `src/core/AutomationClipTransforms.cpp`
- `tests/src/core/AutomationLanesTest.cpp`
- `tests/src/core/AutomationClipTransformTangentTestSupport.h`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-509.md`
