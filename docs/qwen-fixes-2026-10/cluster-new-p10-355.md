# Cluster new-p10-355: long sample frame counts

Verdict: **FIXED**

`Sample::sampleSize()` is `size_t`, while `SampleSelectionView` and its edit fields address frames with `int`. The view now accepts only frame counts through `INT_MAX`; larger clips have no editable range or thumbnail, and the dialog reports that limitation in its status and accessible description.

## Evidence

- [include/SampleEditorDialog.h:62](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-355/include/SampleEditorDialog.h:62) checks the size before narrowing.
- [src/gui/SampleSelectionView.cpp:53](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-355/src/gui/SampleSelectionView.cpp:53) uses that checked conversion; [line 65](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-355/src/gui/SampleSelectionView.cpp:65) suppresses thumbnail construction for unsupported sizes.
- [src/gui/SampleEditorDialog.cpp:81](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-355/src/gui/SampleEditorDialog.cpp:81) reports the refusal; [line 241](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-355/src/gui/SampleEditorDialog.cpp:241) provides an accessible description.
- [tests/src/gui/SampleEditorDialogTest.cpp:171](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-355/tests/src/gui/SampleEditorDialogTest.cpp:171) checks `INT_MAX` and `INT_MAX + 1` without allocating a multi-gigabyte sample.

## Validation evidence

The affected CTest family was `SampleEditorDialogTest`: 1 CTest target, 9 QtTest cases. RED and the 4b reverse proof each reported 8 passed and the new boundary assertion failed. The green runs passed the target.

Commands below ran from the worktree root unless noted. The CTest commands ran from `build/tests`. Build and test output is in the listed new log files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs`.

| Check | Exact command | Exit |
|---|---|---:|
| RED build | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/build-1.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"; exit "$build_status"` | 0 |
| RED test | `ctest -R SampleEditorDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/test-1.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"; exit "$test_status"` | 8 |
| Save reverse-proof patch | `git diff -- include/SampleEditorDialog.h src/gui/SampleSelectionView.cpp src/gui/SampleEditorDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/fix.patch` | 0 |
| Reverse fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/fix.patch` | 0 |
| 4b reverse build | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/build-3.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"; exit "$build_status"` | 0 |
| 4b reverse test | `ctest -R SampleEditorDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/test-3.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"; exit "$test_status"` | 8 |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/fix.patch` | 0 |
| Final build | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/build-4.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"; exit "$build_status"` | 0 |
| Final test | `ctest -R SampleEditorDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/test-4.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"; exit "$test_status"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/gate-1.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"; exit "$gate_status"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-355.r1/logs/gate-2.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"; exit "$gate_status"` | 0 |

The first RED build/test pair is the behavior-preserving extraction of the old narrowing conversion into the checked helper, before the helper rejected oversized values. Reversing `fix.patch` restored that same narrowing behavior; the identical test failed again. Reapplying it made the test pass.

## Files in the commit

- `include/SampleEditorDialog.h`
- `src/gui/SampleSelectionView.cpp`
- `src/gui/SampleEditorDialog.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-355.md`
