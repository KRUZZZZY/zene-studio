# Cluster new-p10-170

**Verdict: FIXED**

`InspectorClipFields.cpp:177-185` now computes the selected-note shift in `qint64`, checks every resulting position against `MaxSongLength`, and narrows only after the whole selection passes. The regression in `DevicePanelTest.cpp:674-718` types `2147483647 ticks` with selected notes at ticks 100 and 200 and asserts that an out-of-range batch is refused before `note.edit` is dispatched.

The original code failed that assertion because the invalid batch reached `note.edit`. Reversing the product patch reproduced the same failure; restoring it passed.

## Verification evidence

All build, test and gate output was written under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-170.r1/logs`. The affected family is `DevicePanelTest`: each CTest run executed **1 CTest test** (not zero).

| Check | Command and working directory | Exit | Result |
|---|---|---:|---|
| Original build | `cmake --build build --target DevicePanelTest zene` (worktree) | 0 | `build-1.log` |
| Initial RED | `ctest -R DevicePanelTest --output-on-failure` (`build/tests`) | 8 | `test-1.log`; assertion at `DevicePanelTest.cpp:716` |
| Fixed build | `cmake --build build --target DevicePanelTest zene` (worktree) | 0 | `build-2.log` |
| Initial GREEN | `ctest -R DevicePanelTest --output-on-failure` (`build/tests`) | 0 | `test-2.log`; 1/1 CTest tests passed |
| Save product patch | `git diff -- src/gui/shell/InspectorClipFields.cpp > .../logs/fix.patch` | 0 | `fix.patch` |
| Remove fix | `git apply -R .../logs/fix.patch` | 0 | Product fix reversed |
| No-fix rebuild | `cmake --build build --target DevicePanelTest zene` (worktree) | 0 | `build-3.log` |
| No-fix RED | `ctest -R DevicePanelTest --output-on-failure` (`build/tests`) | 8 | `test-3.log`; same assertion at `DevicePanelTest.cpp:716` |
| Restore fix | `git apply .../logs/fix.patch` | 0 | Product fix restored |
| Final rebuild | `cmake --build build --target DevicePanelTest zene` (worktree) | 0 | `build-4.log` |
| Final GREEN | `ctest -R DevicePanelTest --output-on-failure` (`build/tests`) | 0 | `test-4.log`; 1/1 CTest tests passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` (worktree) | 0 | Final run: `gate-3.log` |
| File length gate | `bash tests/file-length-gate.sh --check` (worktree) | 0 | Final run: `gate-4.log` |

`fff` grep was not available in the exposed tools for this session, so the required log terms were searched with `rg`; the RED assertion and CTest pass count were also checked directly in the captured logs.

## Files committed

- `src/gui/shell/InspectorClipFields.cpp`
- `tests/src/gui/DevicePanelTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-170.md`
