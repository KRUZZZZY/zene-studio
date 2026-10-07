# Cluster 5395 — Lead clip placement

**Verdict: FALSE-POSITIVE**

The finding does not match the source. In `tests/src/core/StemExportTestSupport.h:237-240`, the first Lead clip has the literal `pos="0"` and `len="%1"`; the second clip uses `%1` for its position and `%3` for its length. The arguments make the first clip run from bar 0 to bar 4 and the second from bar 4 to bar 6. `tests/src/core/StemExportTest.cpp:72` documents the six-bar fixture, and `stemsAreExportedNonSilentAndDistinct()` checks that the exported project length is six bars at line 148.

The source has been correct since the fixture was introduced: `git blame -L 237,240 -- tests/src/core/StemExportTestSupport.h` assigns these XML lines to `117068e767`, the original fixture addition. This is therefore a false positive, not a bug fixed by a later change.

No product or test source change was needed. The requested regression was not added and no RED run was made because the finding describes behavior the fixture does not have. In the original attempt, the existing `StemExportTest` was rebuilt and run. This documentation-only follow-up did not rebuild or rerun tests.

| Evidence | Command/result |
| --- | --- |
| RED regression | N/A — false-positive finding; the source was correct at fixture introduction, so there is no bug state to reproduce. |
| Build | `cmake --build build --target StemExportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5395.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0 |
| GREEN test | `cd build/tests && ctest -R '^StemExportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5395.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0; CTest ran 1/1 test and passed. |
| Affected-family count | 1 CTest test (`StemExportTest`), passed. |
| Complexity gate (follow-up) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5395.r2/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0 |
| File-length gate (follow-up) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5395.r2/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0 |
| Reverse-patch proof | N/A — no product patch was made. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-5395.md` only. |

`fff` was not available in the command/tool environment, so the saved build and test logs were searched with `rg` for `error`, `FAILED`, and `Assertion`. There were no `error` or `Assertion` matches; the `FAILED` search matched only the successful CTest summary, `0 tests failed out of 1`. The test log reports `1/1` passed and `100% tests passed`.
