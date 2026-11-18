# Cluster new-p12-349: locked clip recoloring

**Verdict: FIXED**

`include/EditLocks.h:41-47` says a locked clip cannot be edited. `ClipView::setColor()` had no lock check, so this branch recolored locked clips and then marked the song modified. The fix skips locked clips and colors already equal to the requested value, and calls `setModified()` only after an unlocked selected clip's color changes (`src/gui/clips/ClipView.cpp:347-379`).

The regression (`tests/src/gui/ArrangerTimelineTest.cpp:256-279`) locks and selects one clip, saves to establish a clean baseline, invokes the public random-color slot, and verifies both that the song stays clean and the clip's color is unchanged.

## Evidence

| Check | Command and result |
|---|---|
| Baseline RED | From `build/tests`: `ctest -R '^ArrangerTimelineTest$' --output-on-failure` — **EXIT=8** (`test-2.log`); `!song->isModified()` failed with “recoloring only locked clips marked the song modified”. |
| Reverse-patch RED | Same ctest command after reverting `fix.patch` and rebuilding — **EXIT=8** (`test-4.log`), same dirty-state assertion. |
| Fixed GREEN | Same ctest command after restoring `fix.patch` and rebuilding — **EXIT=0** (`test-5.log`); CTest reported 100% passed, 0 failures out of 1. |
| Affected family count | **1 CTest test target executed:** `ArrangerTimelineTest` (1/1 passed). |
| Complexity gate | `bash tests/complexity-gate.sh --check` — **EXIT=0** (`gate-3.log`). |
| File-length gate | `bash tests/file-length-gate.sh --check` — **EXIT=0** (`gate-4.log`). |

The initial draft regression also exposed that locked clips were recolored on this branch: its color-preservation assertion failed in `test-1.log`. The regression was then ordered to fail first on the reported dirty-state symptom, and the fix enforces the lock contract as well as conditional dirty marking.

## Build and reverse-patch commands

- `cmake --build build --target ArrangerTimelineTest` — **EXIT=0** (`build-2.log`), before the fix.
- `cmake --build build --target zene ArrangerTimelineTest` — **EXIT=0** (`build-3.log`, `build-4.log`, `build-5.log`); both `zene` and the affected test target were rebuilt for the fixed, reverted, and restored runs.
- `git diff -- src/gui/clips/ClipView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-349.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-349.r1/logs/fix.patch` — **EXIT=0**.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-349.r1/logs/fix.patch` — **EXIT=0**.

The `fff grep` tool was not available in this session; build and test logs were searched with `rg -n` in the specified log folder.

## Files in the cluster commit

- `src/gui/clips/ClipView.cpp`
- `tests/src/gui/ArrangerTimelineTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-349.md`
