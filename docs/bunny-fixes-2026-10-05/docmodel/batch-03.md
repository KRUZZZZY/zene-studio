# docmodel batch-03 — automation clips / transforms / commands

Lane: `docmodel`. Six findings from the Luna pass on
`src/core/AutomationClipTransforms.cpp`, `src/core/ControlCommandsAutomation.cpp`.

Five were real and are fixed, each in its own commit. One is a documented design
decision, not a defect.

## Summary

| Cluster | Subject | Verdict | Commit |
| --- | --- | --- | --- |
| 2691 | `drawShape` unbounded point count | FIXED | `3f052a4b3` |
| 3728 | `transformRange` input arithmetic | FIXED | `26480b327` |
| 5533 | `transformRange` destination collisions | FIXED | `dc6851094` |
| 4645 | `transformRange` drops locked tangents | FIXED | `b76118bc8` |
| 5534 | `thin()` removes tangent-locked points | FIXED | `bec115618` |
| 2792 | `automation.mode_set` without undo | FALSE-POSITIVE | none |

All five fixes are in the one file `src/core/AutomationClipTransforms.cpp`, and
all five regressions live in the existing `tests/src/core/AutomationLanesTest.cpp`.
No new test file, no `tests/CMakeLists.txt` or `tests/fork-sources.txt` change.

---

### Cluster 2691 — Drawing a shape can allocate an impractical number of points

Verdict: FIXED

Evidence: `src/core/AutomationClipTransforms.cpp:199-216` (revision e5a603132) computed the
point count inside the loop and inserted into the `QMap` without any bound, so `start=0,
end=INT_MAX, step=1` attempted billions of insertions. Added
`constexpr long long DrawShapeMaxPoints = 1 << 16;` and an early return placed *before* the
range is cleared, so an oversized request is refused whole rather than thinning the shape the
caller asked for.

Commit: `3f052a4b3`

Test: `AutomationLanesTest::aDrawThatWouldNameBillionsOfPointsIsRefused`

Red/green:
- Red: `cmake --build build --target zene AutomationLanesTest` then
  `cd build/tests && ctest -R '^AutomationLanesTest$' --output-on-failure` — EXIT=8
  (map size 200001, expected 2).
- Green: same commands — EXIT=0.

Note: five other `AutomationLanesTest` cases were failing for an unrelated reason — the
`tripleoscillator` plugin module had not been built. Building that target cleared them. This is
the stale-binary false lead called out in the lane rules, not a defect.

---

### Cluster 3728 — Transform input arithmetic can overflow or produce invalid ticks

Verdict: FIXED

Evidence: `src/core/AutomationClipTransforms.cpp:237` (revision e5a603132) fed caller-supplied
scales straight into the tick arithmetic and into `lround` with no validation, so a non-finite
or unrepresentable scale produced an invalid tick. Added two anonymous-namespace helpers,
`transformInputsAreFinite(...)` and `transformedTick(...)`; the latter computes in `long double`,
clamps to `[0, INT_MAX]` and rounds with `std::llround`. `transformRange` now returns 0 for
non-finite or unrepresentable inputs. Added `#include <limits>`.

Commit: `26480b327`

Test: `AutomationLanesTest::aTransformWithUnrepresentableInputsStaysInTickRange`

Red/green:
- Red: `cmake --build build --target zene AutomationLanesTest` then
  `cd build/tests && ctest -R '^AutomationLanesTest$' --output-on-failure` — EXIT=8
  (`transformRange(0, 96, inf, ...)` returned 2, expected 0).
- Green: same commands — EXIT=0.

---

### Cluster 5533 — Time transforms silently overwrite points that land on the same tick

Verdict: FIXED

Evidence: `src/core/AutomationClipTransforms.cpp:237-246` (revision b9a463ad4) did
`kept[tick] = point`, replacing any point already at that tick — including an unmoved point
outside the selected range — while still returning the full `moved` count, so automation was
lost and the command reported success. Added `destinationsAreDistinct(kept, destinations)`
(using `std::set`, added `#include <set>`). Destinations are now computed and checked against
each other and against the unmoved points *before anything is committed*; a collision refuses
the whole transform with `return 0`.

Commit: `dc6851094`

Test: `AutomationLanesTest::aTransformThatWouldLandTwoPointsOnOneTickIsRefused`

Red/green:
- Red: `cmake --build build --target zene AutomationLanesTest` then
  `cd build/tests && ctest -R '^AutomationLanesTest$' --output-on-failure` — EXIT=8
  (returned 3, expected 0).
- Green: same commands — EXIT=0.

Caller checked: `src/core/ArrangementEdits.cpp:289` `moveFollowingAutomation` calls
`transformRange(..., 1.0, delta, 1.0, 0.0)`. It now refuses on a collision rather than dropping
a point. `ArrangementModesTest` and `AudioAutomationToolsTest` were built and run for this
commit — both EXIT=0 (both first reported "Not Run" until their targets were built).

Design note: refusal was chosen over a merge policy because refusal is the only option that
neither destroys points nor misreports the moved count. This is a deliberate behaviour change,
not a pure refactor.

---

### Cluster 4645 — Range transforms discard custom tangents

Verdict: FIXED

Evidence: `src/core/AutomationClipTransforms.cpp:239` (revision e5a603132) built the replacement
node from values, curve and bend only. The locked-tangent flag and the tangent values were
dropped, so `AutomationClip.cpp:1237-1310 generateTangents()` — which skips nodes where
`LOCKEDTAN(it)` is true — regenerated them, and an identity transform silently changed a
hand-drawn curve. The replacement node now carries `setLockedTangents(...)` and, when locked,
copies `getInTangent()` / `getOutTangent()`.

Commit: `b76118bc8`

Test: `AutomationLanesTest::aRangeTransformKeepsManuallyLockedTangents`

Red/green:
- Red: `cmake --build build --target zene AutomationLanesTest` then
  `cd build/tests && ctest -R '^AutomationLanesTest$' --output-on-failure` — EXIT=8
  (`lockedTangents()` FALSE after an identity transform).
- Green: same commands — EXIT=0.

Design note: locked tangents are copied verbatim, with no slope rescaling for non-identity time
scales — the minimal behaviour-preserving fix. Rescaling is arguably more correct and is left as
a leftover below.

---

### Cluster 5534 — Thinning can remove points that define manually shaped curves

Verdict: FIXED (partially — see Leftovers)

Evidence: `src/core/AutomationClipTransforms.cpp:289-298` (revision b9a463ad4). `thin()`'s keep
loop was `if (INVAL(it) != OUTVAL(it) || it.value().curve() != AutomationNode::Curve::Clip)`,
and its `simplify` measurement compares only OUT values against a straight line. A point on the
line whose tangents were locked by hand therefore measured as worthless and was removed.
Extended the keep loop with `|| it.value().lockedTangents()`.

Commit: `bec115618`

Test: `AutomationLanesTest::thinningKeepsPointsWhoseTangentsWereLockedByHand`

Red/green:
- Red: `cmake --build build --target AutomationLanesTest > build/bunny-b.log 2>&1` (EXIT=0) then
  `cd build/tests && ctest -R '^AutomationLanesTest$' --output-on-failure` — EXIT=8,
  `'clip->getTimeMap().contains(96)' returned FALSE`.
- Green: same commands — EXIT=0 (16/16 cases).
- Also green: `ctest -R '^(ArrangementModesTest|AudioAutomationToolsTest|Automation.*Test)$'` —
  EXIT=0, 6/6 (after building `AutomationModeMenuTest`, which first reported "Not Run").

Trap hit worth recording: `AutomationClip::ProgressionType` has no `Spline` member — the spline
mode is `CubicHermite` (`include/AutomationClip.h:58-63`). The first build attempt failed with
BUILD=2 on that name.

---

### Cluster 2792 — `automation.mode_set` changes state without an undo step

Verdict: FALSE-POSITIVE

Evidence: the absence of an undo step is documented behaviour, not an oversight.
- `src/core/ControlCommandsAutomation.cpp:143-155` (the handler's own comment) and
  `src/core/ControlCommandsAutomation.cpp:309-336` (the registered command description) both
  state: "The mode is runtime state: it is not persisted in the project file and it is not
  journalled, so a mode change has no undo."
- `docs/KNOWN-LIMITATIONS.md:591-592` and `docs/AUTOMATION-MODES.md` say the same.
- `src/core/ControlReversibilityTableAutomationModes.cpp:86-100` classifies the command
  `RC::Irreversible, false` with the rationale that the `AutomatableModel` is not a
  `JournallingObject`, and that the design decision in `docs/AUTOMATION-MODES.md` is that the
  alpha's project files must keep loading unchanged — so `saveSettings`/`loadSettings` were
  deliberately left untouched.

So the mutation and the undo contract are already consistent: the command is registered
irreversible, advertises that in its description, and exposes the current mode through
`automation.get_state`. The reported "undo can undo an earlier project edit while leaving the
mode change in place" is the intended behaviour of an irreversible runtime-state command.

Making it undoable would require journalling the mode and persisting it, which changes project
file semantics. That is a design and persistence change, out of scope for this lane.

Commit: none

Test: none

Red/green: none

---

## Gates

Both required before each of the five fix commits, all EXIT=0:

```
bash tests/complexity-gate.sh --check > build/bunny-cx.log 2>&1; echo EXIT=$?   # EXIT=0
bash tests/file-length-gate.sh --check > build/bunny-fl.log 2>&1; echo EXIT=$?  # EXIT=0
```

## Leftovers

- Cluster 5534's suggested fix was two-part. The first half — preserving tangent-locked points —
  is done. The second half, measuring simplification error against the evaluated curve rather
  than against a straight line between OUT values, is **not** done: it would change which
  unlocked points `thin()` removes across the whole file, which is well beyond a minimal
  behaviour-preserving fix and could not be covered by the single narrow regression used here.
  Worth a separate finding with its own red test.
- Cluster 4645: locked tangent slopes are copied verbatim rather than rescaled by the transform's
  time scale. Rescaling would be more correct for non-identity scales but is a semantics change,
  not a minimal fix.
- Clusters 3728 and 5533 introduce a refuse-whole policy (return 0) for inputs that previously
  produced an invalid tick or a silently dropped point. Any caller that relied on the old
  behaviour now gets a refusal instead. The one in-tree caller,
  `src/core/ArrangementEdits.cpp:289`, is covered and green.