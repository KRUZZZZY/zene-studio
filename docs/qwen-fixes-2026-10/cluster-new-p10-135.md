# Cluster new-p10-135

**Verdict: FIXED**

`FileDialog::openAudioFile()` returns one `QString` (`src/gui/modals/FileDialog.cpp:187`) and its callers use one path. The chooser was configured with `QFileDialog::ExistingFiles` at line 154, allowing several selections while silently returning only the first. It now uses `QFileDialog::ExistingFile` at line 154. The regression at `tests/src/gui/SampleEditorDialogTest.cpp:239` opens the real dialog and asserts its mode. `tests/upstream-modifications.txt:750` records the inherited source change.

## Evidence

All CTest commands ran from `build/tests`. Their output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-135.r1/logs/`.

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Regression before fix (`test-1.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` | 8 | Red: expected `ExistingFile`, observed `ExistingFiles`; 8 passed, 1 failed. |
| Regression after fix (`test-2.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` | 0 | Green: CTest passed 1/1. |
| Reverse-patch build (`build-4.log`) | `cmake --build build --target zene SampleEditorDialogTest` | 0 | Rebuilt both targets with the production fix removed. |
| Reverse-patch regression (`test-3.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` | 8 | Red again: observed `ExistingFiles`; 8 passed, 1 failed. |
| Restored build (`build-5.log`) | `cmake --build build --target zene SampleEditorDialogTest` | 0 | Rebuilt both targets after restoring the fix. |
| Restored regression (`test-4.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` | 0 | Green: CTest passed 1/1. |
| Affected-family count (`test-5.log`) | `ctest -R '^SampleEditorDialogTest$' --verbose --output-on-failure` | 0 | 1 CTest target; 9 Qt cases passed. |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

The reverse-patch commands both exited 0:

```sh
git diff -- src/gui/modals/FileDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-135.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-135.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-135.r1/logs/fix.patch
```

The test-target build was first attempted as `cmake --build build --target SampleEditorDialogTest` and exited 2 (`build-1.log`) because the watchdog called `reject()` on a `QWidget`; changing it to `close()` fixed the test code. The same command then exited 0 (`build-2.log`). The post-fix build `cmake --build build --target zene SampleEditorDialogTest` exited 0 (`build-3.log`); the reverse and restored builds are recorded above.

The requested `fff` grep tool was not available in the enabled tools or shell (`command -v fff` exited 1), so each log was searched with `rg` for `error`, `FAILED`, and `Assertion`, plus its exit/result lines.

## Files committed

- `src/gui/modals/FileDialog.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-135.md`
