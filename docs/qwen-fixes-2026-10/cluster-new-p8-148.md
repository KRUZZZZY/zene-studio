# Cluster new-p8-148: fresh unattended audio diagnostic

**Verdict: FIXED**

`MainWindow::finalize()` in `src/gui/MainWindow.cpp` used an `else if` at line 574 for the audio startup failure check. A fresh unattended config first entered the setup branch at line 555, logged the setup skip at line 566, then bypassed the audio diagnostic. The check is now an independent `if` at line 574.

The regression in `tests/control-headless-no-audio-device.py:164-184` runs both failure conditions with `configured=1` and `configured=0`. Fresh launches must show the setup-skip message and the audio diagnostic. The affected CTest family is `ControlHeadlessNoAudioDevice`: **1 CTest test**, covering four real process launches.

## Evidence

| Check | Result |
| --- | --- |
| Canonical pre-fix regression | RED, EXIT=8; configured SDL case passed, fresh SDL case failed because `audio-device-setup` was absent (`test-2.log`). |
| Fixed regression | GREEN, EXIT=0; 1/1 CTest test passed (`test-3.log`). |
| Reversal proof | Removing only `src/gui/MainWindow.cpp` made the test RED again (EXIT=8, `test-4.log`); restoring the same patch made it GREEN (EXIT=0, `test-5.log`). |
| Complexity gate | EXIT=0. |
| File-length gate | EXIT=0. |

All logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs`. `fff` grep was not available in this session; log searches used `rg`.

## Commands and exit codes

Builds ran from the worktree root; CTest ran from `build/tests`.

```sh
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0 (pre-fix build)

ctest -R '^ControlHeadlessNoAudioDevice$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=8 (initial RED, before adding the explicit setup-skip assertion)

ctest -R '^ControlHeadlessNoAudioDevice$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=8 (canonical RED)

cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0 (fixed build)

ctest -R '^ControlHeadlessNoAudioDevice$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0 (GREEN, 1/1)

git diff -- src/gui/MainWindow.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result
# EXIT=0 (reverse production patch)

cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0 (reversed build)

ctest -R '^ControlHeadlessNoAudioDevice$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=8 (reversal RED, 1/1)

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result
# EXIT=0 (restore production patch)

cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0 (restored build)

ctest -R '^ControlHeadlessNoAudioDevice$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0 (restored GREEN, 1/1)

bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/gate-complexity-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0

bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-148.r1/logs/gate-file-length-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0
```

## Files in the commit

- `src/gui/MainWindow.cpp`
- `tests/control-headless-no-audio-device.py`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-148.md`
