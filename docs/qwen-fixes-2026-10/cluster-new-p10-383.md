# Cluster new-p10-383

Verdict: **FIXED**

`SessionModel::restoreState()` now checks that both legacy dimensions convert to integers and lie within `MaxTracks` / `MaxScenes` before marking the block claimed or calling the clamping `resize()`. Invalid dimensions return `false`; the existing `Song::restoreNamedSection()` path then preserves the legacy block as unclaimed XML.

Evidence:

- [src/core/SessionModel.cpp](../../src/core/SessionModel.cpp#L317): conversion and bounds checks precede `m_hadSessionBlock = true` and `resize()`.
- [tests/src/core/SessionModelTest.cpp](../../tests/src/core/SessionModelTest.cpp#L316): load/save regression covers `tracks="bad"`, `tracks="257"`, and `scenes="513"`; it checks that each marker survives and no `<z:scenes>` replacement is emitted.
- [src/core/Song.cpp](../../src/core/Song.cpp#L2334): an unclaimed legacy section remains eligible for preservation after `restoreState()` declines it.
- The regression failed before the production fix because `bad-tracks` disappeared from the saved project. Reversing the production patch after the fix reproduced that same failure; restoring it passed.

Build commands ran from the worktree root; CTest commands ran from `build/tests`.

Evidence table:

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED before fix | `ctest -R '^SessionModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-2.log; exit "$rc"` | 8 | Marker count was 0 instead of 1. |
| GREEN after fix | `ctest -R '^SessionModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-3.log; exit "$rc"` | 0 | Passed. |
| Counterfactual RED | `ctest -R '^SessionModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-4.log; exit "$rc"` | 8 | Reversing the production patch reproduced the lost-marker failure. |
| Final GREEN after reapply | `ctest -R '^SessionModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-5.log; exit "$rc"` | 0 | Passed. |
| Affected family count | `ctest -R '^SessionModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-5.log; exit "$rc"` | 0 | 1/1 CTest test executed (`SessionModelTest`). |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/gate-1.log; exit "$rc"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/gate-2.log; exit "$rc"` | 0 | Passed. |

Builds:

| Log | Exact command | Exit |
| --- | --- | ---: |
| `build-1.log` | `cmake --build build --target SessionModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-1.log; exit "$rc"` | 0 |
| `build-2.log` | `cmake --build build --target SessionModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-2.log; exit "$rc"` | 0 |
| `build-3.log` | `cmake --build build --target zene SessionModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-3.log; exit "$rc"` | 0 |
| `build-4.log` | `cmake --build build --target zene SessionModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-4.log; exit "$rc"` | 0 |
| `build-5.log` | `cmake --build build --target zene SessionModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/build-5.log; exit "$rc"` | 0 |

Counterfactual patch commands:

- `git diff -- src/core/SessionModel.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/fix.patch` — EXIT=0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/fix.patch` — EXIT=0.

An initial regression draft stopped at an internal `hadSessionBlock()` assertion before checking project output (`build-1.log` EXIT=0; `ctest -R '^SessionModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-383.r1/logs/test-1.log; exit "$rc"` EXIT=8). That assertion was removed before the RED evidence above; the regression now proves loss on the saved-project path.

Files committed:

- `src/core/SessionModel.cpp`
- `tests/src/core/SessionModelTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-383.md`
