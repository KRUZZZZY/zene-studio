# Cluster new-p10-224: truncated TinyXML escape

**Verdict: FIXED**

`LocalFileMng::convertFromTinyXMLString()` read the two hex digits and semicolon
before confirming that the complete six-byte escape remained. A malformed
marker also stayed at the same search position, so the importer could loop
forever. The converter now checks the remaining length before any indexed read
and advances past malformed markers. Complete TinyXML escapes still convert
in place; incomplete escapes remain unchanged.

## Evidence

| Evidence | Command / result |
| --- | --- |
| Original-code RED | From `build/tests`, `ctest -R HydrogenImportLocalFileMngTest --output-on-failure` — **EXIT=8**, 1/1 registered test failed on `conversion did not return for an incomplete escape` (`test-1.log`). |
| Reversed-patch RED | Reversed only `plugins/HydrogenImport/LocalFileMng.cpp`, rebuilt, then ran the same ctest command from `build/tests` — **EXIT=8**, same assertion (`test-3.log`). |
| Restored-patch GREEN | From `build/tests`, `ctest -R HydrogenImportLocalFileMngTest --output-on-failure` — **EXIT=0**, 1/1 test passed (`test-4.log`). The test checks `&#x`, `&#xA`, and `&#xAB`, after a valid escape in the same byte array. |
| Affected-family count | **1 ctest test**: `HydrogenImportLocalFileMngTest` (CTest test #291). |
| Complexity gate | `bash tests/complexity-gate.sh --check` — **EXIT=0** (`gate-1.log`). |
| File-length gate | `bash tests/file-length-gate.sh --check` — **EXIT=0** (`gate-2.log`). |

The bounds check and scan progression are at [LocalFileMng.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-224/plugins/HydrogenImport/LocalFileMng.cpp:132). The regression cases are in [HydrogenImportLocalFileMngTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-224/tests/src/plugins/HydrogenImportLocalFileMngTest.cpp:21). Test registration and source wiring are in [tests/CMakeLists.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-224/tests/CMakeLists.txt:860).

## Commands and exit codes

Builds used the configured tree; no configure command was run.

```text
cmake --build build --target zene HydrogenImportLocalFileMngTest -j2 — EXIT=2
cmake --build build --target zene tests/HydrogenImportLocalFileMngTest -j2 — EXIT=2
cmake --build build/tests --target HydrogenImportLocalFileMngTest -j2 — EXIT=0 (original, fixed, reversed, and restored builds)
cmake --build build --target zene hydrogenimport -j2 — EXIT=0 (fixed, reversed, and restored builds)
ctest -R HydrogenImportLocalFileMngTest --output-on-failure — EXIT=8 (original RED; run from build/tests)
git diff -- plugins/HydrogenImport/LocalFileMng.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-224.r1/logs/fix.patch; diff_code=$?; git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-224.r1/logs/fix.patch; reverse_code=$?; echo DIFF_EXIT=$diff_code REVERSE_APPLY_EXIT=$reverse_code; exit 0 — DIFF_EXIT=0, REVERSE_APPLY_EXIT=0
ctest -R HydrogenImportLocalFileMngTest --output-on-failure — EXIT=8 (reversed-patch RED; run from build/tests)
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-224.r1/logs/fix.patch; code=$?; echo APPLY_EXIT=$code; exit 0 — APPLY_EXIT=0
ctest -R HydrogenImportLocalFileMngTest --output-on-failure — EXIT=0 (restored-patch GREEN; run from build/tests)
bash tests/complexity-gate.sh --check — EXIT=0
bash tests/file-length-gate.sh --check — EXIT=0
```

The two root-level test-target attempts returned EXIT=2 because this generated
Make build exposes that test target from `build/tests`; the subdirectory build
command above succeeded. Build and test logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-224.r1/logs`.
The requested `fff grep` tool was not exposed in this session, so log searches
used `rg` with one term per invocation.

## Files committed

- `plugins/HydrogenImport/LocalFileMng.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/src/plugins/HydrogenImportLocalFileMngTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-224.md`
