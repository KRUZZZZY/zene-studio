# Cluster new-p12-329

Verdict: **FIXED**

`BarModelEditor::sizePolicy()` in `src/gui/widgets/BarModelEditor.cpp:20` only returns a policy; it does not update QWidget's stored policy. The constructor now calls `setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed)` at `src/gui/widgets/BarModelEditor.cpp:16`. The regression at `tests/src/gui/KnobKeyboardTest.cpp:94` reads the policy through `QWidget` and checks both axes.

The configured tree has no registered ctest named `LadspaWidgetFactory`; the affected GUI test family used here is `KnobKeyboardTest` (one registered ctest test, one executed).

## Evidence table

Build and gate commands ran from the worktree root. Ctest commands ran from `build/tests`.

| Phase | Exact command | Exit |
|---|---|---:|
| Original build | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/build-red-1.log 2>&1; echo EXIT=$?` | 0 |
| Original RED test | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Fixed build | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/build-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Fixed GREEN test | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Reverse production patch | `git diff -- src/gui/widgets/BarModelEditor.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reversed build | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/build-proof-red-1.log 2>&1; echo EXIT=$?` | 0 |
| Reversed RED test | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/test-proof-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Restored build | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/build-proof-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Restored GREEN test | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/test-proof-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/gate-complexity-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-329.r1/logs/gate-file-length-1.log 2>&1; echo EXIT=$?` | 0 |

Both RED runs failed on `KnobKeyboardTest::barModelEditorStoresItsFixedVerticalPolicy()` because the stored vertical policy was not `QSizePolicy::Fixed`. The two GREEN runs passed, and ctest reported `100% tests passed` with `0 tests failed out of 1`.

## Files in the commit

- `src/gui/widgets/BarModelEditor.cpp`
- `tests/src/gui/KnobKeyboardTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-329.md`
