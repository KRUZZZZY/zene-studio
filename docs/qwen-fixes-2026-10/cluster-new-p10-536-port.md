# Cluster new-p10-536-port

Verdict: **BLOCKED**

The probe-limit bug is real. `probeAndAppend()` in `src/core/BrowserCatalog.cpp:190-201` now sets `context->truncated` at line 199 when probing is enabled and another candidate reaches the exhausted probe budget. The regression in `tests/src/core/BrowserCatalogQueryTest.cpp:353-374` builds 201 audio candidates whose names do not match but whose embedded titles do. It asserts 201 scanned, 200 probed, 200 matched, and a truncated result.

The test first failed on the original implementation, passed with the fix, and failed again after removing only the production patch. The exact patch was reapplied. A final post-reapply build was stopped after about fifteen minutes behind the shared build guard (`all 2 build slots busy, waiting...`), before compilation began; therefore no final post-reapply test result is claimed. The two required gates also fail on unrelated existing branch entries; neither names a changed file. Those failures are recorded below without changing the gates or unrelated modules.

## Evidence

| Check | Command | Exit | Result |
|---|---|---:|---|
| RED | `cmake --build build --target BrowserCatalogQueryTest` | 0 | `build-1.log` |
| RED | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` (from `build/tests`) | 8 | `test-1.log`; `result.truncated` was false; 1 CTest test ran |
| GREEN | `cmake --build build --target BrowserCatalogQueryTest` | 0 | `build-2.log` |
| GREEN | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` (from `build/tests`) | 0 | `test-2.log`; 1 CTest test ran |
| Fix removal | `git diff -- src/core/BrowserCatalog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-536-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-536-port.r1/logs/fix.patch` | 0 | Production change only was reversed |
| Removal proof | `cmake --build build --target BrowserCatalogQueryTest` | 0 | `build-3.log` |
| Removal proof | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` (from `build/tests`) | 8 | `test-3.log`; same truncation assertion failed; 1 CTest test ran |
| Restore | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-536-port.r1/logs/fix.patch` | 0 | Exact production patch reapplied |
| Final build attempt | `cmake --build build --target zene BrowserCatalogTest BrowserCatalogQueryTest` | 130 | `build-4.log`; interrupted after the guard kept both slots busy; compilation did not start |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | `complexity-gate-2.log`; six unrelated new functions exceed CCN 10 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | `file-length-gate-2.log`; fourteen unrelated new files exceed 500 code lines |

The affected focused ctest count was **1**. The `fff grep` tool was not available in this session, so build and test logs were searched with `rg` for the required error, failure, and assertion terms.

## Files in this commit

- `src/core/BrowserCatalog.cpp`
- `tests/src/core/BrowserCatalogQueryTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-536-port.md`
