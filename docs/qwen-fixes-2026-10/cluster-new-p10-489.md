# Cluster new-p10-489 — Rejected settings can still erase existing input pins

**Verdict: FIXED**

## Finding

In `AudioPortsModel::loadSettings`, parsing `inputs="2" outputs="134"` updates
`inputs` to 2, rejects the out-of-range output value, and leaves the initialized
`outputs` value at 0. For a model with two input ports and zero output ports,
the old equality-only guard matched both current counts and loaded the empty input
matrix. `Matrix::loadSettings` clears all pins before applying matrix attributes,
so existing input routing was erased even though the count parse had failed.

## Fix and regression

The matrix load now requires both `countsInRange` and equality between the
current counts and the parsed counts. The regression
`AudioPortsModelTest::outOfRangeCountsKeepExistingPins` uses
`TestAudioPortsModel{2, 0, false}`, loads `inputs="2" outputs="134"` with empty
matrices, and verifies both existing diagonal input pins remain enabled.

## File:line evidence

- `src/core/AudioPortsModel.cpp:226-249` — `loadSettings` parses the counts,
  applies valid counts, and now guards both matrix loads with
  `countsInRange && in().channelCount() == inputs && out().channelCount() == outputs`.
- `src/core/AudioPortsModel.cpp:537-541` — `Matrix::loadSettings` fills all pins
  with false before applying XML attributes.
- `tests/src/core/AudioPortsModelTest.cpp:528-543` —
  `outOfRangeCountsKeepExistingPins` covers the rejected output count and empty
  input matrix.

## Verification evidence

All build and test logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs`.
CTest commands ran from `build/tests`. The affected family contains one
registered CTest test (`AudioPortsModelTest`); its QtTest run reported 19 cases
in the red run (18 passed and the regression failed). Green ctest runs returned
success for the same family.

| Evidence | Exact command | Exit |
| --- | --- | ---: |
| RED build | `cmake --build build --target AudioPortsModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| RED test | `ctest -R '^AudioPortsModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 |
| GREEN build | `cmake --build build --target zene AudioPortsModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| GREEN test | `ctest -R '^AudioPortsModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Reverse only the product fix | `{ git diff -- src/core/AudioPortsModel.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"; } > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/proof-1.log 2>&1` | 0 |
| Rebuild without product fix | `cmake --build build --target zene AudioPortsModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Proof test without product fix | `ctest -R '^AudioPortsModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/fix.patch > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/proof-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Rebuild with product fix restored | `cmake --build build --target zene AudioPortsModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Proof test with product fix restored | `ctest -R '^AudioPortsModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-489.r2/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |

The red runs failed specifically at
`AudioPortsModelTest::outOfRangeCountsKeepExistingPins` because input pin
`(0, 0)` was false. Both green runs passed the focused CTest family.

## Files committed

- `src/core/AudioPortsModel.cpp`
- `tests/src/core/AudioPortsModelTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-489.md`
