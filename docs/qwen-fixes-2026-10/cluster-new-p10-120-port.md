# Cluster new-p10-120-port: EQ frequencies above Nyquist

**Verdict: FIXED**

`plugins/Eq/EqFilter.h` now clamps the effective frequency to the active sample rate's Nyquist frequency. The requested frequency is retained separately (`m_requestedFreq`), so lowering the sample rate clamps the filter and raising it can restore the requested value. Clamping is applied in `setSampleRate`, `setFrequency`, and `setParameters` of the base `EqFilter`, and in `EqPeakFilter`'s overridden `setParameters`. `EqPeakFilter::calcCoefficents` returns identity coefficients when the (clamped) frequency reaches Nyquist. `clampFrequency` is a protected static helper.

Regression: `tests/src/plugins/EqFilterTest.cpp` (six slots: existing Nyquist-finite peak case plus five new cases covering bulk/live/sample-rate/peak clamping). Target registered in `tests/CMakeLists.txt` and source listed in `tests/fork-sources.txt`; `tests/upstream-modifications.txt` updated.

## Verification

All ctest commands ran from `build/tests`. Affected family: one CTest target `EqFilterTest` with six regression slots.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| GREEN after fix (six slots) | `ctest -R '^EqFilterTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | 0 |
| Reverse-patch RED (five new slots fail) | `ctest -R '^EqFilterTest$' --output-on-failure > .../logs/test-rev.log 2>&1; echo EXIT=$?` | 8 |
| Restored-patch GREEN | `ctest -R '^EqFilterTest$' --output-on-failure > .../logs/test-restored.log 2>&1; echo EXIT=$?` | 0 |

Reverse proof commands (both exited 0):

```sh
git diff -- plugins/Eq/EqFilter.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-120-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-120-port.r1/logs/fix.patch; echo EXIT=$?
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-120-port.r1/logs/fix.patch; echo EXIT=$?
```

Builds (all exited 0):

```sh
cmake --build build --target EqFilterTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?
cmake --build build --target zene -j4 > .../logs/build-zene.log 2>&1; echo EXIT=$?
```

Ratchets (pre-existing failures on this branch, unchanged by this fix; no new regressions introduced):

```sh
bash tests/complexity-gate.sh --check > .../logs/gate-ccn-now.log 2>&1; echo EXIT=$?  # EXIT=1
bash tests/file-length-gate.sh --check > .../logs/gate-fl-now.log 2>&1; echo EXIT=$?  # EXIT=1
```

## Files committed

- `plugins/Eq/EqFilter.h`
- `tests/src/plugins/EqFilterTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-120-port.md`
