# Cluster new-p10-253: Ctrl+right-click selects another mixer strip

**Verdict: FIXED**

`MixerChannelView::mousePressEvent` previously changed the active strip for every mouse button. It now changes the active strip only for a left-button press. The regression sends Ctrl+right-button press to another strip and confirms the current strip remains selected.

## Evidence

| File | Evidence |
|---|---|
| `src/gui/MixerChannelView.cpp:370-373` | The handler returns for any button other than `Qt::LeftButton`. |
| `tests/src/gui/MixerModesTest.cpp:197-205` | The regression sets the sender as current, sends Ctrl+right-click to the return strip, and checks that the sender remains current. |
| `tests/upstream-modifications.txt:737` | Records the inherited production change and its regression coverage. |

The initial RED run failed at the regression assertion: actual strip index 2, expected index 1. The final verbose GREEN run reports 14 passed QtTest slots; CTest reports the affected family as 1/1 passed.

## QA evidence table

| Evidence | Exact command or committed files | Result |
|---|---|---|
| Initial RED | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | EXIT=8; 13 slots passed and the new assertion failed. |
| Fix sensitivity RED | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | EXIT=8 after removing the production patch and rebuilding `zene` plus `MixerModesTest`. |
| Restored GREEN | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc` | EXIT=0 after restoring the patch and rebuilding both targets. |
| Affected-family count | `cd build/tests && ctest -R '^MixerModesTest$' --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc` | EXIT=0; 1 CTest test and 14 QtTest slots passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | EXIT=0. |
| Files committed in `0327bf8cd` | `docs/qwen-fixes-2026-10/cluster-new-p10-253.md`; `src/gui/MixerChannelView.cpp`; `tests/src/gui/MixerModesTest.cpp`; `tests/upstream-modifications.txt` | Four explicit paths. |

## Commands and exit codes

The log folder is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs`.

Initial RED before the production edit:

```sh
cmake --build build --target MixerModesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=8 (13 passed, 1 failed; ctrlRightClickDoesNotSelectAnotherStrip)
```

Initial GREEN after the fix:

```sh
cmake --build build --target MixerModesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0 (1/1 CTest test passed)
```

Fix sensitivity proof using the saved production patch:

```sh
git diff -- src/gui/MixerChannelView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/fix.patch; rc=$?; echo EXIT=$rc
# EXIT=0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/fix.patch; rc=$?; echo EXIT=$rc
# EXIT=0
cmake --build build --target zene MixerModesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=8 (same regression assertion fails)
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/fix.patch; rc=$?; echo EXIT=$rc
# EXIT=0
cmake --build build --target zene MixerModesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
cd build/tests && ctest -R '^MixerModesTest$' --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0 (14 passed, 0 failed)
```

Ratchet gates:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-253.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc
# EXIT=0
```

The requested `fff grep` tool was not available in this environment (`command -v fff` returned no path). Build and test logs were searched with `rg` for `error`, `FAILED`, and `Assertion`; the three `FAILED` matches are the intentional RED runs above.

## Files committed

- `src/gui/MixerChannelView.cpp`
- `tests/src/gui/MixerModesTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-253.md`
