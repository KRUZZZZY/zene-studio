# Cluster new-p12-124

Verdict: **BLOCKED**. The bug is fixed and the regression proves both error paths, but the required global gates fail on unrelated baseline regressions. I did not change or re-anchor either gate.

## Finding and change

The finding is real. In `src/core/main.cpp:1405-1415`, `exportstems` returned for a missing `-o` destination or failed `QDir::mkpath()` after `crashreporter::beginSession()` and `safestart::beginSession()` had run, leaving both markers behind. Both refusal branches now call both `endSession()` functions before returning.

`tests/control-stem-export-verb.py:261-292` launches the real binary for each CLI refusal and asserts that both markers are absent after the expected failure. The test uses `tests/data/agent-control-fixture.mmp` and a regular file as the uncreatable destination's parent.

## Evidence

Affected-family test: `ControlStemExportVerb`; **1 test executed** on each CTest run.

| Run | Exact command | Exit | Result |
|---|---|---:|---|
| Original build | `cmake --build build --target zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/build-1.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/build-1.log'; exit $code` | 0 | Built before RED run. |
| Original RED | `cd build/tests && ctest -R '^ControlStemExportVerb$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/test-1.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/test-1.log'; exit $code` | 8 | Both refusals left `session=True safe-start=True`. |
| Build with fix | `cmake --build build --target zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/build-2.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/build-2.log'; exit $code` | 0 |  |
| Initial GREEN | `cd build/tests && ctest -R '^ControlStemExportVerb$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/test-2.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/test-2.log'; exit $code` | 0 | 1/1 passed. |
| Reverse patch | `git diff -- src/core/main.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/fix.patch'; code=$?; echo EXIT=$code; exit $code` | 0 | Reversed only the production fix. |
| Build without fix | `cmake --build build --target zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/build-3.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/build-3.log'; exit $code` | 0 |  |
| Required reverse RED | `cd build/tests && ctest -R '^ControlStemExportVerb$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/test-3.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/test-3.log'; exit $code` | 8 | Both marker assertions failed again (`session=True safe-start=True`). |
| Reapply patch | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/fix.patch'; code=$?; echo EXIT=$code; exit $code` | 0 |  |
| Final build | `cmake --build build --target zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/build-4.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/build-4.log'; exit $code` | 0 |  |
| Final GREEN | `cd build/tests && ctest -R '^ControlStemExportVerb$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/test-4.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/test-4.log'; exit $code` | 0 | 1/1 passed after reapplying the fix. |
| Complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/gate-1.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/gate-1.log'; exit $code` | 1 | Reports six unrelated new CCN regressions, including `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, and `ProjectRevisions.cpp`. None is changed here. |
| File-length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/gate-2.log' 2>&1; code=$?; echo EXIT=$code; echo EXIT=$code >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-124.r1/logs/gate-2.log'; exit $code` | 1 | Reports unrelated baseline entries over 500 lines, including `SmfInterchangeReader.cpp` (504) and `TakeLane.cpp` (520). Neither is changed here. |

The gate failures are outside this fix's diff; the checked diff is limited to `src/core/main.cpp`, `tests/control-stem-export-verb.py`, this allowlist reason, and this report. No gate threshold or baseline was changed.

## Files committed

- `src/core/main.cpp`
- `tests/control-stem-export-verb.py`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-124.md`
