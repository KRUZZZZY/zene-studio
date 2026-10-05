# docmodel batch-05 — move_point, ramp_set and folder routing

Lane: `docmodel`. Six findings from the Luna pass on
`src/core/ControlCommandsAutomationTools.cpp`, `src/core/ControlCommandsAutomationRamp.cpp`
and `src/core/ControlCommandsTrackFolder.cpp`.

Four were real and are fixed, each in its own commit; two were investigated and left
alone (one FALSE-POSITIVE with a pinned proof, one FALSE-POSITIVE on the strength of the
registry's own step-merging rule). This batch also completes the parent reviewer's rework
of `b76118bc8`.

## Rework carried in from batch-04

`b76118bc8` ("a range transform dropped each point's locked tangents") was INCOMPLETE: a
tangent is a slope, value per tick (`src/core/AutomationClip.cpp:1292-1302` computes
Δvalue/Δtick; `src/core/AutomationClip.cpp:651-652` multiplies by `numValues` to keep it a
per-tick slope), so a locked tangent copied unchanged through a time+value rescale gives
the stretched curve the wrong shape. `bf0e96b1e` scales it by `valueScale / timeScale`,
swaps in/out only when `timeScale < 0` (the negative divisor supplies the sign), and carries
tangents unchanged at `timeScale == 0`.

## Summary

| Cluster | Subject | Verdict | Commit |
| --- | --- | --- | --- |
| 4743 | `move_point` indexed the time map with song ticks | FIXED | `4954cc93e` |
| 4744 | moving a point discarded its curve and bend | FIXED | `5608ed164` |
| 2789 | moving onto an occupied tick silently overwrote it | FIXED | `772b669be` |
| 3817 | `ramp_set` advertised an inverse for a no-op | FIXED | `4c80e821d` |
| 4966 | refused routing change leaves an undo step | FALSE-POSITIVE | none |
| 4967 | no-op routing reports a reversible step | FALSE-POSITIVE | `75cf09fc1` (test only) |

### Cluster 4743 — `automation.move_point` uses song ticks directly in the clip time map

Verdict: FIXED

Evidence: `src/core/ControlCommandsAutomationTools.cpp:129-146` passed the supplied ticks
straight into `getTimeMap().contains()`, `removeNode()` and `putValue()`. A time map is keyed
by CLIP-LOCAL ticks, so on a clip starting at song tick 384 the point at song tick 400 sits
at local tick 16 and could not be found. Every sibling already subtracts the clip origin
(`ControlCommandsAutomationLanes.cpp:158` draw_shape, `:172` transform, `:209` set_curve).
Both the source and the destination are now converted, and the reply reports song ticks back.

Commit: `4954cc93e`

Test: `AutomationLanesTest::movePointTakesSongTicksOnAClipThatStartsLater`

Red/green: `ctest -R '^AutomationLanesTest$'` -> EXIT=8, `'moved.ok' returned FALSE.
(inst/0 has no point at 400)` at `AutomationLanesTest.cpp(640)`, 17 passed / 1 failed;
after the fix EXIT=0. `ctest -R '^AutomationModesTest$'` (which carries the pre-existing
`automation.move_point` case) EXIT=0 before and after.

### Cluster 4744 — Moving an automation point discards its curve and bend

Verdict: FIXED

Evidence: the handler saved only `getInValue()`, removed the node and let `putValue()`
insert a plain one, so the moved node came back with the insertion default. A node's curve is
its own progression to the next point (`include/AutomationNode.h:159-162`: Clip, Hold,
Linear, Smooth, Bend, with `bend()` in −1..1), so this silently rewrote the segment's shape.
The curve and bend are now read before the removal and restored on the inserted node.

Commit: `5608ed164`

Test: `AutomationLanesTest::movePointKeepsTheCurveAndBendOfThePointItMoves`

Red/green: EXIT=8, `Actual (int(node.curve())): 0` (Clip) vs
`Expected (int(AutomationNode::Curve::Bend)): 4`; after the fix EXIT=0 (19 passed).
`AutomationModesTest` EXIT=0.

### Cluster 2789 — Moving a point onto another point silently overwrites it

Verdict: FIXED

Evidence: the handler removed the source node and wrote the destination with no occupancy
check. The time map is keyed by tick, so moving the point at tick 10 onto an occupied tick
20 destroyed the tick-20 point. A destination another node already holds is now refused with
`ControlErrorKind::Refused`, leaving both points intact; moving a point onto its OWN tick is a
plain value change and stays allowed.

Commit: `772b669be`

Test: `AutomationLanesTest::movePointRefusesADestinationAnotherPointAlreadyHolds`

Red/green: EXIT=8, `'!moved.ok' returned FALSE. ()`; after the fix EXIT=0 (20 passed).
`AutomationModesTest` EXIT=0. RED was re-confirmed against the final test text with only the
product guard reverted. Two earlier cases in this file were reflowed onto fewer code lines to
stay under the 500-line file-length ratchet; no assertion was changed.

### Cluster 3817 — `automation.ramp_set` reports an inverse when nothing was recorded

Verdict: FIXED

Evidence: `src/core/ControlCommandsAutomationRamp.cpp:165-169` takes the journal checkpoint
and moves the mode only inside `if (changed)`, but `:183-192` attached a
`__transaction` with `reversible: true` unconditionally. The registry's own guard cannot
catch this: `changedNothing()` at `src/core/ControlRegistry.cpp:300` treats a handler-supplied
payload as proof the call really changed something, so the spurious record went onto the
transaction list and undid nothing. The payload is now attached only when a checkpoint was
taken.

Commit: `4c80e821d`

Test: `SampleAccurateAutomationTest::aNoOpRampSetAdvertisesNoInverseAndConsumesNoUndoStep`

Red/green: EXIT=8, `transactions().size()` Actual 94 vs Expected 93; after the fix EXIT=0
(13 passed). RED was confirmed against the true pre-fix state (the unconditional payload),
not an over-trimmed variant.

### Cluster 4966 — Refused routing change leaves an undo step

Verdict: FALSE-POSITIVE

Evidence: the ordering is real as described — `addUndoStep` runs at
`src/core/ControlCommandsTrackFolder.cpp:257`, before `folder->setMode(...)` at `:263`, and the
refusal at `:265` does not remove it. But the step does not survive the call as an orphan:
`ControlRegistry::invoke` calls `journal->mergeCheckpointsFrom(mark)` at
`src/core/ControlRegistry.cpp:334` on the way out, which folds everything the handler pushed
into the CURRENT command's step. A refused call is rolled into the step already open rather
than left on the stack, so the next `control.undo` does not spend itself on it.

Measured, not assumed: a test asserting `control.undo_depth` unchanged across the refusal AND
that the following undo still reverses the real prior edit (`track.set_pinned`) PASSED against
the unfixed code (ctest EXIT=0). Under rule 14 a hypothesis that fails its own attempted
reproduction is not a defect, so the product code was left alone. That test was not kept,
because it would pin registry internals rather than a command's behaviour.

Commit: none

Test: none

Red/green: none (the attempted reproduction was green before any change:
`ctest -R '^TrackFolderTest$'` -> EXIT=0)

### Cluster 4967 — No-op routing reports a reversible step that was not recorded

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsTrackFolder.cpp:232-247` does return
`reversible: true` for a no-op, but this is a deliberate decision documented in the code: the
comment argues an undo of a call that changed nothing must change nothing, so the recorded
inverse is the state the folder is ALREADY in. The harm the report describes — "the next undo
consumes a step for this call" — does not occur, and that is now pinned by a test on this
branch: after a no-op `set_routing`, `control.undo` still reverses the real routing change
behind it.

Commit: `75cf09fc1` (test only, no product change)

Test: `TrackFolderTest::aNoOpRoutingReportsReversibleWithoutStealingTheNextUndo`

Red/green: none (green as written: `ctest -R '^TrackFolderTest$'` -> EXIT=0). This case is a
guard, not a reproduction.

Leftovers: none. The tree is clean and both gates (`tests/complexity-gate.sh --check`,
`tests/file-length-gate.sh --check`) exit 0 at every commit in this batch. Two notes for the
parent: (1) `AutomationLanesTest.cpp` sits close to the 500-line file-length ratchet, so the
next `move_point` case will need a new test file (registered in `tests/CMakeLists.txt` and
`tests/fork-sources.txt`); (2) `changedNothing()` at `src/core/ControlRegistry.cpp:300` cannot
drop a handler-supplied `__transaction`, which is why 3817 had to be fixed in the handler
rather than centrally.
