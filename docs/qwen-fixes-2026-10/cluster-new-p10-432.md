# Cluster new-p10-432 — FIXED

## Evidence

`TrackFolder::routeChild()` now returns before reading or changing a child's channel model if the mixer is absent or the folder channel is negative or outside the mixer channel list ([TrackFolder.cpp:284](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-432/src/tracks/TrackFolder.cpp:284)). This prevents a child joining a restored routing folder without a channel from being clamped to master or another channel.

The regression is [TrackFolderTest.cpp:603](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-432/tests/src/core/TrackFolderTest.cpp:603). It constructs the post-failure restore state and verifies that the child keeps its own channel and receives no saved routing binding. The setup helper is [TrackFolderNoChannelTestSupport.h:27](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-432/tests/src/core/TrackFolderNoChannelTestSupport.h:27), registered in `tests/fork-sources.txt`.

`Mixer::createChannel()` currently appends a channel and does not expose a deterministic refusal seam, so the test constructs the resulting `Routing` plus channel `-1` state directly. The inverse-patch run confirms this test fails without the production guard.

## Test and gate evidence

| Check | Exact command | Exit and result |
|---|---|---|
| Baseline build | `cmake --build build --target zene TrackFolderTest` | `0` (`build-2.log`) |
| RED before fix | `ctest -R '^TrackFolderTest$' --output-on-failure` from `build/tests` | `8` (`test-1.log`); failed at `resultingChannel == channel`; 25 passed, 1 failed (26 QtTest cases) |
| Inverse-patch build | `cmake --build build --target zene TrackFolderTest` | `0` (`build-8.log`) |
| Inverse-patch RED | `ctest -R '^TrackFolderTest$' --output-on-failure` from `build/tests` | `8` (`test-7.log`); same regression assertion failed |
| Restored-fix build | `cmake --build build --target zene TrackFolderTest` | `0` (`build-10.log`) |
| GREEN | `ctest -R '^TrackFolderTest$' --output-on-failure` from `build/tests` | `0` (`test-9.log`); 1/1 ctest target passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | `0` (`gate-7.log`) |
| File length gate | `bash tests/file-length-gate.sh --check` | `0` (`gate-8.log`) |

The family test count is one ctest target, `TrackFolderTest`, containing 26 QtTest cases. The RED runs report 25 passing and the regression failing; the GREEN run passes the complete target.

An initial file-length gate run exited `1` (`gate-2.log`) after the existing 744-line `TrackFolderTest.cpp` was added to `fork-sources.txt`. I removed that oversized entry and registered the new, small helper header instead. The gate configuration and baselines were not changed. Log searches used `rg` because the requested `fff` tool was not available in this environment.

## Files committed

- `src/tracks/TrackFolder.cpp`
- `tests/src/core/TrackFolderTest.cpp`
- `tests/src/core/TrackFolderNoChannelTestSupport.h`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-432.md`
