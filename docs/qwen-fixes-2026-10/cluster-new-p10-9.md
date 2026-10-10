# Cluster new-p10-9 — FIXED

## Finding

`src/core/CollabLink.cpp:147` compared the queued byte count plus the payload size against `MaxQueuedBytes`, then wrote both the payload and a newline. At the exact payload boundary, that allowed one extra queued byte. The check now includes the framing newline.

Regression coverage is in `tests/src/core/CollabTransportTest.cpp:132`. Its connected test socket reports `MaxQueuedBytes - 1` pending bytes; a one-byte line must close the link because the appended newline would cross the limit.

## Evidence

| Check | Command | Exit | Result |
|---|---|---:|---|
| Initial regression build | `cmake --build build --target CollabTransportTest` | 0 | Built the test before the production fix. |
| Initial RED | `ctest -R '^CollabTransportTest$' --output-on-failure` (from `build/tests`) | 8 | `sendLineCountsItsFramingNewline` failed: `closed.size()` was 0. |
| Fixed build | `cmake --build build --target CollabTransportTest` | 0 | Rebuilt the affected test target. |
| Initial GREEN | `ctest -R '^CollabTransportTest$' --output-on-failure` (from `build/tests`) | 0 | 1/1 CTest tests passed. |
| Remove-fix proof | `git diff -- src/core/CollabLink.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-9.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-9.r1/logs/fix.patch` | 0 | Removed only the product fix. |
| Proof RED build | `cmake --build build --target zene CollabTransportTest` | 0 | Rebuilt `zene` and the affected test with the fix removed. |
| Proof RED | `ctest -R '^CollabTransportTest$' --output-on-failure` (from `build/tests`) | 8 | The same `closed.size()` assertion failed. |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-9.r1/logs/fix.patch` | 0 | Restored the product fix. |
| Proof GREEN build | `cmake --build build --target zene CollabTransportTest` | 0 | Rebuilt `zene` and the affected test with the fix restored. |
| Proof GREEN | `ctest -R '^CollabTransportTest$' --output-on-failure` (from `build/tests`) | 0 | 1/1 CTest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

Build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-9.r1/logs` (`build-*.log`, `test-*.log`, and `gate-*.log`). CTest ran one affected-family test; no zero-test result occurred.

The requested `fff` grep tool was not available in this session (`ALL_TOOLS` exposed no fff tool and `command -v fff` returned 1), so I searched the numbered logs with `rg -n` for build errors, test failures, the regression case, and the CTest summary.

## Files committed

- `src/core/CollabLink.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-9.md`
