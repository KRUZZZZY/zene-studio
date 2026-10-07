# Cluster new-p8-98: ZIP writer reader-size mismatch

Verdict: **FIXED**.

## Evidence

`readWhole` refuses archives larger than `INT_MAX` at `src/core/DawProjectZip.cpp:235-253`. The writer previously checked only the classic ZIP limit, then narrowed the local-record offset at the former `centralOffset` line. The new `archiveSizeRefusal` at `src/core/DawProjectZip.cpp:184-201` accounts for every local record, central-directory record, and the end record, and is called at `:305-310` before archive assembly. Thus the offset at `:340` is within the reader's `int` range.

The regression at `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:1438-1457` uses a body that makes the local-record section `INT_MAX + 1` bytes. It failed on the original code because the writer accepted and wrote that archive. The same ctest failed again with only the production patch reversed, then passed after restoring it.

The affected ctest family contains one registered test (`1/1` on each run). The initial and reversal RED runs each reported 34 Qt cases passed and the new case failed; both GREEN runs passed the registered test.

| Evidence | Exact command and result |
| --- | --- |
| RED on original code | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` — **EXIT=8** |
| GREEN with fix | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` — **EXIT=0** |
| RED with fix reversed | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` — **EXIT=8** |
| GREEN after reapplying fix | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` — **EXIT=0** |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` — **EXIT=0** |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` — **EXIT=0** |

All four builds rebuilt `zene` and `DawProjectInterchangeRoundTripTest` before their corresponding ctest run; each build exited 0. The underlying command's exit is the `EXIT=N` value captured below, since the shell wrapper returns 0 to keep that value visible.

```sh
cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0
# EXIT=0
cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0
# EXIT=0
git diff -- src/core/DawProjectZip.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0
# EXIT=0
cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0
# EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0
# EXIT=0
cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-98.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0
# EXIT=0
```

Log searches used `rg -n 'error' --glob '*.log' . || true`, `rg -n 'FAILED' --glob '*.log' . || true`, and `rg -n 'Assertion' --glob '*.log' . || true` from the log folder. The fff grep tool was not exposed in this session; the `rg` searches found the expected RED failures and no assertion output.

## Files committed

- `src/core/DawProjectZip.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-98.md`
