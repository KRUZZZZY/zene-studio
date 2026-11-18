# cluster new-p8-84

**Verdict: FIXED**

`control.set_undo_depth` now records a cap change as reversible only when it dropped no undo steps. If lowering either cap evicts history, the transaction is marked irreversible and `control.undo` refuses it, since the captured inverse only restores the caps.

## Evidence

- [ControlReversibilityTableSnapshot.cpp:146](../../src/core/ControlReversibilityTableSnapshot.cpp:146) declares the command conservatively as a snapshot that is not unconditionally reversible.
- [ControlCommandsUndo.cpp:178](../../src/core/ControlCommandsUndo.cpp:178) measures evictions; line 180 sets per-call reversibility from that count.
- [UndoBoundsTest.cpp:213](../../tests/src/core/UndoBoundsTest.cpp:213) lowers the cap after creating history and asserts that the transaction is irreversible and undo is refused. Line 239 checks the static contract entry.
- Before the fix, `test-1.log` failed with the recorded value `reversible: 1` where the regression expected `false`; the contract row also reported `1`. Reversing the product patch reproduced the failure in `test-3.log`.

## Verification

Commands below include the log redirection as run. CTest commands ran from `build/tests`; build and gate commands ran from the worktree root.

| Purpose | Exact command | Exit |
|---|---|---:|
| Build regression before fix | `cmake --build build --target UndoBoundsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED on original code | `ctest -R '^UndoBoundsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Build initial fix | `cmake --build build --target zene UndoBoundsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN on the same regression | `ctest -R '^UndoBoundsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Save product patch for reverse proof | `git diff -- src/core/ControlCommandsUndo.cpp src/core/ControlReversibilityTableSnapshot.cpp docs/A16-REVERSIBILITY.md docs/CAPABILITIES-0.3.0.md docs/UNDO-BOUNDS.md > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild original sources for reverse proof | `cmake --build build --target zene UndoBoundsTest ReversibilityContractTest ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| RED with product patch reversed | `ctest -R '^UndoBoundsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild fixed targets | `cmake --build build --target zene UndoBoundsTest ReversibilityContractTest ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| GREEN affected family: 3/3 CTest targets | `ctest -R '^(UndoBoundsTest|ReversibilityContractTest|ReversibilityUndoTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-84.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

An earlier duplicate rebuild during the reverse proof did not return a visible `EXIT=` line; the identical rebuild recorded as `build-5.log` completed with `EXIT=0` before the RED run. The fix was then reapplied and verified by the final build and family run above.

## Files committed

- `src/core/ControlCommandsUndo.cpp`
- `src/core/ControlReversibilityTableSnapshot.cpp`
- `tests/src/core/UndoBoundsTest.cpp` (already registered in `tests/fork-sources.txt`)
- `docs/A16-REVERSIBILITY.md`
- `docs/CAPABILITIES-0.3.0.md`
- `docs/UNDO-BOUNDS.md`
- `docs/qwen-fixes-2026-10/cluster-new-p8-84.md`
