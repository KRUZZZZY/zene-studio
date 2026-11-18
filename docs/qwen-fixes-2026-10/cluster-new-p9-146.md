# Cluster new-p9-146: FadeButton parent

Verdict: **FIXED**

The report was real. In [FadeButton.cpp](../../src/gui/tracks/FadeButton.cpp:54), the palette constructor delegated to the color constructor without forwarding `parent`, so a caller supplying a parent received a top-level widget. It now passes `parent` at line 59. The regression in [VcaStripLifetimeTest.cpp](../../tests/src/gui/VcaStripLifetimeTest.cpp:102) constructs the button with a parent and checks `parentWidget()` without putting the button in a layout.

## Regression evidence

The affected ctest family is `VcaStripLifetimeTest`: one ctest test was selected and executed in each run. Before the fix, the new assertion failed because `parentWidget()` was null. With the fix, ctest reported 1/1 tests passed. The reverse-patch check reproduced the failure, and reapplying the fix restored the pass.

Build and test output was captured under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-146.r1/logs`.

| State | Command (working directory) | Log | EXIT |
|---|---|---|---:|
| Original code build | `cmake --build build --target zene VcaStripLifetimeTest` (worktree) | `build-1.log` | 0 |
| Original code regression (RED) | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure` (`build/tests`) | `test-1.log` | 8 |
| Fixed code build | `cmake --build build --target zene VcaStripLifetimeTest` (worktree) | `build-2.log` | 0 |
| Fixed code regression (GREEN) | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure` (`build/tests`) | `test-2.log` | 0 |
| Reverse product patch | `git diff -- src/gui/tracks/FadeButton.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-146.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-146.r1/logs/fix.patch` (worktree) | `fix.patch` | 0 |
| Reversed code build | `cmake --build build --target zene VcaStripLifetimeTest` (worktree) | `build-3.log` | 0 |
| Reversed code regression (RED) | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure` (`build/tests`) | `test-3.log` | 8 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-146.r1/logs/fix.patch` (worktree) | `fix.patch` | 0 |
| Restored code build | `cmake --build build --target zene VcaStripLifetimeTest` (worktree) | `build-4.log` | 0 |
| Restored code regression (GREEN) | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure` (`build/tests`) | `test-4.log` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` (worktree) | `gate-1.log` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check` (worktree) | `gate-2.log` | 0 |
| Whitespace check | `git diff --check` (worktree) | — | 0 |

## Files committed

- `src/gui/tracks/FadeButton.cpp` — forward the supplied parent through the palette constructor.
- `tests/src/gui/VcaStripLifetimeTest.cpp` — add the parent ownership regression.
- `tests/upstream-modifications.txt` — record the inherited source change and its coverage.
- `docs/qwen-fixes-2026-10/cluster-new-p9-146.md` — this evidence report.
