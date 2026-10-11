# Cluster new-p9-392

**Verdict: FIXED**

`RmsHelper::setSize()` accepted zero at [include/RmsHelper.h:51](../../include/RmsHelper.h#L51), while `update()` indexes `m_buffer[m_pos]` and then takes `m_pos % m_size` at [include/RmsHelper.h:89](../../include/RmsHelper.h#L89). `setSize(0)` now throws `std::invalid_argument` before changing the helper. The construction and resize cases are covered by [DeviceDspTest.cpp:209](../../tests/src/core/DeviceDspTest.cpp#L209).

The configured test tree has no `DynamicsProcessor` ctest, so the regression was added to the existing DSP test target `DeviceDspTest`. CTest ran one affected-family test (`1/1`, test #194). The expected exception assertions both failed on the original code and failed again with only the product patch reversed.

## Regression evidence

Commands below ran from `build/tests`.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Original code, RED | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 8 |
| Fixed code, GREEN | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| Product patch reversed, RED | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 8 |
| Product patch reapplied, GREEN | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |

The RED logs show both `rmsHelperRejectsZeroAtConstruction()` and `rmsHelperRejectsZeroResize()` failed because no `std::invalid_argument` was thrown. Both GREEN runs passed with 100% of the one matched ctest passing.

## Build and gate evidence

Build commands ran from the worktree root. The initial build preceded the original-code RED run. Each later build rebuilt `zene`, the regression target, and both DynamicsProcessor targets consuming `RmsHelper.h`.

| Exact command | Exit |
| --- | ---: |
| `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| `cmake --build build --target zene DeviceDspTest dynamicsprocessor partc_ref_dynamicsprocessor > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| `cmake --build build --target zene DeviceDspTest dynamicsprocessor partc_ref_dynamicsprocessor > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| `cmake --build build --target zene DeviceDspTest dynamicsprocessor partc_ref_dynamicsprocessor > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/gate-1-complexity.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/gate-2-file-length.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |

The product-only patch proof used these exact commands, both exit 0:

```sh
git diff -- include/RmsHelper.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-392.r1/logs/fix.patch
```

The requested `fff grep` tool was not available in this session; build, test, and gate logs were searched with `rg`.

## Files in this commit

- `include/RmsHelper.h`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-392.md`
