# docmodel batch-06 — visibility sets and folder routing undo

Lane: `docmodel`. Six findings from the Luna pass on
`src/core/ControlCommandsTrackFolderSets.cpp` and `src/core/ControlCommandsTrackFolder.cpp`.

One is real and is fixed, with a regression test. One is out of scope by the lane's own
documented limit. Four are false positives: they describe reported *inverse descriptors*
as if the product replayed them, and it does not.

The common refutation is one line of plumbing. `transactionPayload`
(`src/core/ControlEditSupport.cpp:481-493`) writes exactly `op`, `args`, `before`,
`reversible` and `mechanism` — it never writes an `applies` key. Only `commandTransaction`
(`src/core/ControlRecordingSupport.cpp:125-134`) adds `applies: command`, and only that key
makes `control.undo` dispatch a real inverse (`src/core/ControlTransactions.cpp:120-141`
exports the descriptor for reporting only). The four visibility-set commands build their
payloads with `transactionPayload`, so their `inverse` descriptors are descriptive text for
the transaction report and are never executed by the product. Clusters 1209, 1210 and 1211
all rest on replaying a descriptor that no code path replays. Cluster 1208 additionally
points at an undo step that is already correct.

## Summary

| Cluster | Subject | Verdict | Commit |
| --- | --- | --- | --- |
| 4969 | `visibility_set_save` accepts duplicate track ids | FIXED | `ce3ac3a65` |
| 4020 | undoing a routing-mode change loses channel settings | SKIPPED-SCOPE | none |
| 1208 | saving a new set reports the wrong inverse | FALSE-POSITIVE | none |
| 1209 | applying a set reports an unusable inverse | FALSE-POSITIVE | none |
| 1210 | show-all reports an unusable inverse | FALSE-POSITIVE | none |
| 1211 | removing a set reports removal as its inverse | FALSE-POSITIVE | none |

### Cluster 4969 — Visibility sets accept duplicate track IDs

Verdict: FIXED

Evidence: `src/core/ControlCommandsTrackFolderSets.cpp:115-125` (in `saveSet`) resolved each
supplied id and appended it unconditionally, with no membership check, and the layer below
does not dedup either (`src/core/TrackContainerFolders.cpp:153-175` stores `trackIds`
verbatim). The stored definition therefore held one track twice, so the reply's `track_count`
counted it twice and `track.visibility_set_apply`, which derives its shown/hidden counts from
that same definition, showed it as two members. `saveSet` now skips an id already present in
`ids`, so a set is a set of members. This is the command's own bookkeeping — no control
command, param or result schema changed, and the `ControlCommandsSnapshot` canary is
untouched.

Commit: `ce3ac3a65`

Test: `TrackFolderTest::aRepeatedTrackIdIsSavedAsOneMember`

Red/green: `ctest -R '^TrackFolderTest$' --output-on-failure` -> EXIT=8 with the product fix
reverted (`Actual track_count: 4, Expected: 2`, 11 passed / 1 failed); EXIT=0 with the fix
restored (12 tests). `ctest -R '^(VisibilitySetMenuTest|UndoHonestyTest)$'`
(`UndoHonestyTest` carries `noRecordNamesAnUnimplementedInverse`) -> EXIT=0 before and after.
Both gates EXIT=0 at the commit.

### Cluster 4020 — Undoing a routing-mode change loses the folder channel's settings

Verdict: SKIPPED-SCOPE

Evidence: the described release is real — `setRouting`
(`src/core/ControlCommandsTrackFolder.cpp:225-288`) can release a folder's mixer channel on a
mode change, and the inverse restores the mode but not the released channel's effects, sends
and parameters. It is a known, documented limit, not an oversight: the handler states it at
`src/core/ControlCommandsTrackFolder.cpp:279-286`, and the contract row in
`src/core/ControlReversibilityTableTrackFolder.cpp` carries the same LIMIT. Preserving a
released channel's full engine state across a mode switch is a design change well beyond the
~150-line ceiling, so the code was left alone.

Commit: none

Test: none

Red/green: none

### Cluster 1208 — Saving a new visibility set has the wrong transaction inverse

Verdict: FALSE-POSITIVE

Evidence: the reported descriptor is never replayed — `saveSet` attaches its
`__transaction` through `transactionPayload`
(`src/core/ControlEditSupport.cpp:481-493`), which omits `applies`, so `control.undo` does
not dispatch it; `src/core/ControlTransactions.cpp:120-141` only exports it for the report. The
real undo step is the lambda at `src/core/ControlCommandsTrackFolderSets.cpp:130-135`, which
already calls `removeVisibilitySet` when the set did not exist before the save, so absence is
genuinely restored. Only the descriptive text is imprecise, and no test or product behaviour
depends on it.

Commit: none

Test: none

Red/green: none. No reproduction was attempted: the refutation is structural. The suites
covering this file were run on this branch and pass -
`ctest -R '^(TrackFolderTest|VisibilitySetMenuTest|UndoHonestyTest)$'` -> EXIT=0 (3 tests).

### Cluster 1209 — Applying a visibility set reports an unusable and incomplete inverse

Verdict: FALSE-POSITIVE

Evidence: same refutation — `applySet`'s payload is built with `transactionPayload`
(`src/core/ControlEditSupport.cpp:481-493`) and carries no `applies` key, so the reported
`track.visibility_set_apply` inverse (which indeed omits `name`) is never validated or run.
The actual undo lambda at `src/core/ControlCommandsTrackFolderSets.cpp:178` restores every
captured visibility flag from the checkpoint it took before applying. The claim that "the
transaction record cannot describe the prior flags" is true of the `before` object and
irrelevant: the flags are captured in the handler's own closure.

Commit: none

Test: none

Red/green: none. No reproduction was attempted: the refutation is structural. The suites
covering this file were run on this branch and pass -
`ctest -R '^(TrackFolderTest|VisibilitySetMenuTest|UndoHonestyTest)$'` -> EXIT=0 (3 tests).

### Cluster 1210 — Show-all reports an unusable inverse and omits prior flags

Verdict: FALSE-POSITIVE

Evidence: identical mechanism — `showAll`'s payload comes from `transactionPayload`
(`src/core/ControlEditSupport.cpp:481-493`) with no `applies` key, so the reported
`visibility_show_all` inverse with its `active_before` argument is descriptive only. The real
undo step at `src/core/ControlCommandsTrackFolderSets.cpp:228` restores the captured per-track
visibility flags and the previously active set name, so the prior visibility is restored
exactly as the report asks for.

Commit: none

Test: none

Red/green: none. No reproduction was attempted: the refutation is structural. The suites
covering this file were run on this branch and pass -
`ctest -R '^(TrackFolderTest|VisibilitySetMenuTest|UndoHonestyTest)$'` -> EXIT=0 (3 tests).

### Cluster 1211 — Removing a visibility set reports removal as its inverse

Verdict: FALSE-POSITIVE

Evidence: `removeSet`'s payload is a `transactionPayload`
(`src/core/ControlEditSupport.cpp:481-493`) result with no `applies` key, so the reported
`track.visibility_set_remove` inverse is never replayed and cannot fail on a missing set. The
real undo step at `src/core/ControlCommandsTrackFolderSets.cpp:271` restores the removed set's
full definition, whether or not it was the active set — the capture happens before the
removal.

Commit: none

Test: none

Red/green: none. No reproduction was attempted: the refutation is structural. The suites
covering this file were run on this branch and pass -
`ctest -R '^(TrackFolderTest|VisibilitySetMenuTest|UndoHonestyTest)$'` -> EXIT=0 (3 tests).

Leftovers: none. The tree is clean and both gates (`tests/complexity-gate.sh --check`,
`tests/file-length-gate.sh --check`) exit 0 at every commit in this batch. Two notes for the
parent: (1) clusters 1208-1211 are all the same root cause — `transactionPayload` produces a
report-only descriptor, while only `commandTransaction` produces a replayable one — so if the
program ever wants the four visibility commands to expose genuinely replayable inverses, that
is a deliberate change of the transaction contract in one place, not four handler edits; (2)
`tests/src/core/TrackFolderTest.cpp` is now 507 lines, so the next case added to it needs a
new test file registered in `tests/CMakeLists.txt` and `tests/fork-sources.txt`.