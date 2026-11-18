# Cluster new-p9-433: FIXED

## Finding and evidence

The finding is real. `exportEvents()` adds the required tick-zero seed when an active map starts after tick zero. `writeConductorTrack()` now checks the resulting event count against `TempoMap::MaxEvents` and refuses before serializing or writing an oversized file. A full map whose tick-zero event absorbs the seed remains exportable and importable.

- [src/core/SmfInterchange.cpp:410](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-433/src/core/SmfInterchange.cpp:410): capacity check after seed generation; refusal sets an error and returns before file writing.
- [src/core/SmfInterchange.cpp:434](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-433/src/core/SmfInterchange.cpp:434): matching reader limit in `mapFromEvents()`.
- [tests/src/core/SmfInterchangeTest.cpp:421](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-433/tests/src/core/SmfInterchangeTest.cpp:421): regression checks refusal/no file for a full map without tick zero, and successful import at the full-capacity tick-zero boundary.

## Test evidence

The regression was run against the original production code and failed because the writer accepted the oversized export. After the fix it passes. The reversible patch proof reproduced the same failure with the production fix removed, then passed after restoring it.

| Evidence | Exact command | Exit |
| --- | --- | ---: |
| RED on original code | `ctest -R '^SmfInterchangeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/test-1.log 2>&1` | 8 |
| RED with production patch reversed | `ctest -R '^SmfInterchangeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/test-3.log 2>&1` | 8 |
| GREEN after restoring production patch | `ctest -R '^SmfInterchange(Test|RoundTripTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/test-4.log 2>&1` | 0 |

The affected ctest family contains 2 tests: `SmfInterchangeTest` and `SmfInterchangeRoundTripTest`; both passed after the fix. The red QtTest run reported 9 passed and 1 failed, with the new capacity assertion as the failure.

## Build and reversal commands

| Exact command | Exit |
| --- | ---: |
| `cmake --build build --target SmfInterchangeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/build-1.log 2>&1` | 0 |
| `cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/build-2.log 2>&1` | 0 |
| `git diff -- src/core/SmfInterchange.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/fix.patch` | 0 |
| `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene SmfInterchangeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/build-3.log 2>&1` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/build-4.log 2>&1` | 0 |

## Required gates

| Exact command | Exit |
| --- | ---: |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/gate-1.log 2>&1` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-433.r1/logs/gate-2.log 2>&1` | 0 |

## Files in the cluster commit

- `src/core/SmfInterchange.cpp`
- `tests/src/core/SmfInterchangeTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-433.md`
