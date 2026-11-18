# Cluster new-p9-573

Verdict: FIXED

`f_cnt_t` is unsigned. In `plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp:219-223`, `paintEvent()` now checks that the playback frame is between `m_from` and `m_to` before subtracting the range start. The subtraction casts the already-validated nonnegative start to `f_cnt_t`, and playback painting is skipped when the frame is outside the visible range (`:259`). This prevents a frame before a zoomed range from wrapping into an invalid pixel coordinate.

## Regression and test constraint

Neither suggested AudioFileProcessor ctest name is registered in this configured tree: `ctest -N -R '^AudioFileProcessorWaveView$'` and `ctest -N -R '^AudioFileProcessorView$'` each reported `Total Tests: 0`. `SampleEditorDialogTest` is an existing Qt test target. A direct widget harness could not link because that target does not link the separately built AudioFileProcessor module (`build-2.log`, exit 2), and reconfiguration was explicitly disallowed. The regression in `tests/src/gui/SampleEditorDialogTest.cpp:157-173` therefore checks the production paint code's lower and upper range checks before the unsigned offset. It fails against the original code and again when the fix is temporarily reverted.

## Evidence

| Stage | Command | Exit | Result |
|---|---|---:|---|
| Test discovery | `ctest -N -R '^AudioFileProcessorWaveView$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/test-1.log 2>&1` | 0 | No matching ctest registered (`Total Tests: 0`). |
| Original-code RED | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/test-2.log 2>&1` | 8 | `playbackCursorChecksVisibleRangeBeforeUnsignedSubtraction` failed at `rangeStartCheck >= paintEvent`; 8 other Qt test cases passed. |
| Inverse-patch command | `git diff -- plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/fix.patch` | 0 | Temporarily restored the original paint code. |
| Rebuilt original-code RED | `cmake --build build --target zene audiofileprocessor SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/build-5.log 2>&1` | 0 | Rebuilt the application, plugin, and test target before the inverse check. |
| Reverted-code RED | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/test-5.log 2>&1` | 8 | The same assertion failed; 8 other Qt test cases passed. |
| Reapply command | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/fix.patch` | 0 | Restored the fix. |
| Rebuilt fixed code | `cmake --build build --target zene audiofileprocessor SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/build-7.log 2>&1` | 0 | Rebuilt all affected targets after the final test edit. |
| GREEN | `ctest -V -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/test-8.log 2>&1` | 0 | 1/1 ctest test passed; QtTest reported 9 passed, 0 failed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/gate-1.log 2>&1` | 0 | PASS. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-573.r1/logs/gate-2.log 2>&1` | 0 | PASS. |

Affected-family count: 1 ctest test (`SampleEditorDialogTest`), containing 9 QtTest cases.

Files committed:

- `plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-573.md`
