# Cluster new-p12-179

Verdict: **FIXED**

The round 2 review was valid. In this branch the historical `PerformView.cpp` path is absent, but `SongEditorWindow` inherits `Editor`, whose Space handler still toggled transport on auto-repeat. `Editor::keyPressEvent` now returns for auto-repeat Space before either `togglePause()` or `togglePlayStop()`.

## File and test evidence

- `src/gui/editors/Editor.cpp:142-155`: Space key handling; line 146 now suppresses auto-repeat before the transport actions.
- `tests/src/gui/TrackFolderMenuTest.cpp:211-224`: while playback is active, sends auto-repeat Space to both `MainWindow` and `SongEditorWindow`, and asserts playback stays active after each event.
- Existing coverage remains: `src/gui/MainWindow.cpp:1635` suppresses repeat toggles at the main window; `src/gui/SessionGridView.cpp:411` suppresses repeated Enter launches. `tests/src/gui/SessionGridViewTest.cpp:152-180` still verifies arrow-key auto-repeat moves selection.

The affected CTest family contains two targets: `SessionGridViewTest` and `TrackFolderMenuTest`. On each RED run, `TrackFolderMenuTest::autoRepeatSpaceDoesNotRetogglePlayback` fails at the new SongEditor assertion (4 other QtTest cases pass); the family reports 1 failed target out of 2. Each GREEN run reports 2/2 targets passing.

## Build and regression commands

Commands below were run from the worktree root, except CTest commands, which were run from `build/tests`. Each command's output was redirected to the named log, and the observed exit code was appended to that log.

| Phase | Exact command | Exit | Log / result |
|---|---|---:|---|
| Initial build before fix | `cmake --build build --target zene SessionGridViewTest TrackFolderMenuTest` | 0 | `build-1.log` |
| Initial RED | <code>ctest -R 'SessionGridViewTest&#124;TrackFolderMenuTest' --output-on-failure</code> | 8 | `test-1.log`; Editor repeat assertion failed |
| Fixed build | `cmake --build build --target zene SessionGridViewTest TrackFolderMenuTest` | 0 | `build-2.log` |
| GREEN | <code>ctest -R 'SessionGridViewTest&#124;TrackFolderMenuTest' --output-on-failure</code> | 0 | `test-2.log`; 2/2 targets passed |

## Reversal proof

| Check | Exact command | Exit | Log / result |
|---|---|---:|---|
| Save and reverse product patch | `git diff -- src/gui/editors/Editor.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-179.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-179.r2/logs/fix.patch` | 0 | Guard removed |
| Rebuild without fix | `cmake --build build --target zene SessionGridViewTest TrackFolderMenuTest` | 0 | `build-3.log` |
| RED without fix | <code>ctest -R 'SessionGridViewTest&#124;TrackFolderMenuTest' --output-on-failure</code> | 8 | `test-3.log`; same Editor repeat assertion failed |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-179.r2/logs/fix.patch` | 0 | Guard restored |
| Rebuild with fix restored | `cmake --build build --target zene SessionGridViewTest TrackFolderMenuTest` | 0 | `build-4.log` |
| GREEN after restoration | <code>ctest -R 'SessionGridViewTest&#124;TrackFolderMenuTest' --output-on-failure</code> | 0 | `test-4.log`; 2/2 targets passed |

## Gates

| Exact command | Exit | Log |
|---|---:|---|
| `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log` |

The fff grep tool was not available in this session (`command -v fff` returned no path); logs were checked with single-term `rg` searches.

## Files committed

- `src/gui/editors/Editor.cpp`
- `tests/src/gui/TrackFolderMenuTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-179.md`
