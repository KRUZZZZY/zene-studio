# Cluster new-p9-499

**Verdict: FIXED**

`changeFileNameVersion()` used the last dot in the full path as the extension boundary. It now checks whether that dot is inside `QFileInfo(fileName).fileName()` before treating it as an extension. For `/home/stems.take/Idea`, increment now produces `/home/stems.take/Idea-01`.

## File and line evidence

- `src/gui/modals/VersionedSaveDialog.cpp:98-101` calculates the basename start and ignores dots before it.
- `tests/src/gui/VersionedSaveDialogTest.cpp:35-39` checks the dotted-parent, extensionless path.
- `tests/CMakeLists.txt:853-854` registers `VersionedSaveDialogTest`.
- `tests/all-sources.txt:2648` registers the new GUI test source in the whole-tree source inventory.
- `tests/upstream-modifications.txt` records the inherited production change and test registration reason.

## Regression and verification evidence

The ctest family contains **1 test** (`VersionedSaveDialogTest`). ctest was run from `build/tests`.

| Stage | Command | Exit | Result |
|---|---|---:|---|
| RED before fix (`test-1.log`) | `ctest -R VersionedSaveDialogTest --output-on-failure` | 8 | 1/1 failed. Actual: `/home/stems-01.take/Idea`; expected: `/home/stems.take/Idea-01`. |
| GREEN after fix (`test-2.log`) | `ctest -R VersionedSaveDialogTest --output-on-failure` | 0 | 1/1 passed. |
| Inverse patch proof (`test-3.log`) | Same ctest command with the production patch removed | 8 | 1/1 failed again. |
| Restored patch proof (`test-4.log`) | Same ctest command after reapplying the production patch | 0 | 1/1 passed. |
| Final rebuild and test (`test-5.log`) | Same ctest command after the final source edit | 0 | 1/1 passed. |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check` | 0 | PASS; baseline unchanged. |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check` | 0 | PASS; baseline unchanged. |

Build prerequisites passed: `cmake --build build --target VersionedSaveDialogTest` exited 0 in `build-2.log`, `build-4.log`, `build-6.log`, `build-8.log`, and `build-10.log`; `cmake --build build --target zene` exited 0 in `build-3.log`, `build-5.log`, `build-7.log`, and `build-9.log`. The initial combined invocation `cmake --build build --target zene VersionedSaveDialogTest` exited 2 after building `zene` and reporting no Make rule for the test target; the targets were then built separately.

The inverse proof used these commands (both patch commands exited 0):

```sh
git diff -- src/gui/modals/VersionedSaveDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-499.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-499.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-499.r1/logs/fix.patch
```

All build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-499.r1/logs`. The `fff grep` tool was not available in this session, so the prescribed log terms (`error`, `FAILED`, `Assertion`) were searched with `rg` against that log folder.

## Files committed

- `src/gui/modals/VersionedSaveDialog.cpp`
- `tests/src/gui/VersionedSaveDialogTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-499.md`
