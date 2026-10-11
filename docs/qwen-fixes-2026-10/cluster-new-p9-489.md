# Cluster new-p9-489

**Verdict: ALREADY-FIXED**

The reported runtime undo path is already protected on this branch. `journalTrackRemoval()` restores the checkpoint, clears the restored folder's serialized mixer index with `setMixerChannel(-1)`, and only then calls `ensureRoutingChannel()` (`src/core/ControlStructuralSupport.cpp:241-247`). The restored folder therefore allocates a fresh channel even when the deleted channel's old index is still in range.

`Mixer::deleteChannel()` notifies live folders before channel indices shift (`src/core/Mixer.cpp:1127-1136`); `TrackFolder::channelDeleted()` clears a folder whose own channel was deleted and renumbers folders above it (`src/tracks/TrackFolder.cpp:424-454`). The in-range adoption branch in `ensureRoutingChannel()` remains, but the undo callback that handles the finding's restore scenario does not pass that stale index to it (`src/tracks/TrackFolder.cpp:466-501`).

The existing regression `undoingAFolderDeleteDoesNotAdoptTheStripThatSlidIntoItsIndex()` constructs a later unrelated strip, removes and undoes a routing folder, and checks the unrelated strip's name and the restored folder's newly allocated channel (`tests/src/core/TrackFolderTest.cpp:683-717`). It passed in the rebuilt test family. No production or test code was changed; the RED test and patch-reversal proof were not run because the finding is already fixed on the identified path.

## Verification evidence

| Check | Exact command | Result |
|---|---|---|
| RED | Not run; existing code clears the stale index and the existing regression covers this path. | N/A |
| Build | `cmake --build build --target zene TrackFolderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-489.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status` (repository root) | EXIT=0 |
| Existing regression / GREEN confirmation | `ctest -R '^TrackFolderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-489.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | EXIT=0; 1/1 registered CTest test executed, 0 failed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-489.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status` (repository root) | EXIT=0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-489.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status` (repository root) | EXIT=0 |

The fff grep tool was not available in the active tool list, so the log folder was searched with `rg -n` for `error`, `FAILED`, and `Assertion`; all three searches had no matches (rg exit 1 for no matches).

## Committed files

- `docs/qwen-fixes-2026-10/cluster-new-p9-489.md` (this report only)
