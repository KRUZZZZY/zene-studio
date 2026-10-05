# Theory lane — batch-06 report

All six findings of this batch are in `src/core/GrooveTemplate.cpp`. Four were
real and are fixed; two were already fixed by commit `57980beef` before this
batch started. Every finding's cited revision (e5a603132, b9a463ad4, 959d30852,
57980beef) is older than the tip, so each was re-read at the current tree before
being classified.

## Summary

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 3175 grid arithmetic overflows signed ticks | FIXED | 83bb21f22 |
| 4162 minimum signed timing value passes validation | ALREADY-FIXED | 57980beef |
| 5100 max positive timing shift breaks idempotence | FIXED | feb2fa8f1 |
| 472 malformed groove slot text overwrites slot zero | FIXED | 61a803d53 |
| 4163 missing groove velocity attribute silences notes | ALREADY-FIXED | 57980beef |
| 2261 random timing breaks the full-strength no-op | FIXED | b264e6c3e |

### Cluster 3175 — Groove grid arithmetic can overflow signed ticks

Verdict: FIXED

Evidence: at the tip every intermediate was signed 32-bit (line numbers in this
paragraph are pre-fix, as read at the tip before this batch). `absoluteSlotOf`
rounded with `pos + stepTicks / 2` (src/core/GrooveTemplate.cpp:64), `gridTickFor`
multiplied slot by slot width in `tick_t` (src/core/GrooveTemplate.cpp:156),
`targetTickFor` added the step's shift (src/core/GrooveTemplate.cpp:163),
`applyGroove` formed `target - pos` (src/core/GrooveTemplate.cpp:391) and
`SlotSums::add` the same deviation (src/core/GrooveTemplate.cpp:269). `tick_t` is
`int32_t`, so a note at `INT32_MAX` overflowed: `gridTickFor(2147483647)` returned
`2147483644` under the old arithmetic, a grid line three ticks *before* the note
and in the wrong slot, and the arithmetic itself is undefined behaviour, not
merely wrong. The rounding, multiplication, target sum, travel difference and
deviation now run in `long long` and are narrowed back through one
`clampedTick` helper, so a value past the end of the timeline is the last tick
rather than a wrapped negative.

Commit: 83bb21f22

Test: GrooveTemplateTest::theGridArithmeticDoesNotOverflowAtTheEndOfTheTimeline

Red/green:
- RED `cd build/tests && ctest -R '^GrooveTemplateTest$' --output-on-failure` EXIT=8 — `Actual (groove.gridTickFor(last)): 2147483644 / Expected: 2147483640`
- GREEN the case passes after the fix; `ctest -R '^(GrooveTemplateTest|ControlGrooveCommandsTest)$'` EXIT=0 and `ctest -R '^ControlGrooveCommands$'` EXIT=0 (the socket test that drives the real `zene` binary)
- `bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0

Note on the first draft of the test: it expected `last - 7`. That was wrong and
was corrected before the commit — 2147483647 is 7 ticks past the grid line at
2147483640, so it belongs to the slot starting at 2147483652, which is past the
end of the representable timeline and must clamp to `last` itself.

### Cluster 4162 — The minimum signed timing value can pass validation

Verdict: ALREADY-FIXED

Evidence: `setStep` no longer takes the absolute value.
src/core/GrooveTemplate.cpp:157 names the reason outright — "Both bounds, not
std::abs: |INT_MIN| is not representable, so abs() of a loaded one is UB" — and
the two comparisons below it (src/core/GrooveTemplate.cpp:158-161) test the
negative and the positive bound directly. `git log -S'abs(value.timing)' --
src/core/GrooveTemplate.cpp`
returns `57980beef`, the commit that replaced the `std::abs` form; the finding
quotes revision `57980beef` itself, so it describes code that commit had already
removed. `timing="-2147483648"` is refused by the lower bound today.

Commit: none needed (fixed by 57980beef)

Test: none added — the existing geometry case
`GrooveTemplateTest::theGeometryBoundsAreTheEnginesOwn` covers the bound, and
`GrooveTemplateTest::aStepCannotReachHalfItsSlot` now covers its exact edge.

Red/green: none.

### Cluster 5100 — A maximum positive timing shift breaks groove idempotence

Verdict: FIXED

Evidence: real. `setStep` accepted `m_stepTicks / 2`, but exactly half a slot is
the boundary between two slots and `absoluteSlotOf` rounds the half *up*
(src/core/GrooveTemplate.cpp:78, the pre-fix rounding of line 64's body), so a
note landing on a `+half` target reads back into the next slot. `applyGroove`
promises a second application is a no-op
(include/GrooveTemplate.h, "At strength 1 the note lands exactly on the groove ...
so a second application is a no-op"). The finding's worked example reproduces:
the old test run moved a note from 0 to 2 and then from 2 to 6.

No tie rule rescues this — whichever side owns the boundary, the other sign then
drifts — so the bound is now strict: a step may reach one tick *inside* half a
slot, never half itself (src/core/GrooveTemplate.cpp:150-161). Two consequences
shipped in the same commit:

- `writeSlot` clamps an extracted deviation to the same reach
  (src/core/GrooveTemplate.cpp:317-327). Clamping to the boundary would build a
  step `setStep` refuses, and a refused step falls back to neutral — so a clip
  whose notes all sit exactly on the boundary would have captured no feel at all.
- The geometry case's own assertions used half a slot as the accepted maximum
  (`GrooveStep{6, 200}` on a 12-tick slot, in
  `GrooveTemplateTest::theGeometryBoundsAreTheEnginesOwn`,
  tests/src/core/GrooveTemplateTest.cpp:75-89 after this commit; the four
  `{6, 200}` assertions sat at lines 79, 84, 88 and 89 before it).
  Those assertions pinned the defect, so they now assert `{5, 200}` as the
  accepted maximum and `{6, 200}` as a *refusal*. No assertion was removed,
  skipped or loosened; the refusal set grew by one case. This is the one place
  in the batch where an existing expectation had to change, and it changed to
  the stricter answer.

Commit: feb2fa8f1

Test: GrooveTemplateTest::aStepCannotReachHalfItsSlot

Red/green:
- RED EXIT=8 — `'!groove.setStep(0, GrooveStep{6, 100})' returned FALSE.`
- GREEN `ctest -R '^GrooveTemplateTest$'` 10 of 11 cases passing (only cluster 2261's case still red at that point), `ctest -R '^ControlGrooveCommandsTest$'` EXIT=0 — its `groove.set` fixtures use timings of 2 and −4 on 12-tick slots, well inside the new reach
- `bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0

### Cluster 472 — Malformed groove slot text silently overwrites slot zero

Verdict: FIXED

Evidence: real. `loadXml` read the slot attribute with `QString::toInt()` and no
ok flag, and `toInt()` returns 0 for text that is not a number, so
`<step slot="bad" timing="3" velocity="90"/>` replaced slot zero while `loadXml`
still returned true. The velocity read two lines below already did it correctly
with `toInt(&velocityRead)`; the slot now reads the same way and an unreadable
slot name is skipped whole (src/core/GrooveTemplate.cpp:247-253). The `"-1"`
default is kept, so a *missing* slot attribute is still refused by `setStep` and
leaves the slot it already holds, exactly as before.

Commit: 61a803d53

Test: GrooveTemplateTest::aMalformedSlotTextIsNotSlotZero

Red/green:
- RED EXIT=8 — `'loaded.step(0).neutral()' returned FALSE. (a malformed slot text overwrote slot zero)`
- GREEN the case passes; `ctest -R '^ControlGrooveCommandsTest$'` EXIT=0 (the pool's XML round trip shares this reader)
- `bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0

### Cluster 4163 — A missing groove velocity attribute silences notes

Verdict: ALREADY-FIXED

Evidence: `loadXml` no longer defaults a missing velocity to 0. It reads
`toInt(&velocityRead)` and maps an unreadable or absent attribute to
`GrooveStep::NoVelocityOpinion` (src/core/GrooveTemplate.cpp:256-259), with the
comment "A missing or unreadable velocity says nothing about velocity; 0 would
silence the slot." `git log -S'velocityRead' -- src/core/GrooveTemplate.cpp`
returns `57980beef`; the finding quotes revision `b9a463ad4`, older still. The
same reasoning protects the slots an extraction never played: a slot nothing
landed in is left neutral rather than given a velocity of 0
(writeSlot, src/core/GrooveTemplate.cpp:314-315, asserted by
`GrooveTemplateTest::extractionReadsTheMeanDeviationPerSlot`).

Commit: none needed (fixed by 57980beef)

Test: none added — the behaviour is covered by the extraction case above, which
asserts slot 0 holds `GrooveStep{0, GrooveStep::NoVelocityOpinion}`.

Red/green: none.

### Cluster 2261 — Random timing breaks the full-strength no-op guarantee

Verdict: FIXED

Evidence: real. `jitteredTick` drew from
`NoteRandom::rollUnit(seed, note.key(), pos, ...)` — seeded on the note's
*current* position (src/core/GrooveTemplate.cpp:359-360 at the tip's pre-fix
state). Applying the jitter changes that position, so repeating the call at full
strength drew a different offset; the regression test's second application moved
3 of 4 notes. The product already had the right rule one subsystem over:
`NoteTransform.cpp`'s `jitterFor` draws from the grid slot and says why in as many
words ("deliberately not its current position, which is what makes the call a
fixed point"), and the command surface documents the same thing for the humanise
("the timing draw is pinned to the slot, so a repeat leaves the positions exactly
where the first roll put them", tests/src/core/ControlGrooveCommandsTest.cpp:259).
The groove's own draw now reads `groove.gridTickFor(pos)` instead of `pos`.

The velocity draw deliberately stays on the position: the groove's velocity target
is absolute, and the surface documents that the velocity jitter rolls again on a
repeat — ControlGrooveCommandsTest asserts exactly that.

Commit: b264e6c3e

Test: GrooveTemplateTest::theRandomTimingIsPinnedToTheSlot

Red/green:
- RED EXIT=8 — `Actual (applyGroove(..., 1.0f, 2, 0, 12345u)): 3 / Expected: 0`
- GREEN `ctest -R '^(GrooveTemplateTest|ControlGrooveCommandsTest)$'` EXIT=0, 0 tests failed out of 2
- `bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0

## Leftovers

Nothing half-done. Two notes for the parent:

- Cluster 5100 changed a *documented public bound* (`|timing|` may now reach one
  tick less than half a slot). Any project file saved by a build that predates
  this commit can hold a step of exactly half a slot; on load that step now leaves
  its slot neutral instead of being applied. No command, param or result schema
  changed, and groove.set now refuses ±half with the existing typed
  InvalidArgs — but if the control-contract canary tracks the accepted timing
  range, that is the parent's call, not this lane's.
- No control command was added, removed or renamed, and no param or result schema
  changed, so the ControlCommandsSnapshot canary should be unmoved.