# Cluster new-p10-63-port — flatten keeps folder membership

Verdict: **FIXED** (port of `bugs/qwen-new-p10-63` commit `533ec55f5`, written by hand against this branch). Not ALREADY-FIXED: the newest line had no folder handling in `flatten`.

Gate caveat: both ratchet gates exit 1 on this tree. Their failure lists are **identical with and without this change** (checked below), and none of the listed files is touched by this fix. Baselines were not changed.

## Evidence

- Bug present before the fix: `src/core/ControlCommandsTrackFlatten.cpp:68-72` (`landAudioTrackId`, HEAD) created the replacement with `landAudioTrack` and never set its parent folder; `flatten` captured no folder id.
- Fix: `src/core/ControlCommandsTrackFlatten.cpp:70-80` — `landAudioTrackId(..., folderId)` looks up the folder with `Engine::getSong()->findTrackById(folderId)` (returns nullptr for `-1`), and calls `Track::setParentFolder` (`:76`), which also informs the folder (`childLinked`) so routing mode binds the child to the folder's mixer channel (`src/core/Track.cpp:399-407`).
- `folderId` is captured from `source->parentFolder()` before removal at `src/core/ControlCommandsTrackFlatten.cpp:106`; it is passed to the redo lambda (`:117-120`) and to the initial landing (`:124`).
- Regression: `tests/control-success-paths.py:190` — new `check_track_flatten_keeps_folder` (folder in routing mode, source flattened, undo, redo), called at `:343`. It runs in the full `ControlSuccessPaths` sequence (no focus mode is added; the file's existing `--stopped-seek-only` mode is unchanged).
- Register: `tests/upstream-modifications.txt` — the existing `ControlCommandsTrackFlatten.cpp` line gained the reason for this change (one entry per path is the convention; no new file, so `fork-sources.txt` is unchanged).

## Commands and results

`L=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-63-port.r1/logs`

| Step | Command | EXIT |
|---|---|---|
| Build zene, pre-fix (no-op) | `cmake --build build --target zene > $L/build-1.log 2>&1; echo EXIT=$?` | 0 |
| **RED** (pre-fix) | `cd build/tests && ctest -R '^ControlSuccessPaths$' --output-on-failure > $L/test-1-red.log 2>&1; echo EXIT=$?` | 8 |
| Build with fix, first try | `cmake --build build --target zene -j2 > $L/build-2.log 2>&1; echo EXIT=$?` | 2 (ccache: cannot write `/run/user/1000/ccache-tmp`; no compile errors) |
| Build with fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10-63 cmake --build build --target zene -j2 > $L/build-3.log 2>&1; echo EXIT=$?` | 0 |
| **GREEN** (fix) | `cd build/tests && ctest -R '^ControlSuccessPaths$' --output-on-failure > $L/test-3-green.log 2>&1; echo EXIT=$?` | 8 (folder checks ok; only the two MIDI checks fail — see below) |
| Proof: revert fix | `git diff -- src/core/ControlCommandsTrackFlatten.cpp > $L/fix.patch && git apply -R $L/fix.patch; echo EXIT=$?` | 0 |
| Proof: build reverted | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10-63 cmake --build build --target zene -j2 > $L/build-4.log 2>&1; echo EXIT=$?` | 0 |
| **RED** (reverted, re-proof) | `cd build/tests && ctest -R '^ControlSuccessPaths$' --output-on-failure > $L/test-4-reverted-red.log 2>&1; echo EXIT=$?` | 8 — `track.flatten keeps its replacement in the routing folder` FAILED (`folder: ''`), `flatten redo keeps its replacement in the routing folder` FAILED |
| Proof: reapply fix | `git apply $L/fix.patch; echo EXIT=$?` | 0 |
| Build with fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10-63 cmake --build build --target zene -j2 > $L/build-5.log 2>&1; echo EXIT=$?` | 0 |
| **GREEN** (reapplied) | `cd build/tests && ctest -R '^ControlSuccessPaths$' --output-on-failure > $L/test-5-reapplied-green.log 2>&1; echo EXIT=$?` | 8 — the three folder checks `ok`; same two MIDI checks fail |
| Baseline C++ test build | `cmake --build build --target zene ControlSuccessPathsTest -j2` (on reverted tree) | 0 |
| Baseline C++ test | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure > $L/test-8-base-cpp.log 2>&1` (reverted tree) | 8 — `controllerFeedbackTakeoverAndTemplates` fails at `ControlSuccessPathsTest.cpp:217` (`controller.template_save`) |
| Affected family build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10-63 cmake --build build --target BounceInPlaceTest ControlBounceRenderTest ControlSuccessPathsTest TrackFolderTest TrackFolderMenuTest -j2 > $L/build-6-tests.log 2>&1; echo EXIT=$?` | 0 |
| Affected family (final) | `cd build/tests && ctest -R 'ControlSuccessPaths\|TrackFolder\|Flatten\|Bounce' --output-on-failure > $L/test-9-final-family.log 2>&1; echo EXIT=$?` | 8 — 7 tests executed, 71% passed |
| Complexity gate (final) | `bash tests/complexity-gate.sh --check > $L/gate-5-final-complexity.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate (final) | `bash tests/file-length-gate.sh --check > $L/gate-6-final-file-length.log 2>&1; echo EXIT=$?` | 1 |
| Complexity gate, reverted tree | `bash tests/complexity-gate.sh --check > $L/gate-3-base-complexity.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate, reverted tree | `bash tests/file-length-gate.sh --check > $L/gate-4-base-file-length.log 2>&1; echo EXIT=$?` | 1 |

### Affected family (final run, `test-9-final-family.log`)

| Test | Result |
|---|---|
| BounceInPlaceTest | Passed |
| ControlBounceRenderTest | Passed |
| TrackFolderTest | Passed |
| TrackFolderMenuTest | Passed |
| ControlTrackFolderTranscript | Skipped (no loadable instrument — not counted as a pass) |
| ControlSuccessPathsTest (C++) | Failed — identical on the reverted tree (`test-8-base-cpp.log`) |
| ControlSuccessPaths (Python) | Failed — only the two pre-existing MIDI checks; all three new folder checks pass |

Pre-existing, not caused by this change (same failures in the RED run on untouched product code, `test-1-red.log`):

- `project.import adds the MIDI file's track` — "no import filter in this build takes … (MIDI sequences and Hydrogen songs …)"
- `project.export_midi writes a .mid` — "the midiexport filter is not loadable in this process"
- C++ `controllerFeedbackTakeoverAndTemplates` — `controller.template_save` could not write under `~/Documents/Zene Studio/presets/controller-templates/`; reproduced on the reverted tree.

### Gate failures (pre-existing, not fixed)

Both gates fail on this tree. Their `REGRESSION` lists are the same with and without this change (`gate-5` vs `gate-3`, `gate-6` vs `gate-4`, compared with `diff`). The failures are in files this fix does not touch (for example `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `tests/src/core/VideoTrackTest.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`). Fixing them means changing many unrelated files or gate baselines, which the task forbids, so they are left as found. Neither gate names `ControlCommandsTrackFlatten.cpp` or `control-success-paths.py`.

## Other notes

- `ccache` could not write under `/run/user/1000`; this was worked around with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p10-63` for the build commands only. No config was changed.
- Five test executables were not built in this build directory; `BounceInPlaceTest ControlBounceRenderTest ControlSuccessPathsTest TrackFolderTest TrackFolderMenuTest` were built before running (`build-6-tests.log`, EXIT=0).
- Two early attempts wrote into a different log folder (`new-p10-63.r1`) by mistake; the write was refused (existing read-only files) and no command ran. All logs cited above are in `new-p10-63-port.r1/logs`.

## Files committed

- `src/core/ControlCommandsTrackFlatten.cpp`
- `tests/control-success-paths.py`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-63-port.md`

No new files were added, so `tests/fork-sources.txt` is unchanged.
