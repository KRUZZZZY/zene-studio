FALSE-POSITIVE

# Cluster new-p15-65: "A file disappearing during search can crash result insertion"

Branch `bugs/qwen-new-p15-65`, base `639e1d23e`. Finding: `src/gui/FileBrowser.cpp` `onSearchMatch` can pass a null item to `addTopLevelItem`, which the finding says can crash.

## Evidence

- **The null path is reachable.** `src/gui/FileBrowser.cpp:255-258` leaves `item` null when neither `fileInfo.isDir()` nor `fileInfo.isFile()` is true. `src/gui/FileBrowser.cpp:260` then calls `m_searchTreeWidget->addTopLevelItem(item)` with no guard. The worker emits `foundMatch` at `src/gui/FileSearchJob.cpp:96` from its own `QFileInfo` check, and the GUI handles it through a queued connection (`src/gui/FileBrowser.cpp:116`). So a file can vanish between emit and handling.
- **The crash does not happen on the linked Qt.** The build links Qt 6.12.0: `build/CMakeCache.txt` has `Qt6_DIR=/usr/lib/cmake/Qt6`, `Qt6Widgets_VERSION` is 6.12.0 in `/usr/lib/cmake/Qt6Widgets/Qt6WidgetsConfigVersionImpl.cmake`, and `ldd build/zene` resolves `libQt6Widgets.so.6` to `/usr/lib/libQt6Widgets.so.6`. A standalone probe (`QTreeWidget tree; tree.addTopLevelItem(nullptr); tree.insertTopLevelItem(0, nullptr);`) returned `count=0` with exit 0. Null insertion is a no-op in Qt 6.12. The scoping brief cited Qt 5 source, which is not the library this build uses.
- **Effect of the null item:** nothing is inserted, which is the same result the finding's proposed early return would give. There is no crash and no user-visible or state difference, so there is no defect to fix.

## Decision

No product change. The finding's claim (crash on null insertion) does not hold on the Qt this build uses, and the null case already behaves like the proposed fix. Adding a guard would be a no-op, so I did not make one.

No regression test was added: the reported failure does not occur, so there is no behaviour a test could pin that fails on the current code. The probe above is the proof. The existing family test `FileSearchJobTest` passes on this tree.

## Commands run (EXIT codes; logs in `new-p15-65.r1/logs`)

| Command | EXIT | Log |
| --- | --- | --- |
| `g++ -std=c++20 -fPIC $(pkg-config --cflags Qt6Widgets) probe.cpp -o probe $(pkg-config --libs Qt6Widgets)` | 0 | `probe-compile-1.log` |
| `QT_QPA_PLATFORM=offscreen ./probe` (prints `count=0`) | 0 | `probe-run-1.log` |
| `ldd build/zene \| grep Qt6Widgets` | (resolved to `/usr/lib/libQt6Widgets.so.6`) | - |
| `python3 .../qwen/feature_check.py <worktree>` | 1 | `feature-check-1.log` |
| `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-1.log` |
| `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce-1.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression-1.log` |
| `tests/scripted/check-namespace` | 0 | `gate-check-namespace-1.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-1.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-1.log` |
| `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-1.log` |
| `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-1.log` |
| `cmake --build build --target FileSearchJobTest -j4` (first attempt) | 2 | `build-FileSearchJobTest-1.log` (ccache could not write its temp file outside the write-jail) |
| `CCACHE_DISABLE=1 cmake --build build --target FileSearchJobTest -j4` | 0 | `build-FileSearchJobTest-2.log` |
| `cd build/tests && ctest -R '^FileSearchJobTest$' --output-on-failure` | 0 (1/1 test run, passed) | `test-FileSearchJobTest-2.log` |

`feature_check.py` exit 1 is an environment failure: it writes its per-gate logs to `<worktree>-feature-check/` in the parent directory, which is outside the write-jail (`PermissionError`, `feature-check-1.log`). I ran the same gate commands directly in the worktree instead. Those are the six `SIMPLE` gates; the two `RATCHET` gates are the ones marked red above.

Complexity and file-length are red on the train. Their regression lines (`gate-complexity-1.log`: 7 lines; `gate-file-length-1.log`: 22 lines) name `WasmSandbox`, `XyPadPairTest`, `DawProjectReadTracks`, `ControlCommandsMixerTools`, `ControlCommandsSessionRecordLand`, `ControlCommandsMixerStrip`, `ControlCommandsShortcuts`, `Vst3Host`, `TakeLane`, `ArrangeToolsTest`, `CollabTransportTest`. None of these files is changed on this branch, and neither `src/gui/FileBrowser.cpp` nor `src/gui/FileSearchJob.cpp` appears in the output. No item is new in a changed file, so both gates are clear for this change.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p15-65.md` (this report). No product, test or registration files changed.

## Self-check

- **The regression test FAILS with the product change reverted:** not applicable. There is no product change, and the reported crash does not reproduce on Qt 6.12 (probe `count=0`, exit 0).
- **The test is deterministic:** not applicable; no test was added.
- **The fix is at the cause and complete:** no fix. The null insertion is already a no-op, so the cause is not a defect. Other `addTopLevelItem` calls in the same file (`src/gui/FileBrowser.cpp:310`, `315`, `379`, `429`) insert non-null items and are unchanged.
- **No collateral damage:** nothing changed; `FileSearchJobTest` passes (exit 0).
- **Verdict evidence (FALSE-POSITIVE):** the code path (`src/gui/FileBrowser.cpp:255-260`) and the Qt 6.12 probe above.
- **Realtime / undo / commands:** not touched; no new control commands.
- **Gates:** six gates pass directly (exit 0); complexity and file-length are red on the train for items in files this branch does not touch.
