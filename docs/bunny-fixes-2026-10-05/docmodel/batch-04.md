# docmodel batch-04 — automation lane / edit / ramp commands

Lane: `docmodel`. Six findings from the Luna pass on
`src/core/ControlCommandsAutomationLanes.cpp`, `src/core/ControlCommandsAutomationEdit.cpp`,
`src/core/ControlCommandsAutomationRamp.cpp`.

None of the six is a fixable defect in this lane: three findings misread the code, and
three would need a command result/schema or a lane-ownership design decision that rule 8
reserves for the parent. No product code was changed in this batch.

## Summary

| Cluster | Subject | Verdict | Commit |
| --- | --- | --- | --- |
| 4741 | `automation.add_point` tick units | SKIPPED-SCOPE | none |
| 4742 | `automation.remove_point` tick units | SKIPPED-SCOPE | none |
| 5587 | `lane_add` can bind a second clip | SKIPPED-SCOPE | none |
| 2787 | omitted shape maximum defaults to the minimum | FALSE-POSITIVE | none |
| 5590 | `set_curve` accepts bend outside −1..1 | FALSE-POSITIVE | none |
| 1007 | ramp undo cannot restore an explicit block override | FALSE-POSITIVE | none |

---

### Cluster 4741 — `automation.add_point` treats song ticks as clip-local ticks

Verdict: SKIPPED-SCOPE

Evidence: the surface is genuinely inconsistent, but the inconsistency is in the
*contract*, not in one handler. `automation.set_curve`
(`src/core/ControlCommandsAutomationLanes.cpp:209`), `automation.draw_shape`
(`src/core/ControlCommandsAutomationLanes.cpp:151-152`) and `automation.transform`
(`src/core/ControlCommandsAutomationLanes.cpp:172-176`) subtract `clip->startPosition()`
because they take a RANGE in song ticks, while `automationAddPoint`
(`src/core/ControlCommandsAutomationEdit.cpp:88-92`) and `automationRemovePoint`
(`src/core/ControlCommandsAutomationEdit.cpp:114-125`) address a single node through the
clip-relative time map. What the surface *reports* agrees with add/remove, not with the
others: `pointJson` (`src/core/ControlAutomationSupport.cpp:117-128`) and `automationJson`
(`src/core/ControlAutomationSupport.cpp:341-359`) emit clip-local ticks and never publish
the clip's start, so a caller cannot convert. Changing add/remove to song ticks without
also changing that reply would break the `get_state` -> `remove_point` round trip the
surface itself is built on; changing it correctly means adding a clip-start field to the
reply — a command result schema change (rule 8).

Commit: none

Test: none

Red/green: none

### Cluster 4742 — `automation.remove_point` cannot find points on moved clips

Verdict: SKIPPED-SCOPE

Evidence: same root and same blocked fix as 4741, on the removal side.
`automationRemovePoint` looks up `clip->getTimeMap()` with the tick the caller gave
(`src/core/ControlCommandsAutomationEdit.cpp:116-118`), and the tick it echoes back in the
NotFound message is the one it was given; the clip's own start is never part of the
exchange, so the "point at song tick 800 / clip starts at 768" scenario in the finding
describes a caller that never learned where 768 was — `automationPointsJson`
(`src/core/ControlAutomationSupport.cpp:117-128`) reports clip-local ticks only. This is
one finding with 4741, seen from the delete side.

Commit: none

Test: none

Red/green: none

### Cluster 5587 — Adding an automation lane can create a second driver for the same parameter

Verdict: SKIPPED-SCOPE

Evidence: the described overwrite is real at the engine level — `Song::processAutomations`
collects into one `AutomatedValueMap values` keyed by `AutomatableModel*`
(`src/core/Song.cpp:550`, apply loop from `src/core/Song.cpp:654`), so two clips bound to
one model both publish and the later write wins by track order. But `createLane`
(`src/core/ControlCommandsAutomationLanes.cpp:69-86`) always creates a fresh lane and a
fresh clip and calls `clip->addObject(model)`, while every EDIT path resolves the model
through `automationClipForModel` (`src/core/ControlAutomationSupport.cpp:307-328`), which
returns the FIRST clip it finds for that model. So after `lane_add` the edits keep
addressing the original clip while playback sees both: that is a lane-ownership design
decision (reuse the existing clip, move it, or refuse), and choosing refuse changes
`automation.lane_add`'s success path — rule 8.

Commit: none

Test: none

Red/green: none

### Cluster 2787 — An omitted shape maximum defaults to the minimum

Verdict: FALSE-POSITIVE

Evidence: the draw path already defaults each bound to its own end —
`src/core/ControlCommandsAutomationLanes.cpp:151-152`:

    const float low  = ...args.value("min").toDouble(model->minValue<float>());
    const float high = ...args.value("max").toDouble(model->maxValue<float>());

`shapeBoundsFit` (`src/core/ControlCommandsAutomationLanes.cpp:130-138`) also reads both
keys with `toDouble(model->minValue<float>())`, but that value is only a VALIDATION
placeholder for a key the caller left out, and it is compared against the model's own
range, which it always passes — it never reaches the drawn points. I wrote the regression
the finding implies (square shape, neither `min` nor `max` named, must reach
`volume->maxValue()`; and `min` named alone must still reach the maximum) and it PASSES on
the current tree, unmodified.

Commit: none

Test: `AutomationLanesTest::anOmittedShapeBoundIsTheModelBound` (written to confirm the
finding, green on arrival — the test was removed again rather than committed as a
no-op regression)

Red/green: `cmake --build build --target zene AutomationLanesTest` EXIT=0, then
`cd build/tests && ctest -R '^AutomationLanesTest$' --output-on-failure` EXIT=0 — the
behaviour the finding calls a bug is already the shipped behaviour.

### Cluster 5590 — `automation.set_curve` accepts bend values outside its documented range

Verdict: FALSE-POSITIVE

Evidence: the clamp is one layer below the handler.
`AutomationNode::setCurve` (`src/core/AutomationNode.cpp:121-125`) stores
`std::clamp(bend, -1.f, 1.f)`, so a `bend: 10` from the surface can never reach
playback through this path — `bendValueAt` reads the already-clamped `m_bend`. The
finding's "playback uses that out-of-range bend" does not happen. Refusing the command
would be a new validation and a new error kind on an existing command's params — rule 8 —
and buys nothing the clamp does not already give.

Commit: none

Test: none

Red/green: none

### Cluster 1007 — Ramp undo cannot restore an explicit block override

Verdict: FALSE-POSITIVE

Evidence: both halves of the finding's premise fail on the current tree. First, the flag
is NOT serialized only when on: `src/core/AutomationClip.cpp:877-881` writes
`sample_accurate` for any `m_rampSetting != Default`, with `"0"` for an explicit block,
so a checkpoint carries an explicit off as well as an explicit on. Second, the checkpoint
is taken BEFORE the flag moves (`src/core/ControlCommandsAutomationRamp.cpp:167-168`),
not after, so the checkpoint cannot be the post-change state. The comment above the save
(`src/core/AutomationClip.cpp:874-876`) describes exactly this two-valued encoding. What
remains is a STALE comment at `src/core/ControlCommandsAutomationRamp.cpp:128-130` that
talks about absence as the restore mechanism — noted, not fixed here.

Commit: none

Test: none

Red/green: none

Leftovers: the stale comment at `src/core/ControlCommandsAutomationRamp.cpp:128-130`
describes the old absence-based restore; it is misleading but harmless, and touching that
file risks the canary for no behaviour gain. The 4741/4742 tick-unit split and 5587's
second-driver overlap both need a decision from the parent (command result schema, or lane
ownership) before any code moves.