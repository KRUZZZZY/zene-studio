# Cluster new-p9-409 — FIXED

## Evidence

The finding is real. In `src/gui/SampleThumbnail.cpp:123`, a failed cache search selects the original-buffer fallback. The selected source width is computed at line 135, but the bounds at lines 150–151 previously read `finerThumbnail->width()` even when that iterator was `rend()`. Both clamps now use `finerThumbnailWidth`, which selects the original buffer size for that fallback.

The regression in `tests/src/gui/SampleEditorDialogTest.cpp:137` renders a 100-frame sample into 21 pixels. The finest cached thumbnail is 20 pixels wide, so rendering takes the original-buffer fallback. It checks the expected waveform pixel and pins the clamp bounds to the selected source width. The source check covers release builds where the invalid iterator read can appear to work.

## Validation

| Proof | Exact command | Exit | Evidence |
|---|---|---:|---|
| Save and reverse product patch (`cwd`: worktree) | `git diff -- src/gui/SampleThumbnail.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/fix.patch; echo EXIT=$?` | 0 | Required inverse-patch proof; the following RED run used the reverted source |
| RED build (`cwd`: worktree) | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/build-11.log 2>&1; echo EXIT=$?` | 0 | Rebuilt product with fix temporarily removed |
| RED test build (`cwd`: worktree) | `cmake --build build --target SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/build-12.log 2>&1; echo EXIT=$?` | 0 | Fresh regression binary |
| RED (`cwd`: `build/tests`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/test-8.log 2>&1; echo EXIT=$?` | 8 | Regression failed on the unpatched implementation; QtTest totals: 8 passed, 1 failed |
| Reapply product patch (`cwd`: worktree) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored fix after RED |
| GREEN build (`cwd`: worktree) | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/build-13.log 2>&1; echo EXIT=$?` | 0 | Rebuilt product with fix restored |
| GREEN test build (`cwd`: worktree) | `cmake --build build --target SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/build-14.log 2>&1; echo EXIT=$?` | 0 | Fresh regression binary |
| GREEN (`cwd`: `build/tests`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/test-9.log 2>&1; echo EXIT=$?` | 0 | 1/1 affected CTest targets passed |
| Complexity gate (`cwd`: worktree) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/complexity-gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed |
| File-length gate (`cwd`: worktree) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-409.r1/logs/file-length-gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed |

The affected family contains one registered CTest target, `SampleEditorDialogTest`; its QtTest suite has seven test methods, plus setup and cleanup cases. The `fff grep` tool was not available in this session, so I searched the saved logs with `rg` for the required failure and summary terms.

## Files committed

- `src/gui/SampleThumbnail.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-409.md`
