FIXED

## Verdict: REAL

A folder's serialized `mixch` is an ownership claim on a live mixer channel, and it was
adopted verbatim from a COPY payload. Two live folders then named one strip, and the
destructor deletes the strip it recorded.

- `src/tracks/TrackFolder.cpp:164-169` — `~TrackFolder` deletes `m_mixerChannel` when it is in
  range. Nothing checks that this folder is the one that created it.
- `src/tracks/TrackFolder.cpp:648-666` — `loadTrackSpecificSettings` read `mode`, `mixch` and
  `prevch` straight out of the element it was given, with no distinction between a project
  document and a copy of another folder's element.
- The copy payloads: `src/gui/tracks/TrackGrip.cpp:78-96` (Ctrl-drag saves the track into a
  `DataFile` of type `DragNDropData`), `src/core/Track.cpp:642-651` (`Track::clone` saves it
  into a `<clonedtrack>`), and the two consumers — `src/gui/tracks/TrackView.cpp:433`
  (`m_track->restoreState(dataFile.content().firstChild().toElement())`) and
  `src/gui/editors/TrackContainerView.cpp:625-632`
  (`Track::create(dataFile.content().firstChild().toElement(), m_tc)`).
- Both wrappers are already in the tree's copy-container list:
  `src/core/ProjectIds.cpp:103-113` names `clonedtrack` and `dnddata`, and
  `ProjectIds::isDocumentElement` (`src/core/ProjectIds.cpp:251-262`) is how the tree already
  tells a copy from a document.
- The sibling precedent for the same rule, which held before this fix and did not hold for a
  folder's channel: `src/core/Track.cpp:1046-1050` — an instrument track's `mixlink` is read
  only `if( ProjectIds::isDocumentElement( element ) )`, and is `-1` for a copy ("a copy has
  none of its own").

Reachable paths, all three reproduced by the new tests:

| Path | Where | A copy of a routing folder is made by |
| --- | --- | --- |
| Clone Track | `src/gui/tracks/TrackOperationsWidget.cpp:301-305` → `Track::clone()` | re-loading the source's `<track>` element out of `<clonedtrack>` |
| Ctrl-drag onto another folder row | `src/gui/tracks/TrackView.cpp:393-405` accepts `track_7` on a folder row, `:433` restores it | `restoreState()` of the source's element out of `<dnddata>` |
| Ctrl-drag copy | `src/gui/editors/TrackContainerView.cpp:625-632` | `Track::create()` of the source's element out of `<dnddata>` |

One honest correction to the finding: the Ctrl-drag of a folder onto the container's empty
space is refused before the drop, because `TrackContainerView::dragEnterEvent`
(`src/gui/editors/TrackContainerView.cpp:536-549`) admits only `track_0` and `track_2`. The
duplicate ownership the finding describes is real; the drop that reaches it for a folder is
the folder-to-folder one (`TrackView::dragEnterEvent` matches the target row's own type), and
the "new track" branch it names is the same `Track::create` call that `Track::clone` makes.

## The fix (cause, not symptom)

`src/tracks/TrackFolder.cpp:659-665` — the routing half is not read out of a copy payload:

```cpp
	if( !ProjectIds::isDocumentElement( element ) )
	{
		m_mode = Mode::Group;
		m_mixerChannel = -1;
		m_savedChildChannels.clear();
	}
```

Why this state and not "give the copy a fresh channel": a copy has none of the source's
children (they keep their `folder` attribute, which names the source's id), and
`TrackFolder::setMode(Mode::Routing)` refuses a folder with no children — an empty routing
folder is a state this class does not allow. A routing folder with no channel likewise "has
nothing to sum into", which is exactly the state `TrackFolder::channelDeleted`
(`src/tracks/TrackFolder.cpp:490-518`, the `Mode::Group` fallback at `:512-517`) leaves behind
after the same loss, so the copy lands in group mode with no bindings. Nothing is allocated,
freed or journalled on a load path.

Callers and siblings checked:

| Caller / sibling | Element wrapper | Treated as | Behaviour |
| --- | --- | --- | --- |
| `Track::loadTrack` project load | `<trackcontainer>` in `<song>` | document | unchanged |
| journal checkpoint restore | `<journaldata>` | document | unchanged |
| `control::restoreTrackFromXml` (undo of a track remove, DAWproject import, bounce, flatten, role) | `<zene-track-snapshot>` | document | unchanged; the undo path already clears the index and re-allocates (`src/core/ControlStructuralSupport.cpp:276-292`) |
| `Track::clone` / Clone Track | `<clonedtrack>` | copy | fixed |
| Ctrl-drag copy (`TrackContainerView::dropEvent`) | `<dnddata>` | copy | fixed |
| Ctrl-drag onto a folder row (`TrackView::dropEvent`) | `<dnddata>` | copy | fixed |
| `tests/src/core/TrackFolderNoChannelTestSupport.h:35-43` (detached `<trackfolder>`) | none | document | unchanged — that fixture still gets routing mode, and its test passes |
| `Track::savePreset` / `loadPreset` | `<instrumenttracksettings>` | copy | unchanged (a folder preset is not a path this tree has) |
| `project.copy_tracks` / `project.paste_tracks`, `project.import_tracks` | fresh `<track>` document root | document | unchanged by this fix; see Other findings for the pre-existing `mixch=0` it writes |

## Evidence (this session; logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p17-193.r1/logs`)

Every command ran from the worktree root unless it says `build/tests`, and every log line
below is the unpiped exit code appended to the log.

### RED — the tests, no product change (`build-1-red.log`, `test-1-red.log`)

| Command | Log | Result |
| --- | --- | --- |
| `cmake --build build --target TrackFolderTest -j4` | `build-1-red.log` | EXIT=0 |
| `cd build/tests && ctest -R '^TrackFolderTest$' --output-on-failure` | `test-1-red.log` | EXIT=8 — `Totals: 40 passed, 3 failed` |

```
FAIL!  : TrackFolderTest::aClonedRoutingFolderClaimsNoChannelOfTheOriginal() 'copiedFolder->mixerChannel() != channel' returned FALSE. (the copy claims the original folder's mixer channel)
FAIL!  : TrackFolderTest::aCtrlDragCopyOfARoutingFolderClaimsNoChannelOfTheOriginal() 'copiedFolder->mixerChannel() != channel' returned FALSE. (the dropped copy claims the original folder's mixer channel)
FAIL!  : TrackFolderTest::aRoutingFolderDroppedOnAnotherRowClaimsNoChannelOfTheOriginal() 'target->mixerChannel() != channel' returned FALSE. (the row the folder was dropped on claims the dragged folder's channel)
```

### GREEN — with the fix (`build-2-green.log`, `test-2-green.log`, `test-2b-green-probe.log`)

| Command | Log | Result |
| --- | --- | --- |
| `cmake --build build --target zene TrackFolderTest -j4` | `build-2-green.log` | EXIT=0 |
| `cd build/tests && ctest -R '^TrackFolderTest$' --output-on-failure` | `test-2-green.log` | EXIT=0 — `1/1 Test #446: TrackFolderTest ... Passed` |
| `cd build/tests && ./TrackFolderTest aClonedRoutingFolderClaimsNoChannelOfTheOriginal aCtrlDragCopyOfARoutingFolderClaimsNoChannelOfTheOriginal aRoutingFolderDroppedOnAnotherRowClaimsNoChannelOfTheOriginal` | `test-2b-green-probe.log` | EXIT=0 — `Totals: 5 passed, 0 failed` (the three slots and init/cleanup) |

### 4b — the test catches the bug (`patch-*.log`, `build-3-reverted.log`, `test-3-red-probe.log`, `build-4-restored.log`, `test-4-green-probe.log`, `test-5-green-ctest.log`)

| Command | Log | Result |
| --- | --- | --- |
| `git diff -- src/tracks/TrackFolder.cpp > .../logs/fix.patch` | `patch-1-reverse.log` | EXIT=0 |
| `git apply -R .../logs/fix.patch` | `patch-1-reverse.log` | EXIT=0 (only the test file remains modified) |
| `cmake --build build --target TrackFolderTest -j4` | `build-3-reverted.log` | EXIT=0 |
| `cd build/tests && ./TrackFolderTest <the three slots>` | `test-3-red-probe.log` | **EXIT=3** — `Totals: 2 passed, 3 failed` |
| `git apply .../logs/fix.patch` | `patch-2-restore.log` | EXIT=0 |
| `cmake --build build --target zene TrackFolderTest -j4` | `build-4-restored.log` | EXIT=0 |
| `cd build/tests && ./TrackFolderTest <the three slots>` | `test-4-green-probe.log` | EXIT=0 — `Totals: 5 passed, 0 failed` |
| `cd build/tests && ctest -R '^TrackFolderTest$' --output-on-failure` | `test-5-green-ctest.log` | EXIT=0 |

The reverted-run failures are the same three assertions quoted above, at
`tests/src/core/TrackFolderTest.cpp:1125`, `:1169` and `:1211`.

### Family and gates

| Command | Log | Result |
| --- | --- | --- |
| `cmake --build build -j4` | `build-5-all.log` | EXIT=97 — refused by the shared build guard ("build a named target"), recorded, not a build failure |
| `cmake --build build --target TrackFolderMenuTest FolderSummaryLaneTest TrackAddRoutingTest -j4` | `build-6-family.log` | EXIT=0 |
| `cd build/tests && ctest -R '(Folder|Routing)' --output-on-failure` | `test-6-family.log` | EXIT=8 — 9 targets "Not Run" (their binaries were not built by that command) and `ControlRoutingCommands` Failed |
| `git apply -R .../logs/fix.patch` | `patch-3-reverse-again.log` | EXIT=0 |
| `cmake --build build --target zene -j4` | `build-7-reverted-zene.log` | EXIT=0 |
| `cd build/tests && ctest -R '^ControlRoutingCommands$' --output-on-failure` | `test-7-routing-reverted.log` | EXIT=8 — **the same `IndexError: list index out of range` at `tests/control-routing-commands.py:213` with the fix reverted: pre-existing, not this change** |
| `git apply .../logs/fix.patch` | `patch-4-restore-again.log` | EXIT=0 |
| `cmake --build build --target zene TrackFolderTest TrackFolderMenuTest FolderSummaryLaneTest TrackAddRoutingTest -j4` | `build-8-restored-final.log` | EXIT=0 |
| `cd build/tests && ctest -R '^(TrackFolderTest\|TrackFolderMenuTest\|FolderSummaryLaneTest\|TrackAddRoutingTest)$' --output-on-failure` | `test-8-family-final.log` | EXIT=0 — 4/4 passed |
| `FEATURE_CHECK_LOG_DIR=.../logs/feature-check python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | EXIT=0 — `RESULT: PASS`, all eight rows PASS |

`gates-1.log`:

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Files changed

- `src/tracks/TrackFolder.cpp` — the fix (one include, the copy rule at `:659-665`, and the
  contract stated in the function's comment).
- `tests/src/core/TrackFolderTest.cpp` — three regressions:
  `aClonedRoutingFolderClaimsNoChannelOfTheOriginal`,
  `aCtrlDragCopyOfARoutingFolderClaimsNoChannelOfTheOriginal`,
  `aRoutingFolderDroppedOnAnotherRowClaimsNoChannelOfTheOriginal`.
- `tests/upstream-modifications.txt` — the ledger line for that inherited test file.
- `docs/qwen-fixes-2026-10/cluster-new-p17-193.md` — this report.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   Reverted: `test-3-red-probe.log` EXIT=3 (`Totals: 2 passed, 3 failed`), and the family run
   without the change, `test-1-red.log`, EXIT=8 (`40 passed, 3 failed`). Restored:
   `test-4-green-probe.log` EXIT=0 (`5 passed, 0 failed`) and `test-5-green-ctest.log` EXIT=0.
   The reverted run is the same binary rebuilt from the same tree with only
   `git diff -- src/tracks/TrackFolder.cpp` reversed (`patch-1-reverse.log` EXIT=0), and every
   rebuild between the runs exited 0 (`build-3-reverted.log`, `build-4-restored.log`).

2. **The test is deterministic.** No timers, sleeps, threads or wall-clock values. It uses the
   real object paths and their own returns: `Track::clone()` (the cloned document), a
   `DataFile(DataFile::Type::DragNDropData)` payload built with `Track::saveState` and read
   back with `Track::create` / `Track::restoreState` (exactly what the drop does), plus
   `Mixer::numChannels()`, `Mixer::mixerChannel(index)->m_name` and
   `child->mixerChannelModel()->value()` as oracles. Two consecutive runs of the same binary
   gave the same verdicts (`test-2b-green-probe.log` and `test-4-green-probe.log`, both
   `5 passed, 0 failed`; `test-1-red.log` and `test-3-red-probe.log` both 3 failures).

3. **The fix is at the cause and complete.** The cause is the adoption of a live channel index
   out of a copy payload, not the deletion that follows it, so the deletion guard
   (`src/tracks/TrackFolder.cpp:164-169`) is untouched and no legitimate owner loses its
   ability to delete its strip. Every path that can load a folder element was walked and is
   listed in the table above: the six document paths keep adopting `mixch` (project load,
   journal checkpoint, `control::restoreTrackFromXml` in all five of its callers, the detached
   fixture element, presets), and the three copy payloads (`<clonedtrack>`, `<dnddata>` used
   by `Track::create`, `<dnddata>` used by `restoreState`) stop adopting it. The same rule is
   what `Track::loadTrack:1046-1050` already applies to `mixlink`, and it is the tree's own
   `ProjectIds::isDocumentElement` seam, so no new notion of "copy" was invented.

4. **No collateral damage.** A project document, a journal checkpoint and a snapshot are never
   a copy container, so an existing project loads byte-for-byte as before — held by the
   existing round-trip slots of this test file, which all pass
   (`membershipAndModeSurviveASaveAndReopen`, `everyFolderFieldResetsOnAbsence`,
   `routingModePointsEveryChildAtTheFolderChannel`, `undoingARoutingFolderDeleteRestoresItsChannel`,
   `undoingAFolderDeleteDoesNotAdoptTheStripThatSlidIntoItsIndex`,
   `aRoutingFolderWithoutAChannelDoesNotRouteItsChild`, `aSavedChildChannelFollowsItsStripWhenALowerChannelIsDeleted`:
   `test-5-green-ctest.log` EXIT=0). The change adds no limit and no rejection, so no other
   caller can exceed anything: it only declines to adopt a foreign index on a copy. Nothing
   runs on the audio thread — `loadTrackSpecificSettings` is reached from `Track::loadTrack`,
   which is a load/restore path, and the added lines are three member writes; the file is not
   on a render path. Undo stays one honest step: no command was added and no journal
   behaviour changed; the copy paths that were one undo step (Ctrl-drag →
   `control::recordCreatedTrack`) still are.

5. **ALREADY-FIXED / FALSE-POSITIVE:** not claimed — the verdict is REAL, with the code
   references listed under "Verdict" and the RED run above.

## Other findings

- `project.copy_tracks` / `project.paste_tracks` and `project.import_tracks`
  (`src/core/ControlCommandsImportTracks.cpp:86-94`, `detach()`) set every `mixch` attribute to
  `0`, folder elements included. For a `<trackfolder>` that index names the mixer master, which
  is not a channel a folder may own: a pasted or imported routing folder comes in "routing" on
  the master, and `TrackFolder::setName` would rename the master strip. Pre-existing, unrelated
  to this finding's duplicate-ownership bug, and not changed here.
- `tests/control-routing-commands.py:213` indexes `loaded[0]` after a failed device load
  instead of stopping, so the run ends in `IndexError` rather than a reported check failure.
  It fails the same way with this fix reverted (`test-7-routing-reverted.log`), so it is not a
  regression from this change.
- `ctest -R '(Folder|Routing)'` reports nine targets "Not Run" because their binaries were not
  built by the named-target command used here; they are not failures of this change.
