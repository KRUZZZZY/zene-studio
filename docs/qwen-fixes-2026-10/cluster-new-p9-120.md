# Cluster new-p9-120: Peak EQ at Nyquist

**Verdict: FIXED**

`EqPeakFilter::calcCoefficents()` in `plugins/Eq/EqFilter.h:272-300` now clamps its calculation frequency below Nyquist and uses identity coefficients if the sine or `sinh` argument is unsafe. This prevents nonfinite coefficients from reaching `StereoBiQuad` at the sample-rate boundary. The regression in `tests/src/plugins/EqFilterTest.cpp:29-34` sets a 16 kHz peak band at a 32 kHz sample rate and checks the rendered sample is finite.

The test failed before the fix with `std::isfinite(filter.update(...))` returning false. It passed after the fix, failed again with the production patch reversed, and passed after the patch was restored. The affected ctest family contains **1 test target** (`EqFilterTest`); the target runs one regression slot.

## Evidence

All ctest commands below ran from `build/tests`. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs`.

| Check | Exact command | Exit |
|---|---|---:|
| Pre-fix build (`build-4.log`) | `cmake --build build --target zene EqFilterTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Original RED (`test-1.log`) | `ctest -R '^EqFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Fixed build (`build-5.log`) | `cmake --build build --target zene eq EqFilterTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Fixed GREEN (`test-2.log`) | `ctest -R '^EqFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Save and reverse production patch (`fix.patch`) | `git diff -- plugins/Eq/EqFilter.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Reverted build (`build-6.log`) | `cmake --build build --target zene eq EqFilterTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Reverted RED (`test-3.log`) | `ctest -R '^EqFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Restored build (`build-7.log`) | `cmake --build build --target zene eq EqFilterTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Restored GREEN (`test-4.log`) | `ctest -R '^EqFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Complexity gate (`gate-5.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/gate-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File-length gate (`gate-6.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/gate-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

The RED assertion in both `test-1.log` and `test-3.log` is `std::isfinite(filter.update(1.0f, 0, 1.0f))` returning false. Both final ctest runs report 1/1 tests executed.

Initial build setup attempts, kept here separately from the regression evidence:

| Exact command | Exit | Observed reason |
|---|---:|---|
| `cmake --build build --target zene EqFilterTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 2 | The generated Makefile did not yet have an `EqFilterTest` target. |
| `cmake --build build --target help > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Refreshed the generated target list. |
| `cmake --build build --target zene EqFilterTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-120.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 2 | The new test needed to include `SampleFrame.h` before `EqFilter.h`; corrected before the pre-fix build recorded above. |

The fff grep tool was not exposed in this session (`command -v fff` returned exit 1), so logs were searched with separate `rg` queries for `error`, `FAILED`, `Assertion`, and test/gate pass counts.

## Registration and commit contents

The test is registered in `tests/CMakeLists.txt:325`, `tests/fork-sources.txt:2068`, and `tests/all-sources.txt:2658`. The fork-sources regenerate and verify recipes include its path. Reasons for changing inherited `plugins/Eq/EqFilter.h` and `tests/CMakeLists.txt` are recorded in `tests/upstream-modifications.txt:39` and `:765`.

Files committed:

- `plugins/Eq/EqFilter.h`
- `tests/src/plugins/EqFilterTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-120.md`
