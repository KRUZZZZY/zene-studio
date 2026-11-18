# Cluster new-p12-101

Verdict: **FIXED**

## Finding and change

The finding was real. Before the fix, `Graph::dropEvent` decoded the `samplefile` key, left loading commented out, then accepted the drop. `Graph::dragEnterEvent` also advertised the file type as supported. The current handlers explicitly ignore both events until sample-file loading exists.

Evidence: the commented loader is at `src/gui/widgets/Graph.cpp:73`; the corrected handlers are at `src/gui/widgets/Graph.cpp:414` and `:419`. The regression is `tests/src/gui/SampleEditorDialogTest.cpp:248`. The inherited-source reason is registered at `tests/upstream-modifications.txt:1054`.

The regression supplies the string-pair `samplefile` MIME payload and directly exercises both protected handlers through a test subclass. It checks the drop is rejected and that drag-enter is not advertised as supported.

## Verification evidence

The affected CTest family is `SampleEditorDialogTest`: **1 CTest test executed (1/1)**. The RED run reports 8 QtTest cases passed and the new drop assertion failed; GREEN reports the CTest target passed.

| Stage | Exact command and working directory | Exit |
| --- | --- | ---: |
| Initial harness build (cwd: worktree) | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/build-1.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| Initial harness attempt (cwd: `build/tests`; discarded because it failed on synthetic drag-enter before proving drop handling) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/test-1.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 8 |
| Corrected regression build before production change (cwd: worktree) | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/build-2.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| RED on original handler (cwd: `build/tests`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/test-2.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 8 |
| GREEN after fix (cwd: worktree build) | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/build-3.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| GREEN after fix (cwd: `build/tests`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/test-3.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 0 |
| Create and reverse only the product patch (cwd: worktree) | `git diff -- src/gui/widgets/Graph.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/fix.patch; patch_exit=$?; echo EXIT=$patch_exit` | 0 |
| Reversal build (cwd: worktree) | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/build-4.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| RED with product patch reversed (cwd: `build/tests`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/test-4.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 8 |
| Restore product patch (cwd: worktree) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/fix.patch; patch_exit=$?; echo EXIT=$patch_exit` | 0 |
| Final build (cwd: worktree) | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/build-5.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| Final GREEN after restoring patch (cwd: `build/tests`) | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/test-5.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 0 |
| Complexity ratchet (cwd: worktree) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/gate-1.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit` | 0 |
| File-length ratchet (cwd: worktree) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-101.r1/logs/gate-2.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit` | 0 |

Both ratchet gates passed. The reversal proof failed at `!drop.isAccepted()` with the fix removed and passed after restoring the same patch.

`fff` was not available as a tool or shell command in this session; build and test logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files in the commit

- `src/gui/widgets/Graph.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-101.md`
