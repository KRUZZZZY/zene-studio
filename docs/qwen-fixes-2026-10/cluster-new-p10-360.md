# Cluster new-p10-360: scale child loading

**Verdict: FIXED**

`Scale::loadSettings` previously restored every child node as an `Interval`. Comments, text nodes,
unknown elements, and malformed `<interval>` elements therefore became default-like scale entries.
The loader now accepts only element children named `interval` with a complete, parseable ratio or
cents representation. Non-finite cents are skipped. Valid ratio and cents intervals retain their
previous behavior.

## File and line evidence

- `src/core/Scale.cpp:36-55` validates the persisted ratio and cents field shapes.
- `src/core/Scale.cpp:138-159` filters node type, tag name, and interval fields before restoring.
- `tests/src/core/ControlNoteScaleVerbsTest.cpp:185-218` covers valid ratio/cents intervals, an
  unknown element, comment/text nodes, and empty, incomplete, invalid, and non-finite intervals.

With the production patch removed, the regression measured 9 intervals instead of 2. With the
patch restored, the scale family passed.

## Verification command ledger

Commands below are copied from the final reversal proof and final gates. Each command writes its
full output to a new numbered log under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs`.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Save and remove production fix | `git diff -- src/core/Scale.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 | Patch removed for RED proof |
| RED build | `cmake --build build --target zene ControlNoteScaleVerbsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/17-final-red-build.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt with production fix removed |
| RED test | `ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/18-final-red-test.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | Regression failed: actual interval count 9, expected 2; 14 passed and 1 failed |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 | Exact saved production patch restored |
| GREEN build | `cmake --build build --target zene ControlNoteScaleVerbsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/19-final-restored-build.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt application and affected test |
| GREEN test | `ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/20-final-restored-test.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Affected family passed |
| Test count | `ctest -R '^ControlNoteScaleVerbsTest$' --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/21-final-test-count.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | 1 registered ctest; 15 Qt test cases passed, 0 failed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/22-final-complexity-gate.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Pass |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-360.r1/logs/23-final-file-length-gate.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Pass |

An initial regression placement in `SessionTuningTest` was discarded because that family also had an
unrelated instrument-creation failure after rebuilding `zene`; the final regression lives in the
scale vocabulary test family. The initial logs are `02-red-test.log` and `04-red-retest.log` (both
ctest exit 8). The final affected family above passes all 15 Qt cases.

## Files committed

- `src/core/Scale.cpp`
- `tests/src/core/ControlNoteScaleVerbsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-360.md`
