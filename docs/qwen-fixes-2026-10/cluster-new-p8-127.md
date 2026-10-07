# Cluster new-p8-127: importer exception and journalling

**Verdict: FIXED**

`ImportFilter::tryImportFile()` disabled the project journal and restored it only after normal loop completion. An exception from plugin construction or `tryImport()` therefore left subsequent edits unjournalled. A local scope guard now restores the exact prior state on every exit from the function ([ImportFilter.cpp:98](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-127/src/core/ImportFilter.cpp:98)).

The new regression uses a real test-only importer module that throws from `tryImport()`. `ImportFilterTest::anImporterExceptionRestoresJournalling()` checks the exception and the restored journal state ([ImportFilterTest.cpp:56](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-127/tests/src/core/ImportFilterTest.cpp:56)).

## Evidence

| Check | Command/log | Exit | Result |
|---|---|---:|---|
| RED on original code | `ctest -R '^ImportFilterTest$' --output-on-failure` (`test-1.log`) | 8 | 1/1 failed: `journal->isJournalling()` returned false after the importer exception. |
| GREEN after fix | `ctest -R '^ImportFilterTest$' --output-on-failure` (`test-2.log`) | 0 | 1/1 passed. |
| Fix removed and rebuilt | `ctest -R '^ImportFilterTest$' --output-on-failure` (`test-3.log`) | 8 | Same assertion failed again. |
| Patch reapplied and rebuilt | `ctest -R '^ImportFilterTest$' --output-on-failure` (`test-4.log`) | 0 | 1/1 passed again. |
| Affected-family count | CTest selection `^ImportFilterTest$` | — | 1 test executed; no other tests match this family. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (`gate-3.log`) | 0 | PASS. |
| File-length gate | `bash tests/file-length-gate.sh --check` (`gate-4.log`) | 0 | PASS. |
| Whitespace check | `git diff --check` | 0 | PASS. |

All build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs`. The fff grep tool was not available in this session, so logs were searched with `rg` for the requested error and failure terms.

## Exact build, test, and gate commands

Commands below include their output paths and captured exit codes. Build-1 and build-2 were setup attempts: the first combined-target invocation stopped after building `zene` with `No rule to make target 'ImportFilterTest'`; the next exposed two fixture compile errors, which were corrected before build-3.

```text
cmake --build build --target zene ImportFilterTest throwing_import_filter_test_plugin > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status
EXIT=2
cmake --build build --target ImportFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status
EXIT=2
cmake --build build --target ImportFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
ctest -R '^ImportFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status
EXIT=8
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
cmake --build build --target ImportFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/build-5.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
ctest -R '^ImportFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
git diff -- src/core/ImportFilter.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/fix.patch; status=$?; echo EXIT=$status
EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/build-6.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
cmake --build build --target ImportFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/build-7.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
ctest -R '^ImportFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status
EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/fix.patch; status=$?; echo EXIT=$status
EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/build-8.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
cmake --build build --target ImportFilterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/build-9.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
ctest -R '^ImportFilterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/gate-3.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-127.r1/logs/gate-4.log 2>&1; status=$?; echo EXIT=$status
EXIT=0
```

## Files in the commit

- `src/core/ImportFilter.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `tests/src/core/ImportFilterTest.cpp`
- `tests/src/core/ThrowingImportFilterPlugin.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-127.md`
