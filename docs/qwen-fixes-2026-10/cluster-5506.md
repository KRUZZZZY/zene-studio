# Cluster 5506: text fixture short writes

Verdict: **FIXED**

## Finding and fix

The finding is real. `tests/src/core/BrowserTestSupport.h:176-183` implemented
`writeText`; line 180 previously treated every nonnegative result as success.
The helper now caches `text.toUtf8()` and reports success only when the write
count equals the encoded byte count (`BrowserTestSupport.h:180-181`).

The regression is `tests/src/core/BrowserCatalogTest.cpp:109-146`. On Unix it
temporarily limits the fixture process to one byte per file, ignores
`SIGXFSZ`, writes a 1 MiB string, confirms the file is exactly one byte, and
requires `writeText` to return false. The resource limit and signal handler are
restored before assertions.

The files `tests/src/core/BrowserCatalogTest.cpp` and
`tests/src/core/BrowserTestSupport.h` were already registered in
`tests/fork-sources.txt` (lines 1912-1913); no source registry change was
needed.

## Evidence

All commands below redirect build/test/gate output to new files under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs` and print
the unpiped command status.

| Evidence | Exact command and working directory | Result |
| --- | --- | --- |
| RED, original helper and 1 MiB regression | `ctest -R '^BrowserCatalogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/test-3.log 2>&1; test_rc=$?; echo EXIT=$test_rc` (cwd: `build/tests`) | `EXIT=8`; `textFixtureRejectsShortWrite` failed because the helper returned true after the one-byte write. |
| Remove-fix proof | `git diff -- tests/src/core/BrowserTestSupport.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/fix.patch; apply_rc=$?; echo EXIT=$apply_rc` (cwd: worktree), then `ctest -R '^BrowserCatalogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/test-5.log 2>&1; test_rc=$?; echo EXIT=$test_rc` (cwd: `build/tests`) | Patch reversal `EXIT=0`; regression failed again, `EXIT=8`. |
| Restore-fix proof | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/fix.patch; apply_rc=$?; echo EXIT=$apply_rc` (cwd: worktree), then `ctest -R '^(BrowserCatalogTest|AafImportTest|ControlBrowserCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/test-6.log 2>&1; test_rc=$?; echo EXIT=$test_rc` (cwd: `build/tests`) | Patch application `EXIT=0`; `EXIT=0`, 3/3 CTest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/gate-1.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc` (cwd: worktree) | `EXIT=0` |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/gate-2.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc` (cwd: worktree) | `EXIT=0` |

Affected-family count: **3 CTest tests**, all passed in the final run.

### Build and test command record

Each build used the worktree root as its working directory; each CTest command
used `build/tests`.

| Log | Command | Exit |
| --- | --- | --- |
| `build-1.log` | `cmake --build build --target BrowserCatalogTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/build-1.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `test-1.log` | `ctest -R '^BrowserCatalogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/test-1.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 |
| `build-2.log` | `cmake --build build --target BrowserCatalogTest AafImportTest ControlBrowserCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/build-2.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `test-2.log` | `ctest -R '^(BrowserCatalogTest|AafImportTest|ControlBrowserCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/test-2.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 |
| `build-3.log` | `cmake --build build --target BrowserCatalogTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/build-3.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `test-3.log` | `ctest -R '^BrowserCatalogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/test-3.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 |
| `build-4.log` | `cmake --build build --target BrowserCatalogTest AafImportTest ControlBrowserCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/build-4.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `test-4.log` | `ctest -R '^(BrowserCatalogTest|AafImportTest|ControlBrowserCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/test-4.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 0 |
| `build-5.log` | `cmake --build build --target BrowserCatalogTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/build-5.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `test-5.log` | `ctest -R '^BrowserCatalogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/test-5.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 |
| `build-6.log` | `cmake --build build --target BrowserCatalogTest AafImportTest ControlBrowserCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/build-6.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| `test-6.log` | `ctest -R '^(BrowserCatalogTest|AafImportTest|ControlBrowserCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5506.r1/logs/test-6.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 0 |

The first regression used a 32-byte payload. `test-1.log` was RED on the
original helper, but `test-2.log` remained RED with the first count comparison:
Qt buffered that small write and returned the full count before the failed
flush. The test was changed to a 1 MiB payload, which exercises a short return
from `write()` itself; the same revised test then went RED before the fix,
GREEN after it, RED with the fix removed, and GREEN after restoration.

The requested `fff grep` command/tool was not available in this session. Build,
test, and gate logs were searched with `rg` for errors and failure/assertion
markers; the logs remain in the path above.

## Files committed

- `tests/src/core/BrowserCatalogTest.cpp`
- `tests/src/core/BrowserTestSupport.h`
- `docs/qwen-fixes-2026-10/cluster-5506.md`
