# Cluster new-p8-260

**Verdict: FALSE-POSITIVE**

## Finding and evidence

The watcher at `src/gui/AccessibleAnnouncements.cpp:117-128` interprets any modified-to-clean transition as a save. On this branch, however, Undo cannot make that transition:

- `src/core/ProjectJournal.cpp:112-144` ends a successful undo with `Engine::getSong()->setModified()`. The no-argument overload at `src/core/Song.cpp:2466-2468` sets the flag to true.
- The only calls that clear the flag are `src/core/Song.cpp:1485` (new-project setup), `src/core/Song.cpp:1848` (project load), and `src/core/Song.cpp:2106` (`noteProjectSaved()`, after a successful save). The load transition is guarded by `m_loading` at `AccessibleAnnouncements.cpp:131-136`; a new project has no filename for the queued save announcement at lines 125-127.
- `rg -n "m_modified\\s*=|setModified\\(false\\)" include/Song.h src/core/Song.cpp src/core/ProjectJournal.cpp` found only the assignment in `Song::setModified(bool)` and those three clear calls.

Thus the reported “edit, then undo to saved state” path does not clear `Song::isModified()` here. No production behavior was changed.

## Evidence table

| Check | Result |
|---|---|
| RED attempt | No valid bug-reproducing RED. The exploratory test failed at its setup assertion because the chosen item-list edit does not mark the song modified. Details and exact command are below. |
| GREEN baseline | `cd build/tests && ctest -R ItemListPanelTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs/test-2.log' 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=0; 1/1 CTest target passed. |
| Affected-family count | 1 CTest target: `ItemListPanelTest`, 1/1 passed. |
| Complexity gate | EXIT=0 (`gate-3.log`). |
| File-length gate | EXIT=0 (`gate-4.log`). |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p8-260.md` only. |

## Commands and results

Exploratory attempt, discarded because it did not reach the alleged behavior:

- `mkdir -p '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs' && cmake --build build --target ItemListPanelTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs/build-1.log' 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=2. The test draft called private `Song::setModified(bool)`.
- `cmake --build build --target ItemListPanelTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs/build-2.log' 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=0.
- `cd build/tests && ctest -R ItemListPanelTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs/test-1.log' 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=8. The draft failed at `QVERIFY(song->isModified())` immediately after the item-list edit, confirming that this edit was not a valid way to reproduce the reported dirty-state transition. This is not counted as RED proof.

After removing that invalid draft:

- `cmake --build build --target ItemListPanelTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs/build-3.log' 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=0.
- `cd build/tests && ctest -R ItemListPanelTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs/test-2.log' 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=0; 1/1 passed.
- `cmake --build build --target zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs/build-4.log' 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=0.
- `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs/gate-3.log' 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=0.
- `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-260.r1/logs/gate-4.log' 2>&1; code=$?; echo EXIT=$code; exit $code` — EXIT=0.

The requested `fff` grep tool was unavailable in this environment; build and test logs were searched with `rg`.
