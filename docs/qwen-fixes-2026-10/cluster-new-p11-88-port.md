# Cluster new-p11-88-port: folders inflate mixer-sharing loss

**Verdict: FIXED (ported).** The bug was still present on `bugs/qwen-new-p11-88-port`
(branch tip `b02a0ff2b`, the 050/m0-train-5 line). The fix from `bugs/qwen-new-p11-88`
commit `7af609ecb` was re-applied by hand, not merged or cherry-picked.

**Gates: both exit 1, pre-existing on this branch.** Their REGRESSION sets are identical
with and without this change (see below), and none names a file this change touches.
They are not caused by the port and were not fixed here (out of scope for one bug).

## What was wrong

`src/core/DawProjectSession.cpp:309-313` (before the fix) counted every track without a
mixer-channel model as lost mixer sharing:

```cpp
else
{
    // A track without a mixer model cannot emit a destination IDREF.
    local.mixerSharingLost++;
}
```

A folder has no mixer channel by design, so every folder inflated `mixer_sharing_lost`.
The import side already says so: `src/core/DawProjectSession.cpp:540` states that a
missing channel on a folder "is not a failure".

## The change

- `src/core/DawProjectSession.cpp`: removed the `else` branch (`:309-313` before the
  edit). The track still writes no destination when it has no channel; it is no longer
  counted as a loss.
- `include/DawProjectInterchange.h` (`:359-362`): comment-only update. The old comment
  described the counted case, which is now wrong. Comment-only, so no ABI or behaviour
  change.
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp` (`:1648-1667`): new
  `aFolderDoesNotCountAsMixerSharing()`. It creates one folder and one instrument track,
  asserts the folder has no mixer model and the instrument has one, and expects
  `loss.mixerSharingLost == 0`.

No new source files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt`
need no change (`DawProjectSession.cpp` and `DawProjectInterchange.h` are already
registered in `fork-sources.txt`).

**Open point for the owner:** after this change nothing increments `mixerSharingLost`.
The current tree has no duplicate-strip pass (the older line had one, at its
`DawProjectSession.cpp:354-360`), and `:277-278` states that shared strips are not a
loss. The key `mixer_sharing_lost` (`src/core/DawProjectModel.cpp:369`) is therefore now
always 0. I kept the field so the report shape does not change. Removing it is a schema
decision for the owner.

## Evidence

| Check | Command (run from the worktree) | Result |
| --- | --- | --- |
| Build, initial | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j2` | EXIT=2: ccache could not write its temp file under `/run/user/1000` (sandbox denial). Not a code error. |
| Build, RED | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p11-88 cmake --build build --target zene DawProjectInterchangeRoundTripTest -j2` | EXIT=0 |
| **RED** (unfixed code) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | **EXIT=8**. `FAIL! : ...aFolderDoesNotCountAsMixerSharing() Actual (loss.mixerSharingLost): 1, Expected (0): 0`. Totals 54 passed, 1 failed. |
| Build, GREEN | same build command, after the fix | EXIT=0 |
| **GREEN** (fixed code) | `ctest -R DawProjectInterchangeRoundTripTest -V` | **EXIT=0**. `Totals: 55 passed, 0 failed` (54 existing + the new test). |
| Reverse-patch: save | `git diff -- src/core/DawProjectSession.cpp include/DawProjectInterchange.h > fix.patch` | (product files only) |
| Reverse-patch: revert | `git apply -R fix.patch` | EXIT=0 |
| Reverse-patch: build | build command above | EXIT=0 |
| Reverse-patch: RED | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | **EXIT=8**, same assertion (`Actual 1, Expected 0`). The test catches the bug. |
| Reverse-patch: re-apply | `git apply fix.patch` | EXIT=0 |
| Reverse-patch: build | build command above | EXIT=0 |
| Reverse-patch: GREEN | `ctest -R DawProjectInterchangeRoundTripTest -V` | EXIT=0, 55 passed |
| Dependents: build | `CCACHE_TEMPDIR=... cmake --build build --target DawProjectDeflateImportTest ProjectContainerTest -j2` | EXIT=0 (both include the changed header) |
| Dependents: test | `ctest -R 'DawProject\|ProjectContainer' -V` | EXIT=8 only because 3 `ProjectContainer*` executables were not yet built ("Could not find executable"); not failures |
| Dependents: build | `CCACHE_TEMPDIR=... cmake --build build --target ProjectContainerEntriesTest ProjectContainerSaveTest ProjectContainerSaveAsGuiTest -j2` | EXIT=0 |
| **Affected family** | `ctest -R 'DawProject\|ProjectContainer'` | **EXIT=0, 6 of 6 tests passed**: #84 ProjectContainerTest, #85 ProjectContainerEntriesTest, #86 ProjectContainerSaveTest, #294 DawProjectInterchangeRoundTripTest, #295 DawProjectDeflateImportTest, #384 ProjectContainerSaveAsGuiTest |
| Complexity gate (with fix) | `bash tests/complexity-gate.sh --check` | **EXIT=1**. 20 REGRESSION lines across both gates, e.g. `parseNotes@94-151@src/core/DawProjectReadTracks.cpp (CCN 13)`, `dotCentreAlong@76-120@tests/src/gui/XyPadPairTest.cpp (CCN 16)`. |
| File-length gate (with fix) | `bash tests/file-length-gate.sh --check` | **EXIT=1**. e.g. `REGRESSION: new file over 500 lines: src/core/SmfInterchangeReader.cpp (504)`. |
| Gates with ALL changes reverted | `git diff > all-changes.patch && git apply -R all-changes.patch`, both gates, `git apply all-changes.patch` | complexity EXIT=1, file-length EXIT=1. The REGRESSION set is identical to the set with the fix (compared line by line). |

The gate logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-88-port.r1/logs/`
(`gate-1.log`, `gate-2.log`, `gate-3-baseline-tree.log`, `gate-4-baseline-tree.log`).
Build and test logs are `build-1..7.log` and `test-1..6.log` in the same folder.

## Notes

- The `fff` search tools were not used for code search in this task; `Grep`/`rg` was used
  instead.
- The brief said the round-0 commits were already on this branch. They are not: branch
  tip `b02a0ff2b` has no `new-p11-88` commit. The earlier fix (`7af609ecb`) is on
  `bugs/qwen-new-p11-88` only. No round-0 review text was included with the brief, so
  there was no separate round-0 list to address beyond the finding.
- The ccache temp-dir override (`CCACHE_TEMPDIR=/tmp/...`) is an environment setting for
  the build command only. No configuration file was changed.
