# Cluster new-p8-231

Verdict: **FIXED**

The writer now rejects an absolute event position beyond the reader's supported
file-tick range before emitting the track. `include/SmfInterchange.h:131` sets
`SmfMaxFileTicks` to `0xFFFFFFFF`; `src/core/SmfInterchangeReader.cpp:323` refuses
a track once its accumulated position exceeds that value. The writer guard is at
`src/core/SmfInterchange.cpp:216`. Regression coverage is
`tests/src/core/SmfInterchangeTest.cpp:396`: the 4.3-billion-tick control writes
and reads, while the 4.4-billion-tick case verifies that the baseline writer's
file is rejected by the reader and requires the writer to refuse it without
leaving a file.

## Evidence

All ctests below ran from `build/tests`; build and test output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs`.

| Check | Exact command | Exit / result |
| --- | --- | --- |
| Reverse the product fix for sensitivity proof | `git diff -- src/core/SmfInterchange.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Build reverted `zene` and regression target | `cmake --build build --target zene SmfInterchangeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| RED: prove writer/reader disagreement without fix | `ctest -R '^SmfInterchangeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 8; the reader-refusal assertion passed, then the writer-refusal assertion failed (1 test run) |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Build fixed app and both SMF test targets | `cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| GREEN: focused regression | `ctest -R '^SmfInterchangeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 0; 1/1 passed |
| Affected test family | `ctest -R '^(SmfInterchangeTest|SmfInterchangeRoundTripTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/test-7.log 2>&1; echo EXIT=$?` | 0; 2/2 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/gate-5.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-231.r1/logs/gate-6.log 2>&1; echo EXIT=$?` | 0 |
| Whitespace check | `git diff --check` | 0 |

The `fff` grep helper was not exposed in this session and `command -v fff`
returned no path, so the build/test logs were searched with `rg` for `error`,
`FAILED`, and `Assertion`.

## Files in this commit

- `src/core/SmfInterchange.cpp`
- `tests/src/core/SmfInterchangeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-231.md`
