IMPLEMENTED

Q050-212, track creation output routing and auto-channel assignment. Design and format: `docs/050-features/Q050-212.md`.
"Bus" is decided as a regular mixer channel, because a parallel bus takes no track input.

## Acceptance

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-212.r1/logs`.

| Criterion | Command | Exit (restored) | Exit (behaviour disabled) |
|---|---|---|---|
| `track.add` output, UI destination choice, add-N-to-new-bus | `ctest -R '^TrackAddRoutingTest$' --output-on-failure` | 0 (`test-9-restored`) | 8, 14/14 cases fail (`test-8-reverted`) |
| Auto-assignment, rename sync, disabled mode | same | 0 | 8 |
| Save/load, undo, redo for explicit and automatic | same | 0 | 8 |

Reversal method: `.cpp` bodies were neutralised (output and auto-channel ignored, sync and `mixlink` off, the toolbar and
checkbox entries removed, the bus command's build skipped), keeping the headers so the test compiles. A full reverse of
the commits cannot compile the test, because it uses the new API.

Neighbours (`test-4`, `test-5`, `test-6`): `ControlCommandsSnapshot`, `ReversibilityContractTest`, `ReversibilityUndoTest`,
`TrackFolderMenuTest`, `StableTrackIdsTest`, `MixerRoutingBackwardCompatTest`, `ControlRoutingCommands` all passed.
`ControlEditCommandsTest` exit 8, one failure, `trackSetArmManyReportsOnlyTheTracksItChanged`: the arm cannot create its take
file under `~/Documents`, which this session's write jail forbids. No change here touches arming; not re-run outside the jail.

Not run: the whole ctest suite and `run-all-gates.sh` (the brief forbids the suite; these belong to the merge point).
Not tested: the `src/core/main.cpp` readiness fix (see the feature doc). Hosted CI not run, so Qt 5.15 and MSVC are reasoned
only: the local build is Qt 6.

## Round 2 (review round 1 asked for two changes)

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-212.r2/logs`.

| Review point | Test | Exit (reversed) | Exit (restored) |
|---|---|---|---|
| File and preset drops on the arranger assign a channel | `TrackRouteDropTest::droppingAFileOrPresetOnTheArrangerFollowsThePreference` (real drag-enter and drop events, on and off) | 8 (`test-11-reverted`) | 0 (`test-12-restored`) |
| Send to new SlicerT instance assigns, in the song only | `TrackRouteDropTest::sendingASampleToSlicerTFollowsThePreferenceInTheSongOnly` (real browser context menu, both entries) | 8 | 0 |
| Route away, rename, route back: names meet in one undo step | `TrackRouteDropTest::routedBackAfterARenameTheChannelTakesTheNameInTheRoutingStep` (command and model write, depth +1, undo, redo, reopen) | 8 | 0 |

The reversal removed this round's hunks in `FileBrowser.cpp`, `TrackContainerView.cpp`, `InstrumentTrack.cpp` and `SampleTrack.cpp`;
the 3 cases of `TrackRouteDropTest` failed (`test-11-reverted`). Before the split, with all cases in one file, the same reversal failed those 3 and passed the other 16 (`test-5-reverted`). The reconcile is called from each track type's route change, so the header number and
track menu are covered as well as `track.set_mixer_channel`. Only the command and a model write were driven, not those widgets.

Neighbours (`test-7-neighbours`, `test-8`): `ControlCommandsSnapshot`, `ReversibilityContractTest`, `ReversibilityUndoTest`, `TrackFolderMenuTest`,
`StableTrackIdsTest`, `MixerRoutingBackwardCompatTest`, `ControlRoutingCommands`, `ArrangerFileDropTest`, `BrowserDragUrlTest` passed.
`ControlEditCommandsTest` exit 8 on `trackSetArmManyReportsOnlyTheTracksItChanged` only: its take file under `~/Documents` is
refused by this session's write jail, the same failure as round 1. Not re-run outside the jail.

Found, not fixed: a browser drag carries a URL, so a preset drag on the empty arranger goes to `clip.import_file` and is
probably refused (read, not run; feature doc, Known limits). It predates this unit. The container's preset branch is reached
in the test by the StringPair payload alone.

Build note: the session's ccache temp directory is outside the write jail, so builds used `CCACHE_TEMPDIR=/tmp/ccache-q212`.
Commit trailers name Claude Sonnet 5.5 because the brief requires that line; this round was run by Claude Opus 5.5.

## Self-check

Round 2 answers. The round 1 answers below stand for the rest of the unit.

- **Completion check:** the unmodified scripts cannot write `zene-feat-q050-212-feature-check` beside the worktree in this write jail
  (`PermissionError`, `feature-check-1`). They were run from `/tmp` copies differing only in that log path; `feature-check-4` exit 0, every gate PASS; `submit-3` exit 0, RESULT: PASS.
- **Fails reversed, passes restored?** Yes: `TrackRouteDropTest` build 0, exit 8 reversed (`test-11-reverted`); build 0, exit 0 restored with `TrackAddRoutingTest` (`test-12-restored`).
- **One undo step?** Yes: `undo_depth` rises by 1 on route-back through the command and through a model write, and undo and redo restore the route and the
  channel's name together. The drop and SlicerT paths have no undo step, like the other Browser opens (Known limits). No command class changed: `track.set_mixer_channel` stays `true_inverse`, and its reply names `control.undo` when a name was reconciled.
- **Audio thread?** The new call sits in the route-change slot. It returns at once off the track's own thread, so a change on the render thread touches nothing. Reasoned: TSan does not run here.
- **Old projects?** A load runs with journalling off and `isLoadingProject` set, so the reconcile does nothing; no format change this round; `MixerRoutingBackwardCompatTest` exit 0.
- **Portability?** Reasoned, not built: local build is Qt 6. The new test uses only `QDropEvent(QPointF, ...)`, `QDragEnterEvent(QPoint, ...)` and `QMimeData::removeFormat`, all in Qt 5.15; no forbidden identifiers added.

Completion check: `submit_check.py` and `feature_check.py` write a log folder beside the worktree, which this session's write
jail refuses (`PermissionError`, `feature-check-1`). They were run from `/tmp` copies differing only in that log path:
`submit-1` RESULT: PASS, `feature-check-2` exit 0, every gate PASS. The unmodified scripts did not complete here.

- **Fails reverted, passes restored?** Yes. Disabled: build exit 0, `TrackAddRoutingTest` exit 8, 14 of 14 cases fail
  (`build-10-reverted`, `test-8-reverted`). Restored: build exit 0, test exit 0 (`build-11-restored`, `test-9-restored`).
- **Every criterion proven by a test I ran?** Yes, all by `TrackAddRoutingTest`, exit 0; case per criterion in
  `docs/050-features/Q050-212.md`. Final tree: `test-10-final`, exit 0, 6 of 6 with `ControlCommandsSnapshot`, `ControlShutdown`,
  `MainCliStartupTest`, `AppearanceStartupTest`, `ControlRoutingCommands`.
- **One undo step, honest classes?** Yes. `undo_depth` rises by exactly 1 for `track.add` with a plug-in and a new channel, and
  the bus command undoes in one call. `track.add` and `track.add_to_new_bus` are `true_inverse` (`ReversibilityContractTest`
  exit 0, histogram rows=888). Exception: a track opened from the Browser has no undo step, as before this change (feature doc).
- **Audio thread?** Nothing added runs there. Channel creation takes the existing change lock on the UI thread; the link and
  name work runs on the UI thread, a rename from the loader thread being queued to it. Reasoned: TSan does not run here.
- **Old projects?** `mixlink` is written only for a linked track (`openingAProjectAssignsNothing` checks the file has none) and
  absence means no link; opening assigns nothing; `MixerRoutingBackwardCompatTest` exit 0. New data round-trips through
  `project.save`/`project.open` in the test. No render comparison was run; no render path changed.
- **Portability?** Reasoned, not built: local build is Qt 6 and hosted CI was not run. No `QT_VERSION`-gated API, `std::filesystem`
  or `windows.h` use, and a grep of the new files for the forbidden identifiers finds none. `QMenu::addAction(text)` plus
  `QObject::connect` avoids the Qt 6.3 deprecated overload. New API used by the test or across libraries is `LMMS_EXPORT`.
