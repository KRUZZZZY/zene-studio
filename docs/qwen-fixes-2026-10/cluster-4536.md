FIXED

# Cluster 4536 — reassigning a child's mixer channel breaks folder routing

## Finding

> Reassigning a child's mixer channel breaks folder routing — routing is applied when a child
> joins, but nothing restores it if the user later changes that track's mixer channel. Enable
> routing, then use the track's mixer menu to assign the child elsewhere. Its audio stops passing
> through the folder while the folder still reports routing enabled.
> (`docs/TRACK-FOLDER-DESIGN.md`, `src/tracks/TrackFolder.cpp`)

## Verdict: REAL

The intended contract is stated in the code, not inferred:

- `include/TrackFolder.h:176-179` — *"routing mode's invariant is that EVERY child of a routing
  folder points at the folder's channel"*.
- The invariant is only ever **established**, at four points: a child joins
  (`TrackFolder::childLinked`, `src/tracks/TrackFolder.cpp:244` → `routeChild`, `:294`), routing is
  turned on (`enableRouting`, `:358`), a restored folder gets its channel
  (`ensureRoutingChannel`, `:482`), and a folder channel is deleted (`channelDeleted`, `:440`).
  Nothing re-establishes it when the child's own `IntModel` is written afterwards.
- The only observers of a track's mixer-channel model are the track's own slot
  (`src/tracks/InstrumentTrack.cpp:116`, `src/tracks/SampleTrack.cpp:67` →
  `updateMixerChannel()`). No folder is connected to a child's model, so the write goes through
  silently.
- The folder's state read kept advertising routing over children it no longer summed:
  `src/core/ControlCommandsTrackFolderShared.h:146` (`"routing": folder->isRouting()`), with each
  child's own `mixer_channel` beside it (`:137`).

Reproduced before anything was changed: the regression slot asserted the refusal and instead the
reassignment went through — `test-1-red.log:27`

```
FAIL!  : TrackFolderTest::reassigningARoutedChildsMixerChannelIsRefused() '!refused.ok' returned FALSE.
(the child of a routing folder stands on ch-3 while the folder reports routing: its audio leaves the
folder's sum and the folder keeps advertising a channel it no longer sums into (refusal expected, got
channel 3))
```

That is the reported state exactly: the child moved to a channel of its own (ch-3) while the folder
still reported `routing: true` and still claimed its own summing channel.

## Fix

`src/core/ControlCommandsTrackChannel.cpp:58-79` — `track.set_mixer_channel` refuses, **typed**
(`ControlErrorKind::Refused`), a reassignment of a track whose direct parent is a routing folder
with a live channel, and names the folder, its channel and the way out:

```
%1 is summed through folder %2's mixer channel (%3): routing mode owns the channel of every child
it sums, so pointing the track elsewhere would leave the folder reporting routing while none of this
track's audio passed through it. Turn the folder's routing off first (track.set_routing with routing
false), then set the channel
```

Choices, and why they are the smallest correct fix:

- **Refused, not silently applied.** This is the finding's second remedy ("prevent direct
  reassignment while routed") and it is the honest one: the reply names the channel the track really
  plays into. The first remedy (auto-restoring the folder's channel on every write to the child's
  model) would fight `Mixer::moveChannelLeft` (`src/core/Mixer.cpp:1562-1589`), which today renumbers
  a routed child **with** the folder's strip: an unconditional restore would pull the child back to
  the folder's stale index and feed it into the wrong strip — a regression outside this bug.
- **A refusal changes nothing and records nothing**, so undo stays one honest step: the check runs
  before the model checkpoint (`ControlCommandsTrackChannel.cpp:80-83`), and the regression test
  asserts the last recorded transaction is unchanged across the refusal.
- **Naming the folder's own channel stays allowed** (`channel->index() != folderChannel`): it is a
  no-op for a child already routed, and it is how a child left off the folder channel by an older
  build gets put back.
- **No new command, no metadata change**: the id, args/result schema, `mutating` flag and the
  `TrueInverse` row in `ControlReversibilityTableLive.cpp:104` are untouched, so the
  `ControlCommandsSnapshot` canary is unaffected (it was re-run: `test-7-canary.log`).

Every other writer of a routed child's channel was checked and already keeps the invariant:

| writer | what it does |
|---|---|
| `Track::setParentFolder` → `childLinked` (`TrackFolder.cpp:244`) | routes the child on join (unchanged) |
| `TrackFolder::enableRouting` / `ensureRoutingChannel` | route every child (unchanged) |
| `Mixer::deleteChannel` (`src/core/Mixer.cpp:1415`, `:1421-1450`) | tells the folders **first**; a routed child moves down with the folder's channel, and the folder whose own channel went un-routes its children before the track walk |
| project load (`mixch`) | membership resolution calls `childLinked`, which re-routes the child — a hand-edited `mixch` cannot survive |
| `track.flatten`, `track.drum_rack_pad_add`, `track.add` with `output`, project import | create the track (its channel is set before it joins, and joining routes it) |
| `control.undo` / `control.transactions` inverse replay | goes through `track.set_mixer_channel` again, so it is covered by the same refusal, and the stack order means a `set_routing` step sits above the child's channel write |

## Regression test

`tests/src/core/TrackFolderTest.cpp:683-733`,
`TrackFolderTest::reassigningARoutedChildsMixerChannelIsRefused` (in the existing folder-test file,
beside the other routing slots). One folder with one instrument child in routing mode:

1. `track.set_mixer_channel` to another channel is **refused** (`errorKind == Refused`), the child
   is still on the folder's channel, `isRouting()` still true, the folder's channel index unchanged,
   and the last recorded transaction is unchanged (no undo step for a refusal).
2. Out of routing mode, the *same* call succeeds and the child really moves — the refusal is about
   the routing, not about the command, so it cannot be mistaken for a broken verb.

Deterministic: no timing, sleeps, threads or injected clocks — a synchronous in-process
`ControlRegistry::instance()->invoke(...)` on fixed track ids, and mixer channels the test creates
and deletes itself.

## Commands run (all from the worktree, logs in the log folder)

```
cmake --build build --target TrackFolderTest -j4 > …/logs/build-1-baseline.log 2>&1; echo EXIT=$?   EXIT=0
# RED: test added, product untouched
cmake --build build --target TrackFolderTest -j4 > …/logs/build-2-red.log 2>&1; echo EXIT=$?        EXIT=0
cd build/tests && ctest -R TrackFolderTest --output-on-failure > …/logs/test-1-red.log 2>&1; echo EXIT=$?
                                                                                                   EXIT=8
# GREEN: fix in place
cmake --build build --target TrackFolderTest zene -j4 > …/logs/build-3-green.log 2>&1; echo EXIT=$? EXIT=0
cd build/tests && ctest -R TrackFolderTest --output-on-failure > …/logs/test-2-green.log 2>&1; echo EXIT=$?
                                                                                                   EXIT=0
./TrackFolderTest | tail -1                      # Totals: 36 passed, 0 failed, 0 skipped
# 4b — the test catches the bug
git diff -- src/core/ControlCommandsTrackChannel.cpp > …/logs/fix.patch; echo EXIT=$?              EXIT=0
git apply -R …/logs/fix.patch; echo EXIT=$?                                                       EXIT=0
cmake --build build --target TrackFolderTest -j4 > …/logs/build-4-reverted.log 2>&1; echo EXIT=$?  EXIT=0
cd build/tests && ctest -R TrackFolderTest --output-on-failure > …/logs/test-3-reverted.log 2>&1; echo CTEST_EXIT=$?
                                                                                                   EXIT=8
git apply …/logs/fix.patch; echo EXIT=$?                                                          EXIT=0
cmake --build build --target TrackFolderTest zene -j4 > …/logs/build-5-restored.log 2>&1; echo EXIT=$?
                                                                                                   EXIT=0
cd build/tests && ctest -R TrackFolderTest --output-on-failure > …/logs/test-4-restored.log 2>&1; echo CTEST_EXIT=$?
                                                                                                   EXIT=0
# family
cmake --build build --target TrackAddRoutingTest TrackFolderMenuTest FolderSummaryLaneTest -j4 \
  > …/logs/build-6-family.log 2>&1; echo EXIT=$?                                                   EXIT=0
cd build/tests && ctest -R "TrackFolderTest|TrackAddRoutingTest|TrackFolderMenuTest|FolderSummaryLaneTest|ControlTrackFolderTranscript|ControlSuccessPaths|ControlEngineWiring" \
  --output-on-failure > …/logs/test-5-family.log 2>&1; echo CTEST_EXIT=$?                           EXIT=8
#   TrackFolderTest, TrackFolderMenuTest, TrackAddRoutingTest, FolderSummaryLaneTest,
#   ControlTrackFolderTranscript PASSED. The two transcript failures were the missing MIDI
#   import/export plugins in this partially built tree ("no import filter in this build takes …");
#   after building them the same suites pass:
cmake --build build --target midiimport midiexport amplifier -j4 > …/logs/build-7-plugins.log 2>&1; echo EXIT=$?
                                                                                                    EXIT=0
cd build/tests && ctest -R "ControlEngineWiring|ControlSuccessPaths" --output-on-failure \
  > …/logs/test-6-family-plugins.log 2>&1; echo CTEST_EXIT=$?                                       EXIT=8
#   ControlSuccessPaths PASSED. ControlEngineWiring still fails its own environment check
#   "the catalogue lists at least two loadable built-in effects (devices=['dev-0'])" — the plugin
#   catalogue of this tree, nothing to do with tracks, folders or channels.
# canary + folder transcript
cd build/tests && ctest -R "^ControlCommandsSnapshot$|^ControlTrackFolderTranscript$" \
  --output-on-failure > …/logs/test-7-canary.log 2>&1; echo CTEST_EXIT=$?                          EXIT=0
# gates (run twice: with the fix and test in the working tree, then again with the
# ledger line and this report in place — both tables are identical)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > …/logs/gates-1.log 2>&1; echo EXIT=$?
                                                                                                   EXIT=0
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > …/logs/gates-2-precommit.log 2>&1; echo EXIT=$?
                                                                                                   EXIT=0
```

The gates table (`gates-1.log`, `gates-2-precommit.log`):

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

The completion check on the committed branch (`submit-1.log`):

```
python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 4536 > …/logs/submit-1.log 2>&1; echo EXIT=$?

PRE-SUBMIT CHECK for 4536
  PASS  commits on the branch
  PASS  working tree clean
  PASS  report docs/qwen-fixes-2026-10/cluster-4536.md
  PASS  tests added or changed
  PASS  new test files registered in tests/CMakeLists.txt
  PASS  gates (feature_check.py)
RESULT: PASS - submit
EXIT=0
```

(It has no FAIL. The "tests added or changed" row lists transcripts the train already carried; this
cluster's own test change is `tests/src/core/TrackFolderTest.cpp`, which needed no CMakeLists edit
because the file is already a registered target.)

## Self-check

**The regression test FAILS with the product change reverted and PASSES with it.** Reverted:
`test-3-reverted.log`, `CTEST_EXIT=8`, the slot itself `FAIL!` (quoted above). Restored:
`test-4-restored.log`, `CTEST_EXIT=0`, and the binary's own tally `Totals: 36 passed, 0 failed`.

**The test is deterministic.** No wall-clock timing, no sleeps, no threads, no races: one
synchronous `ControlRegistry::instance()->invoke("track.set_mixer_channel", …)` on tracks the test
creates, with mixer channels it creates and deletes itself. Nothing is observed across a period or a
timer, so there is no seam to inject.

**The fix is at the cause and complete.** The invariant is stated in
`include/TrackFolder.h:176-179` and was only established on join / mode-on / restore / channel
delete. The user-facing write — `track.set_mixer_channel`, which is what the Inspector's channel
menu calls (`src/gui/shell/InspectorPanel.cpp:196-222`) and what a script calls — is now refused for
a routed child. Every other writer was walked (table above) and already preserves the invariant:
`childLinked`, `enableRouting`, `ensureRoutingChannel`, `Mixer::deleteChannel`, project load,
`track.flatten` / `track.drum_rack_pad_add` / `track.add` with `output`, and inverse replay through
the same command. Sibling commands in the `track.*` group that touch membership or channels were
checked: `track.set_folder` / `track.folder_move` (membership → `childLinked`/`childUnlinked`),
`track.set_routing` (the mode switch, untouched), `track.folder_get_state` / `track.get_state` /
`track.list` / `track.overview` (reads, untouched), `track.add` (no folder yet when the channel is
set), `track.remove` (folder told, then the container deletes). None needed a change; the one that
did — `track.set_mixer_channel` — is the one fixed.

**No collateral damage.** The change is inside one command handler; the load, save, teardown and
mixer paths are untouched, so old projects load exactly as before (proved by
`control-track-folder.py` — `ControlTrackFolderTranscript` PASSED — which round-trips a routing
folder through real save/load, and by the suite's other 35 slots — `Totals: 36 passed, 0 failed`). The new check
cannot reject a legitimate writer: it only fires for a track whose direct parent is a routing folder
with a live channel, and it explicitly allows naming that folder's channel, so re-routing a child
back onto the folder is always accepted. `ControlSuccessPaths`' folder checks — including "track.
flatten keeps its replacement in the routing folder" and both undo/redo rows — PASSED after the
missing MIDI plugins were built (`test-6-family-plugins.log`).

**Realtime and undo.** Nothing is added to the audio thread: the check runs in a control command on
the control/GUI thread, before any model write, and allocates only a `QString` for the refusal
message. A refusal records no journal step (asserted by the test), and the command's reversibility
class is unchanged (`ControlReversibilityTableLive.cpp:104`, `TrueInverse`), so the A16 histogram
and the command snapshot are untouched: `ControlCommandsSnapshot` was re-run with the folder
transcript and both PASSED (`test-7-canary.log`, `CTEST_EXIT=0`).

**Sibling commands (the check's list).** `submit_check` names `app.*` and two `arrangement.*`
commands because the file it matches them from contains those id strings; none of them touches a
track's mixer channel, a folder, or a model this fix reads. The `track.*` commands that DO touch
channels or membership are the ones this fix had to be consistent with, and they are listed in the
paragraph above: `track.set_folder` / `track.folder_move` (membership → `childLinked` /
`childUnlinked`, so joining routes and leaving un-routes), `track.set_routing` (the mode switch;
`releaseRouting()` still gives every child back its saved channel, which is exactly the call this
refusal tells the user to make), `track.folder_get_state` / `track.get_state` / `track.list` /
`track.overview` (reads, reporting each child's real channel), `track.add` (`output` is set before
the track can have a folder), `track.drum_rack_pad_add` / `track.flatten` (create the track, then
join it), `track.remove` (`childUnlinked`, then the container deletes). None of them changes here.

**One deliberate behaviour change, stated.** A script or menu that reassigns a routed child now gets
a typed `refused` instead of a channel it cannot have. That is the fix; the way out is named in the
message (`track.set_routing` with `routing` false), and the same call succeeds once the folder is
out of routing mode — asserted by the test's second half.

## Other findings

- `Mixer::moveChannelLeft` (`src/core/Mixer.cpp:1562-1589`) renumbers a routed child with its strip
  but never tells a routing folder that its own channel index moved, so `mixch` can end up naming a
  different strip after `mixer.move_channel`. Pre-existing, outside this finding, not changed here —
  it is also the reason the "auto-restore the folder channel on every write" remedy was rejected.
- `ControlEngineWiring` fails in this tree on its own environment check ("the catalogue lists at
  least two loadable built-in effects", `devices=['dev-0']`). Unrelated to this fix; the tree has
  almost no plugin targets built.
