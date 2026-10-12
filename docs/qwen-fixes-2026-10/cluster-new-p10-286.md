# Cluster new-p10-286

**Verdict: BLOCKED**

## Finding evidence

The finding is real in `plugins/Sf2Player/PatchesDialog.cpp:354-356, 382`.
`bankChanged()` sets `stop` after adding a unique preset, and the outer loop
condition then prevents visiting any later soundfont. The duplicate check at
line 375 can remain while the loop visits all soundfonts.

## Why the fix is blocked

The configured CTest tree has no `PatchesDialog` test. The search for a
registered test in `build/tests/CTestTestfile.cmake` returned no match, and
`tests/src` contains no `PatchesDialog` or `Sf2Player` test source. The
`tests/CMakeLists.txt` entry at lines 1562-1564 builds the SF2 reference module;
it does not register a test. Adding and building a regression would require
changing the test target configuration, which conflicts with the instruction
not to configure this already-configured build. No production change was made.

The regression RED and GREEN commands were not run. The affected CTest family
has 0 registered tests, so a CTest run would be invalid under the supplied
zero-tests rule. The `fff grep` tool was also unavailable in this session, so
the required log search could not be performed.

## Evidence table

| Check | Command / result |
| --- | --- |
| RED | Not run; no registered test target. Exit code: N/A. |
| GREEN | Not run; no registered test target. Exit code: N/A. |
| Affected-family count | 0 registered tests (`rg` registration search exited 1). |
| CTest discovery | `ctest -N -R '^PatchesDialog$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r1/logs/test-list-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` from `build/tests`; EXIT=0. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r1/logs/gate-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"`; EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r1/logs/gate-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"`; EXIT=0. |
| Registered-test search | `rg -n '^add_test\(PatchesDialog' build/tests/CTestTestfile.cmake`; EXIT=1. |
| Matching test-source search | `rg --files tests/src | rg 'PatchesDialog|Sf2Player'`; EXIT=1. |
| Build | Not run; no production changes were made. |

The first CTest discovery attempt ran from the repository root and returned
EXIT=97; it was discarded. The corrected discovery above ran from `build/tests`.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-286.md` — this blocked-status report.
