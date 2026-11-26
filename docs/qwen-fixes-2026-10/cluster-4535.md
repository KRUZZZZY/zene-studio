FIXED

# Cluster 4535 — Routing a parent folder does not route nested descendants

## Verdict: REAL, fixed

The earlier attempt (SKIPPED-SCOPE) confirmed the mechanics but did not fix them. Nesting is allowed
(`TrackFolder::canHold`, `src/tracks/TrackFolder.cpp:221` refuses only self, another container and a
cycle), so the defect is reachable and was fixed rather than refused.

### Evidence of the defect (before the fix)

- `enableRouting()` looped over `children()` only, and `routeChild()` returns when the child has no
  mixer-channel model (a folder has none: `Track::mixerChannelModel()` is nullptr by default and
  `TrackFolder` declares no override). The track inside a nested folder was never reached.
- Regression test run on the unfixed tree (`logs/test-red.log`): `routingAFolderRoutesTheTracksOfItsNestedFolders`
  `deepModel->value()` 0, expected 3; `tracksMovingIntoOrOutOfANestedFolderFollowTheOuterRouting`
  `joinerModel->value()` 0, expected 4; `reassigningANestedRoutedTracksMixerChannelIsRefused`
  `deep->mixerChannelModel()->value()` 0, expected 6.

### Fix (design)

A routing folder owns the channel of every track below it, down to the next routing folder, which owns
its own subtree. (A routing folder nested in a routing folder keeps its own children on its own channel;
chaining one folder channel into another would need mixer sends and is not attempted here.)

- `src/tracks/TrackFolder.cpp`: `routedMembers()` (direct children with nested group folders expanded
  recursively; a nested routing folder contributes nothing) now drives `enableRouting`, `releaseRouting`
  (unroute and range shrink), `channelDeleted` and `ensureRoutingChannel`. `routingOwner()` finds the
  nearest routing folder at-or-above a folder; `childLinked`/`childUnlinked` use it, so a track that joins
  or leaves a group folder nested in a routing folder, or a whole folder moved in or out, is routed or
  restored (`routeSubtree`/`unrouteSubtree`). `appendRouted` reads no state of a folder with no members,
  which is what a folder in `~TrackFolder` looks like, so the half-destroyed object is not touched.
  Recursion terminates because `canHold` refuses cycles.
- `include/TrackFolder.h`: declarations for the above.
- `src/core/ControlCommandsTrackChannel.cpp`: the BUG-4536 reassignment guard looked at the direct parent
  only; it now uses `routingOwner()` so a nested routed track cannot be moved off the owner's channel
  (sibling consistency).
- No new control commands, no audio-thread code touched (all main-thread model edits), no file added, so
  nothing to register. Saved data: `prevch` bindings are keyed by track id and already global; old projects
  whose routing folder holds a nested folder now route the nested tracks on load, the same way a direct
  child on another channel already was.

### Tests (`tests/src/core/TrackFolderTest.cpp`)

- `routingAFolderRoutesTheTracksOfItsNestedFolders` (the finding's scenario, plus release restores value and range)
- `tracksMovingIntoOrOutOfANestedFolderFollowTheOuterRouting`
- `aNestedRoutingFolderKeepsItsOwnChildren` (guard for the design rule; passes with and without the fix)
- `reassigningANestedRoutedTracksMixerChannelIsRefused`

### Commands and results (logs in /home/kruzzzzy/.config/kads/zene/state/runs/4535.r1/logs)

```
RED   cmake --build build --target zene TrackFolderTest -j4 > build-red.log        EXIT=0
RED   (cd build/tests && ctest -R TrackFolderTest --output-on-failure > test-red.log)   EXIT=8  (37 passed, 3 failed)
GREEN cmake --build build --target zene TrackFolderTest -j4 > build-green.log      EXIT=0
GREEN (cd build/tests && ctest -R TrackFolderTest --output-on-failure > test-green.log) EXIT=0
FAMILY ctest -R "Folder|MixerChannel|ControlTrack|Routing" > test-green-family.log    EXIT=0 (19/19 passed)
4b    git diff -- src/tracks/TrackFolder.cpp include/TrackFolder.h src/core/ControlCommandsTrackChannel.cpp > fix.patch   EXIT=0
4b    git apply -R fix.patch                                                        EXIT=0
4b    cmake --build ... > build-revert.log                                          EXIT=0
4b    ctest -R TrackFolderTest > test-revert.log                                    EXIT=8  (FAILS, the 3 tests above)
4b    git apply fix.patch                                                           EXIT=0
4b    cmake --build ... > build-reapply.log                                         EXIT=0
4b    ctest -R TrackFolderTest > test-reapply.log                                   EXIT=0
```

Family build note: four of the names matching the family pattern (`ControlTrackFolderTranscript`,
`ControlMixingRouting`, `ControlRoutingCommands`, `ControlWidePinRouting`) are Python script tests, not build
targets; they ran against the rebuilt `build/zene`. A single combined family build returned EXIT=2 because of
those non-target names; the C++ targets were then built individually without error.

### Gates

`python3 .../feature_check.py <worktree> > gates.log` returned `EXIT=1` without printing a table: the script
itself runs `git -C .../gate-baselines/tree checkout -q --detach <base>`, which the session's git guard refuses
("is not allowed in a Bunny lane"). The gate table is therefore **unverified** from this session. New
functions are small (each CCN well under 10 by inspection) and no new file was added; `TrackFolder.cpp` was
already over 500 lines before this change.

### Not verified

- TSan does not run on this box. Reasoning: all changed code runs on the main/UI thread under model edits,
  never in process/render, so no new audio-thread allocation, lock or log.
- The full test suite was not run, only the name-pattern family above.

## Other findings

- A routing folder nested in a routing folder is not summed into the outer folder's channel (design limit
  described above).
- `enableRouting` still accepts a folder whose only child is an empty nested folder (nothing to sum).

Delegation: Haiku ran the RED build/test, the GREEN build/test plus the ctest family, and the revert/reapply
proof (steps 4b); I wrote the tests, the fix, this report and reviewed the diff and logs.

## Self-check

- **Fails reverted, passes with fix:** yes. Reverted (`git apply -R fix.patch`, rebuild): `ctest -R TrackFolderTest` EXIT=8, 3 new tests fail (`logs/test-revert.log`). Re-applied: EXIT=0 (`logs/test-reapply.log`). The 4th new test (`aNestedRoutingFolderKeepsItsOwnChildren`) guards the design rule and passes both ways by intent.
- **Deterministic:** yes. The tests drive `setMode`/`setParentFolder` and read `IntModel::value()` directly on the main thread; no timers, sleeps, threads or event loop waits, no timestamps.
- **Complete / siblings:** every loop that walked `children()` for routing (`enableRouting`, `releaseRouting` unroute and `shrinkChildRange`, `channelDeleted`, `ensureRoutingChannel`) now uses `routedMembers()`; `childLinked`/`childUnlinked` (the join/leave path via `Track::setParentFolder`, also used by `~TrackFolder` and `~Track`) use `routingOwner()`; the `track.set_mixer_channel` guard (only other reader of "parent is routing", found with a search for `isRouting()`/`Mode::Routing`) uses `routingOwner()`. Display-only readers (`folder_mode` in `ControlCommandsTransport.cpp:114`, `ControlEditSupport.cpp:449`) report the folder's own mode and are unchanged.
- **No collateral damage:** a routing folder with only direct children behaves exactly as before (`routedMembers()` equals `children()` there; same `routeChild`/`unrouteChild`). No save-format change (`prevch` stays id:channel pairs). Only projects that already combined a routing folder with a nested folder change: their nested tracks are now routed on load, as a direct child on another channel already was. No limit or check was added.
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable; the verdict is FIXED.
- **Gate status:** `feature_check.py` and therefore `submit_check.py`'s gate item cannot run in this session: the script's own `git checkout --detach` into its baseline tree is refused by the session's git guard (exit 97, `logs/gates.log`, `logs/submit-check.log`). This is a tooling block, not a gate result; no gate was seen passing.
