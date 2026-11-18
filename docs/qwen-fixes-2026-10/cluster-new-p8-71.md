# Cluster new-p8-71

Verdict: **FIXED**

## Evidence

- [src/core/ControlCommandsVcaEdit.cpp](../../src/core/ControlCommandsVcaEdit.cpp#L164) records the inverse operation as `control.undo` and now supplies an empty args object, matching that command's schema.
- [tests/src/core/ControlVcaEditGroupsTest.cpp](../../tests/src/core/ControlVcaEditGroupsTest.cpp#L129) moves both clips, reads the published inverse, dispatches its `op` and `args`, and verifies both clips return to their original tick.
- Before the fix, replay failed with `args: unexpected property 'clip'`.

## Regression evidence

| Run | Exact command | Exit |
| --- | --- | ---: |
| RED with the production fix reverted | `cd build/tests && ctest -R '^ControlVcaEditGroupsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/test-8.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| GREEN with the production fix restored | `cd build/tests && ctest -R '^ControlVcaEditGroupsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/test-10.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

The affected ctest family ran **1 CTest test** (`ControlVcaEditGroupsTest`); its final run passed. That test binary covers five functional QtTest slots.

The final proof builds were:

- `cmake --build build --target ControlVcaEditGroupsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/build-9.log 2>&1; result=$?; echo EXIT=$result; exit $result` — EXIT=0 (fix reverted).
- `cmake --build build --target ControlVcaEditGroupsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/build-10.log 2>&1; result=$?; echo EXIT=$result; exit $result` — EXIT=0 (fix restored).
- `cmake --build build --target zene ControlVcaEditGroupsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/build-11.log 2>&1; result=$?; echo EXIT=$result; exit $result` — EXIT=0 (final application and affected-test rebuild).

An earlier redundant command, `cmake --build build --target ControlVcaEditGroupsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit $result`, exceeded the tool's 30-second wait without surfacing an EXIT code. It was not used as evidence; later rebuilds of the same target returned EXIT=0 before their associated ctest runs.

Patch proof commands both exited 0:

- `git diff -- src/core/ControlCommandsVcaEdit.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result`

## Gates

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/gate-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` — EXIT=0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-71.r1/logs/gate-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` — EXIT=0.

The requested `fff` grep tool and executable were unavailable in this session, so bounded `rg` searches were used to verify the logged RED/GREEN assertions and ctest counts.

## Files in the commit

- `src/core/ControlCommandsVcaEdit.cpp`
- `tests/src/core/ControlVcaEditGroupsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-71.md`
