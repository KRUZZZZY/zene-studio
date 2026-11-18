# Cluster new-p10-418: folder indentation reaches settings controls

Verdict: **BLOCKED**

## Finding and fix

The finding is real. `TrackContainerView::realignTracks()` calls
`TrackView::updateFolderIndent()` (`src/gui/editors/TrackContainerView.cpp:323`).
Pattern and automation track labels are manually positioned in the settings
widget, so changing that widget's contents margins does not move them.

`src/gui/tracks/TrackView.cpp:160-166` now reapplies indentation after the
subclass constructor has created its controls. In
`src/gui/tracks/TrackView.cpp:176-202`, direct children of a settings widget
without a layout are positioned from their saved base x coordinate plus 10 px
per folder level. Repeated realignments keep the same offset. The existing
accessible folder description remains in place.

The regression in `tests/src/gui/FolderSummaryLaneTest.cpp:141-153` creates a
pattern track inside a folder and requires its label to move from x=3 to x=13.
On the original code the assertion observed x=3. The same test failed again when
the production patch was reversed, and passed after restoration.

The code and test are verified, but both required repository-wide gates return
`EXIT=1` on unrelated files. The complexity gate reports six new over-target
functions in other files; the file-length gate reports fourteen unrelated new
files over 500 code lines. Neither gate names a file changed for this fix. I did
not change those files or re-anchor either gate, so acceptance remains blocked.

## Evidence

All build, test, and gate output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs`.
Builds ran from the worktree root; CTest ran from `build/tests`.

| Evidence | Exact command | Result |
| --- | --- | --- |
| RED on original code | `ctest -R '^FolderSummaryLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=8`; 6 passed, 1 failed. The label x value was 3; expected 13. |
| Initial GREEN | `ctest -R '^FolderSummaryLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0`; 1/1 CTest target passed. |
| Reverse-patch removal | `git diff -- src/gui/tracks/TrackView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit $status` | `EXIT=0`. |
| RED with production patch removed | `ctest -R '^FolderSummaryLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=8`; the same label-indent assertion failed, 6 other QtTest cases passed. |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit $status` | `EXIT=0`. |
| Final GREEN and family count | `ctest -R '^FolderSummaryLaneTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/test-5.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=0`; 7/7 QtTest cases passed, and 1/1 CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=1`; six new over-target functions are reported in files outside this change. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | `EXIT=1`; fourteen unrelated new files exceed 500 code lines. |

### Build command record

| Log | Exact command | Exit |
| --- | --- | --- |
| `build-2.log` | `cmake --build build --target FolderSummaryLaneTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| `build-3.log` | `cmake --build build --target zene FolderSummaryLaneTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| `build-4.log` | `cmake --build build --target zene FolderSummaryLaneTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| `build-5.log` | `cmake --build build --target zene FolderSummaryLaneTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-418.r1/logs/build-5.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |

The requested `fff grep` tool was unavailable in this session. Logs were searched
with `rg` for error, failure, and assertion markers.

## Files committed

- `src/gui/tracks/TrackView.cpp`
- `tests/src/gui/FolderSummaryLaneTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-418.md`
