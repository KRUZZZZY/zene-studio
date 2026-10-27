Verdict: FIXED

# Cluster new-p23-8: phaseScope window and channel safety

## Review correction

The null-window half of the finding is a false positive on this path. `phaseScope` returns `Requires` when
`controlMainWindow()` is null (`src/gui/ControlCommandsMixerView.cpp:197`), and `ControlRegistry::invoke()` runs the
handler on the UI thread (`src/core/ControlRegistry.cpp:249-259`). With no event processing between the check and
`showPhaseScope()`, its GUI-null return at `src/gui/PhaseScopeView.cpp:221-225` is not reachable there.

The channel-safety conclusion was incomplete. `PhaseScopeWindow::channel()` clamps a missing saved channel ID to
index 0 (`src/gui/PhaseScopeView.cpp:56-63, 181-184`), which is safe only while the master exists. Mixer channel
access is unchecked (`include/Mixer.h:634-637`). A loaded `trackfolder` accepts `mixch="0"` verbatim
(`src/tracks/TrackFolder.cpp:579-589`). Before this fix, both `releaseRouting()` and `~TrackFolder()` passed that
index to `Mixer::deleteChannel()`, which erases it (`src/core/Mixer.cpp:1440-1443`). With only the master present,
that leaves an empty mixer. `PhaseScopeWindow` then calls `setChannel(0)` during construction
(`src/gui/PhaseScopeView.cpp:143-151`), and dereferences `mixerChannel(0)` before `showPhaseScope()` can return.

The fix guards both folder channel-release paths: only an in-range index greater than 0 can be deleted, and the
folder's stored index is cleared even when the saved value is invalid. Valid routing-folder channels continue to be
released normally. The regression parses the saved `trackfolder` node and exercises both release and destruction.

## Regression evidence

Test: `TrackFolderTest::aMalformedFolderChannelCannotDeleteTheMaster`, in the existing registered
`tests/src/core/TrackFolderTest.cpp` target. It uses no clock, sleep, or race. The existing normal phase-scope path
also passes in `MixerMeteringTest`.

| Run | Command | Exit | Result |
|---|---|---:|---|
| RED | `CCACHE_DISABLE=1 cmake --build build --target zene TrackFolderTest -j4` | 0 | Test compiled before the product change. |
| RED | `ctest -R TrackFolderTest --output-on-failure` from `build/tests` | 8 | `masterSurvivedRelease` was false at `TrackFolderTest.cpp:570`. |
| GREEN | `CCACHE_DISABLE=1 cmake --build build --target zene TrackFolderTest -j4` | 0 | Product fix and regression target rebuilt. |
| GREEN | `ctest -R TrackFolderTest --output-on-failure` from `build/tests` | 0 | 1/1 CTest test passed. |
| REVERSED | `git diff --cached -- src/tracks/TrackFolder.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-8.r1/logs/qa-fix.patch` | 0 | Saved only the product patch. |
| REVERSED | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-8.r1/logs/qa-fix.patch` | 0 | Reversed only the product file; test remained. |
| REVERSED | `CCACHE_DISABLE=1 cmake --build build --target zene TrackFolderTest -j4` | 0 | Rebuilt without the product change. |
| REVERSED | `ctest -R TrackFolderTest --output-on-failure` from `build/tests` | 8 | Same `masterSurvivedRelease` failure. |
| RESTORED | `git checkout -- src/tracks/TrackFolder.cpp` | 0 | Restored the staged product file. |
| RESTORED | `CCACHE_DISABLE=1 cmake --build build --target zene TrackFolderTest -j4` | 0 | Rebuilt the restored product code. |
| RESTORED | `ctest -R TrackFolderTest --output-on-failure` from `build/tests` | 0 | 1/1 CTest test passed. |
| SIBLING | `CCACHE_DISABLE=1 cmake --build build --target zene TrackFolderTest MixerMeteringTest -j4` | 0 | Both affected targets rebuilt. |
| SIBLING | `ctest -R MixerMeteringTest --output-on-failure` from `build/tests` | 0 | 1/1 CTest test passed, including normal phase-scope coverage. |

Build/test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-8.r1/logs/`:
`qa-build-1.log`, `qa-test-red-1.log`, `qa-build-2.log`, `qa-test-green-1.log`, `qa-fix.patch`,
`qa-build-reversed.log`, `qa-test-reversed.log`, `qa-build-3.log`, `qa-test-green-restored.log`,
`qa-build-4.log`, and `qa-test-mixer-green.log`.

Affected family: `TrackFolderTest`, 1 CTest test; `MixerMeteringTest`, 1 CTest test. No full suite was run.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-8`: **PASS, exit 0**. All six simple gates passed; complexity and file-length report no new item in this change's files.

The original feature-check failure in the first report was environmental and stale: the reviewer ran the required gate
successfully on this machine. Complexity and file-length are interpreted by the gate's changed-file baseline filter.

## Files committed

- `src/tracks/TrackFolder.cpp`
- `tests/src/core/TrackFolderTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p23-8.md`

## Self-check

- **Regression red without the product change and green restored:** red `ctest -R TrackFolderTest --output-on-failure`
  exited 8 both before the fix and after reversing only the product file; it passed with exit 0 after restoration.
- **Deterministic:** the test parses a fixed XML node with `mixch="0"` and checks the mixer channel count. It uses no
  wall-clock wait, sleep, or race.
- **Cause and sibling paths:** `releaseRouting()` and `~TrackFolder()` were both checked and now apply the same
  positive/in-range rule. The command is the only `src/` caller of `showPhaseScope()`; the null-window guard is already
  effective on its UI-thread path.
- **No collateral damage:** valid folder indices are positive and in range, so their release behavior is unchanged.
  No saved project data is rewritten. No control command, undo behavior, or audio-thread path changed.
- **Portability:** product changes use existing C++ and Qt APIs. The regression uses `QDomDocument::setContent`, which
  is available on the supported Qt builds; no platform-specific API was added.
- **Gates:** `feature_check.py` passed with exit 0 on the final source and test changes. The report correction itself
  does not alter source or test behavior.
