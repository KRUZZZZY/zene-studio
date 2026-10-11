# Cluster new-p9-161

Verdict: **FIXED**

`graphModel` now has a two-sample minimum. The constructor clamps shorter initial lengths to two, and `setLength()` ignores requests below two without emitting `lengthChanged()`. This keeps `smooth()`, `normalize()`, convolution, and drawing away from zero or one-sample lengths.

## Evidence

| File | Evidence |
| --- | --- |
| `src/gui/widgets/Graph.cpp:40,473-476,498-501` | Defines the minimum, applies it to construction, and rejects shorter setter requests. |
| `tests/src/gui/SampleEditorDialogTest.cpp:157-181` | Covers `-1`, `0`, and `1` setter requests, the accepted length of `2`, smoothing and normalization at the minimum, and a one-sample constructor argument. |
| `tests/upstream-modifications.txt:1054-1055` | Records reasons for the inherited production and test changes. |

The final regression failed with the product patch reversed: `model.length()` was `-1` after `model.setLength(-1)`, expected `4`. It passed with the patch restored.

## Verification

The exact commands and exit codes are below. Test and build output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-161.r1/logs`.

| Check | Command | Exit |
| --- | --- | ---: |
| Initial RED before the product fix (`test-1.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` (from `build/tests`) | 8 |
| Final RED with `fix.patch` reversed (`test-4.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` (from `build/tests`) | 8 |
| Final GREEN after restoring `fix.patch` (`test-5.log`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` (from `build/tests`) | 0 |
| Affected-family count | 1 CTest target, `SampleEditorDialogTest`; its 7 functional Qt test slots ran | 1/1 |
| Build (`build-2.log` through `build-5.log`) | `cmake --build build --target zene SampleEditorDialogTest` | 0 each |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check` | 0 |

The reverse-patch proof commands also completed successfully: `git diff -- src/gui/widgets/Graph.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-161.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-161.r1/logs/fix.patch` (exit 0), then `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-161.r1/logs/fix.patch` (exit 0).

## Files committed

- `src/gui/widgets/Graph.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-161.md`
