# Cluster new-p12-130: parentless MIDI setup widget

**Verdict: FIXED**

`MidiSetupWidget::show()` at `src/gui/MidiSetupWidget.cpp:68` now checks
`parentWidget()` before changing the parent's visibility. It still sets its own
visibility from the configuration section. The regression at
`tests/src/gui/MidiControllerMenuTest.cpp:81` creates the widget through
`MidiSetupWidget::create<TestMidiClient>(nullptr)`, calls `show()`, and checks
that the configurable widget becomes visible.

## Evidence

| Check | Exact command | Exit / result |
|---|---|---|
| RED before fix | `ctest -R MidiControllerMenuTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8`; 1/1 CTest case segfaulted |
| GREEN after fix | `ctest -R MidiControllerMenuTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; 1/1 passed |
| Reverse-patch proof | `ctest -R MidiControllerMenuTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8`; 1/1 segfaulted again without the production guard |
| Restored-patch proof | `ctest -R MidiControllerMenuTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; 1/1 passed after reapplying the guard |
| Affected family count | Same exact CTest commands above | 1 CTest test executed; zero-run condition was not hit |
| Complexity gate | `bash tests/complexity-gate.sh --check` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check` | `EXIT=0` |

## Build, regression, and gate commands

Every build and test command below wrote its output to a new numbered log in the
run log folder. The first dedicated-target attempt found that the configured
build had no `MidiSetupWidgetTest` target (`No rule to make target`, exit 2), so
the new case was added to the already registered `MidiControllerMenuTest`
executable without reconfiguring.

| Purpose | Exact command | Exit |
|---|---|---|
| Initial dedicated-target attempt | `cmake --build build --target MidiSetupWidgetTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 2 |
| Build regression on original code | `cmake --build build --target MidiControllerMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Build after fix | `cmake --build build --target MidiControllerMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Save and reverse production patch | `git diff -- src/gui/MidiSetupWidget.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild without guard | `cmake --build build --target MidiControllerMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild after restoring guard | `cmake --build build --target MidiControllerMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild main application | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-130.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Files in this fix commit

- `src/gui/MidiSetupWidget.cpp`
- `tests/src/gui/MidiControllerMenuTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-130.md`
