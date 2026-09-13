# LANE-STATE — `030/folder-tracks` in `zene-030/wft`

Owner items **3+20+21** (folder tracks; the layout/workspace-presets half is 0.5.0 and is NOT built).
Base: `334790219` (the current tip of `release/0.3.0`; no merge was in progress in `zene-030`).

## The four-part scope contract — status

| part | state |
|---|---|
| 1. engine work | **in**, compiles, 9 of 11 registered unit-test slots pass |
| 2. control-surface command group | **in** (9 new ids + `track.add type=folder` + folder fields on the 3 reads), A16 rows in all four table files |
| 3. a registered proof | **in**: ctest `TrackFolderTest` (C++) and ctest `ControlTrackFolderTranscript` (`tests/control-track-folder.py`, socket + audio) |
| 4. UI-absence lines | **in**: `docs/RELEASE-NOTES-v0.3.0-alpha.md` (new section) and `docs/KNOWN-LIMITATIONS.md` (new bullet) |

## What is known to work (measured)

* `bash tools/local-ci.sh --build-dir build --jobs 2` → **configure EXIT=0, build EXIT=0, 0 errors**
  (`build/zene` and `build/tests/TrackFolderTest` both produced).
* `TrackFolderTest` — 9 passed / 2 failed:
  PASS: `folderIsATrackTypeThatHoldsTracks`, `membershipAndModeSurviveASaveAndReopen`,
  `everyFolderFieldResetsOnAbsence`, `routingModePointsEveryChildAtTheFolderChannel`,
  `legacyProjectGrowsNoFolderState`, `danglingFolderIsRepaired`,
  `unparentingARootTrackIsANoOpNotACrash`.
  FAIL: `refusesSelfAndCycles` (a stale `QString` reason — fixed in the working tree, needs a re-run),
  `visibilitySetsSurviveASaveAndReopen` (see the open defect below).
* The socket transcript gets all the way through membership, routing wiring, flags, sets, refusals and
  the transaction report; it died at `check_save_and_reopen` on a **real bug that is now fixed**
  (`track.set_folder` with the child already at the container root dereferenced a null folder; the
  engine crashed). Fixed and covered by the new unit-test slot above — the transcript has not been
  re-run since.
* A live probe (`/tmp/wft-verify/probe-folder.py`) proves routing mode works end to end over the
  socket: the folder takes mixer channel 1, every child reports `mixer_channel: 1`, taking a child out
  restores it, and `track.set_routing false` releases the channel (`mixer_channel: -1`).

## THE OPEN DEFECT (start here)

**The named visibility sets are not written to the project file.**
Evidence (temporary qWarning instrumentation, now removed): `TrackContainer::saveSettings` is entered
with `m_visibilitySets.size() == 1` on the Song (`this == 0x56cfffb385f0`), the block that calls
`saveVisibilitySets()` is reached, and yet the saved file contains **no** `<visibilitysets>` element
and no set name. The tracks written by the same function ARE in the file, and `folder="…"` /
`visible="0"` / `<trackfolder mode="routing" prevch="…" mixch="1"/>` are all written, so the
`<trackcontainer>` element being written is the right one.

Next step (one command): re-add the diagnostic inside `TrackContainer::saveVisibilitySets` itself —
print `element.isNull()`, `parent.tagName()`, and `parent.childNodes().count()` before and after
`parent.appendChild(element)` — rebuild only `TrackFolderTest`
(`cmake --build build -j2 --target TrackFolderTest`) and run it. That settles whether the append does
not land or the element is dropped later. A likely suspect, not yet checked: `DataFile::write` /
`SerialisationHook` filtering, or an element-ordering drop in the same save.

## Remaining acceptance work (in order)

1. Fix the visibility-set persistence defect above; re-run `TrackFolderTest` (expect 11/11).
2. Re-run the socket transcript: `cd build/tests && QT_QPA_PLATFORM=offscreen python3 ../../tests/control-track-folder.py ../zene`
   (unpiped, exit code wanted). It needs `PYTHONPATH` unset — the script adds its own directory.
3. Regenerate the MCP snapshot (the ninth touch-point) against a live instance of THIS build and commit
   `tools/mcp-zene-control/zene_control/commands_snapshot.json`; then `cd build/tests && ctest -R ControlCommandsSnapshot`.
4. Regenerate **both** manifests with their own recipes (`/tmp/wft-verify/regen-manifests.sh` — copy it
   into the worktree first) and require `REPRODUCES` for each; `git commit --amend` so the new files are
   declared in the same commit.
5. `bash tools/local-ci.sh --build-dir build --jobs 2` again (ctest must be 100%), then
   `bash tests/run-all-gates.sh` (exit 0 or 3, never 1), then the rest of the acceptance list in
   `WAVE-1-BRIEFS.md` — every number unpiped, logs in `/tmp/wft-verify/`.
6. Delete `build/` when the lane is done (disk: the tree is ~18 GB).

## Files added

`include/TrackFolder.h`, `src/tracks/TrackFolder.cpp`, `src/core/ControlCommandsTrackFolder.cpp`,
`src/core/ControlCommandsTrackFolderSets.cpp`, `src/core/TrackContainerFolders.cpp`,
`tests/src/core/TrackFolderTest.cpp`, `tests/control-track-folder.py`,
`docs/TRACK-FOLDER-DESIGN.md` (cherry-picked from `next/trackfolder`).

## Deliberate deviations, to report

* The command ids follow `docs/TRACK-FOLDER-DESIGN.md` §8.3 (`track.set_folder`,
  `track.folder_set_collapsed`, `track.set_routing`, `track.set_pinned`) plus one read the design did
  not name (`track.folder_get_state`) and the four `track.visibility_set_*` verbs. The design's
  `track.visibility_set_*` row was "named, not specified"; the four verbs here are that naming.
* `Track::visible` and the named sets are persisted **view state**: applying a set does not mute and
  does not change a render (the transcript proves the negative control). No GUI reads them.
* `Track::mixerChannelModel()` became a `virtual` on `Track` (returning nullptr) with overrides on
  `InstrumentTrack`/`SampleTrack`, so routing mode re-binds a child in one call instead of a
  `dynamic_cast` to the two types that have a channel today.
