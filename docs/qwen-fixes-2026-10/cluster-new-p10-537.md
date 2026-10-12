# Cluster new-p10-537

Verdict: **FIXED**

`src/core/BrowserTagStore.cpp:251-265` now trims the input before insertion and
refuses an empty normalized tag with an error. This keeps accepted tags stable
across save and reload, matching `load()`'s existing normalization.

`tests/src/core/BrowserCatalogTest.cpp:170-192` covers whitespace-only input,
the refusal reason, and a padded non-empty tag round trip. The regression failed
against the original implementation and failed again with the production patch
reversed. The browser test and store source are already registered in
`tests/fork-sources.txt`; no inherited source file was changed.

## Evidence

| Check | Command | Exit | Result |
| --- | --- | ---: | --- |
| Original test target build | `cmake --build build --target BrowserCatalogTest` | 0 | `build-1.log` |
| RED on original code | `ctest -R BrowserCatalogTest --output-on-failure` from `build/tests` | 8 | `test-1.log`; `tagInputIsTrimmedAndWhitespaceOnlyTagsAreRefused` reports that whitespace-only input was accepted |
| GREEN build | `cmake --build build --target zene BrowserCatalogTest` | 0 | `build-2.log` |
| GREEN after fix | `ctest -R BrowserCatalogTest --output-on-failure` from `build/tests` | 0 | `test-2.log`; ctest ran 1/1 `BrowserCatalogTest` |
| Reverse production patch | `git diff -- src/core/BrowserTagStore.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-537.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-537.r1/logs/fix.patch` | 0 | Patch removed for the proof run |
| Reversed-code build | `cmake --build build --target zene BrowserCatalogTest` | 0 | `build-3.log` |
| RED with patch reversed | `ctest -R BrowserCatalogTest --output-on-failure` from `build/tests` | 8 | `test-3.log`; the same whitespace-only assertion failed |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-537.r1/logs/fix.patch` | 0 | Patch restored |
| Restored-code build | `cmake --build build --target zene BrowserCatalogTest` | 0 | `build-4.log` |
| GREEN after restore | `ctest -R BrowserCatalogTest --output-on-failure` from `build/tests` | 0 | `test-4.log`; ctest ran 1/1 `BrowserCatalogTest` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

All build, ctest, and gate output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-537.r1/logs`.

Files committed for this cluster:

- `src/core/BrowserTagStore.cpp`
- `tests/src/core/BrowserCatalogTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-537.md`
