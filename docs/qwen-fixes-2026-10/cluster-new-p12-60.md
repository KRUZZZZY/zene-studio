# Cluster new-p12-60: Asset hashing cap

**Verdict: FIXED**

`controlScanProjectAssets()` records the media size before `controlHashProjectAssets()` opens it. The previous hash path trusted that saved size and passed the file to an unbounded `QCryptographicHash::addData(&file)`. The new reader counts bytes as it hashes and reads no further than the configured cap plus one byte; it reports the reference unhashed as soon as it observes that extra byte.

## File evidence

- [ControlProjectAssets.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-60/src/core/ControlProjectAssets.cpp:295): `hashReference()` retains the scan-time fast rejection, then reads bounded chunks and counts bytes at lines 310-331. `controlHashProjectAssets()` uses it with `MaxProjectAssetHashBytes` at line 409.
- [ControlProjectAssetsShared.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-60/src/core/ControlProjectAssetsShared.h:102): declares the internal helper used by production and the deterministic test.
- [ControlProjectArchiveTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-60/tests/src/core/ControlProjectArchiveTest.cpp:357): scans a 5-byte file, grows it to 29 bytes, and verifies that a 16-byte cap leaves it unhashed.

## Regression and patch-removal evidence

The ctest family is `ControlProjectArchiveTest`; one CTest target ran. Its QtTest executable reported 13 passes and the regression failure in RED, so the family contains 14 QtTest cases. GREEN ctest runs passed the target (1/1).

| Phase | Exact command | Exit and observed result |
|---|---|---|
| RED, before bounded reads | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status` | `EXIT=8`; `fileGrowthAfterScanRespectsHashCap()` failed because `hashed` was true. Totals: 13 passed, 1 failed. |
| GREEN | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status` | `EXIT=0`; CTest reports 1/1 passed. |
| Fix reversed | `git diff -- src/core/ControlProjectAssets.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/fix.patch; status=$?; echo EXIT=$status` | `EXIT=0`. |
| RED, with only the fix reversed | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status` | `EXIT=8`; the same `hashed` assertion failed. |
| Fix reapplied | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/fix.patch; status=$?; echo EXIT=$status` | `EXIT=0`. |
| GREEN, after reapply | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status` | `EXIT=0`; CTest reports 1/1 passed. |

All ctest commands above ran from `build/tests`. The initial RED was preceded by a successful `ControlProjectArchiveTest` build. The fixed and patch-removal runs were each preceded by builds of both `zene` and `ControlProjectArchiveTest` after the production change was applied or reversed.

## Build record

Commands and exit codes, in order:

1. `mkdir -p /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs && cmake --build build --target ControlProjectArchiveTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status` — `EXIT=2` (internal-header include path corrected).
2. `cmake --build build --target ControlProjectArchiveTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status` — `EXIT=2` (control namespace qualifications corrected).
3. `cmake --build build --target ControlProjectArchiveTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status` — `EXIT=0`.
4. `cmake --build build --target zene ControlProjectArchiveTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status` — `EXIT=2` (deprecated Qt overload replaced).
5. `cmake --build build --target zene ControlProjectArchiveTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/build-5.log 2>&1; status=$?; echo EXIT=$status` — `EXIT=0`.
6. `cmake --build build --target zene ControlProjectArchiveTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/build-6.log 2>&1; status=$?; echo EXIT=$status` — `EXIT=0`.
7. `cmake --build build --target zene ControlProjectArchiveTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/build-7.log 2>&1; status=$?; echo EXIT=$status` — `EXIT=0`.

## Gates

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/gate-5.log 2>&1; status=$?; echo EXIT=$status` — `EXIT=0`.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-60.r1/logs/gate-6.log 2>&1; status=$?; echo EXIT=$status` — `EXIT=0`.

## Commit contents

- `src/core/ControlProjectAssets.cpp`
- `src/core/ControlProjectAssetsShared.h`
- `tests/src/core/ControlProjectArchiveTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-60.md`
