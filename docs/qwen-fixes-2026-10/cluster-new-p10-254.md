# Cluster new-p10-254

Verdict: **FIXED**

## Evidence

`MixerChannelLcdSpinBox::contextMenuEvent()` now only asks `TrackView` for the mixer assignment submenu when `m_tv` is non-null ([MixerChannelLcdSpinBox.cpp:70](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-254/src/gui/widgets/MixerChannelLcdSpinBox.cpp:70)). The regression test creates the spin box with the default null view, sends a `QContextMenuEvent`, and verifies that its popup opens and closes ([MixerModesTest.cpp:120](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-254/tests/src/gui/MixerModesTest.cpp:120)). The inherited-file reason is recorded at [upstream-modifications.txt:821](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-254/tests/upstream-modifications.txt:821).

The affected ctest family is `MixerModesTest`: **1 registered ctest executed per run**. The RED runs each failed in `aMixerChannelContextMenuWorksWithoutTrackView()` with SIGSEGV at address zero; the GREEN runs each passed 1/1.

| Run | Exact command | Exit / observed result |
|---|---|---|
| Initial RED build | `cmake --build build --target MixerModesTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Initial RED ctest | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8; 1/1 test failed with SIGSEGV in the regression method |
| GREEN build | `cmake --build build --target MixerModesTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN ctest | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0; 1/1 passed |
| Save and reverse product patch | `git diff -- src/gui/widgets/MixerChannelLcdSpinBox.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reverse-proof rebuild | `cmake --build build --target MixerModesTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Reverse-proof ctest | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8; 1/1 test failed with SIGSEGV in the same regression method |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Final build | `cmake --build build --target zene MixerModesTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Final ctest | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-254.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The `fff grep` helper was not exposed in this environment. Build, test, and gate logs were searched with targeted `rg` queries; no build errors or gate failure markers were found.

## Files included in the commit

- `src/gui/widgets/MixerChannelLcdSpinBox.cpp`
- `tests/src/gui/MixerModesTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-254.md`
