# Cluster new-p12-330: BarModelEditor color setters

**Verdict: FIXED**

`BarModelEditor::setBackgroundBrush`, `setBarBrush`, and `setTextColor` in
`src/gui/widgets/BarModelEditor.cpp:40-66` changed their stored values without
requesting a paint. Each setter now calls `update()` after changing its value.
The regression in `tests/src/gui/KnobKeyboardTest.cpp:111-129` counts paint
events and verifies that each setter schedules another paint. The production
change is registered in `tests/upstream-modifications.txt:1054`.

The suggested `LadspaWidgetFactory` target is not registered in this build's
CTest list. `KnobKeyboardTest` is the existing GUI test target used instead.

## Evidence

| Check | Command (run from) | Exit | Result |
| --- | --- | ---: | --- |
| RED on original code | `ctest -R '^KnobKeyboardTest$' --output-on-failure` (`build/tests`) | 8 | 1/1 CTest target ran; the new assertion failed after changing the background brush. QtTest reported 15 passed, 1 failed. |
| RED after inverse patch | `ctest -R '^KnobKeyboardTest$' --output-on-failure` (`build/tests`) | 8 | Rebuilt with the production fix reversed; the same assertion failed (15 passed, 1 failed). |
| GREEN with fix applied | `ctest -R '^KnobKeyboardTest$' --output-on-failure` (`build/tests`) | 0 | 1/1 CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

Each build command below rebuilt both `zene` and `KnobKeyboardTest`; all three
exited 0. Test and build output is saved under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/`.

## Commands and exit codes

```text
mkdir -p /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs && cd build/tests && ctest -N > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/ctest-list-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=0 (402 tests listed; no LadspaWidgetFactory target)

cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=0
ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=8

git diff -- src/gui/widgets/BarModelEditor.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=0
cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=0
ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=8

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=0
cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=0
ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=0

bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-330.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
EXIT=0
```

The `fff` log-search tool was not available in this session; saved logs were
searched with `rg` for build errors, test failures, and QtTest totals.

## Files committed

- `src/gui/widgets/BarModelEditor.cpp`
- `tests/src/gui/KnobKeyboardTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-330.md`
