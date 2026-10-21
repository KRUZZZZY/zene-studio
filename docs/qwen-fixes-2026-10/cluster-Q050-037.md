IMPLEMENTED

Q050-037 - define the automation value before its first point. Feature record:
`docs/050-features/Q050-037.md`. Round-3 logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-037.r3/logs`.

**Round 3 (review r2: REWORK).** The generic `valueAt()` change also moved relative
(trim) clips before their first node (offset -0.5 -> +0.3 on a 0..1 model
initialised to 0.8). Fixed: `AutomationClip::preFirstValueLocked()` and the
`valueAt()` pre-first branch return `0` for a relative clip (and do not set
`beforeFirstNode`), and `writeBlockRamp()` does not report a `firstNodeFrame` for
one, so `Song::buildAutomationRamps` shares the one ramp (0 before the node) and
`TrackContainer` is not asked to override it. Absolute clips are unchanged.

## Acceptance

| Criterion | Command (from `build/tests`, `QT_QPA_PLATFORM=offscreen`) | Result |
|---|---|---|
| Pre-first reads the authored initial value / explicit clip-start value; seek-stop-return is stable; save/reload keeps the contract; per-target values on published ramps | `ctest -R "AutomationPreFirstValueTest\|SampleAccurateAutomationTest" --output-on-failure` (`build-2.log` EXIT=0; `test-1.log`) | EXIT=0, 2/2. All earlier cases pass, unchanged. |
| Relative (trim) clip keeps its old pre-first offset (review r2) | same, `AutomationPreFirstValueTest::aRelativeClipKeepsItsOldPreFirstTrimOffset` | EXIT=0. Model initial 0.8 (not the midpoint), first node at tick 50: `valueAt` = 0 and not flagged pre-first; song read = 0.8 + (0 - 0.5) = 0.3; ramp = 0 on every frame; nodes at/after unchanged. |
| Neighbour sweep (13 automation tests, incl. the instrument-module tests after building `tripleoscillator amplifier`) | `ctest -R "^(AutomationModesTest\|AutomationPreFirstValueTest\|SampleAccurateAutomationTest\|ControlAutomationScriptTest\|ControlAutomationModesTest\|AutomationRenderAllocationTest\|AutomationPrepassProofTest\|AutomationLanesTest\|AutomationWriteToEndTest\|AutomationThinTest\|AudioAutomationToolsTest\|AutomationClipDisconnectTest\|AutomationTrackTest)$"` (`build-5.log` EXIT=0, `test-4.log`: 4 failed because the instrument modules were not built, "no automatable device parameter") | `build-6.log` (tripleoscillator, amplifier) EXIT=0; then the 4 failed tests (`test-5.log`) EXIT=0; the other 9 passed in `test-4.log`: 13/13 green. |

**RED evidence 1 - the review's point (relative guards removed).** Temporarily
turned the three `isRelative()` guards in `AutomationClip.cpp` off, rebuilt
(`build-3.log` EXIT=0), ran `ctest -R AutomationPreFirstValueTest`
(`test-2-relguard-removed.log`): **EXIT=8**,
`aRelativeClipKeepsItsOldPreFirstTrimOffset` failed (`valueAt` read 0.8, expected 0).
Restored from a copy; the rebuilt tree passes (`test-4.log`).

**RED evidence 2 - the whole fix reversed.** Temporarily made
`preFirstValueLocked()` return `0` and stopped `valueAt()` setting the pre-first
flag (test APIs kept so the tests compile): build (`build-4.log`) EXIT=0, ctest
(`test-3-reversed.log`) **EXIT=8**: the five pre-first cases in
`AutomationPreFirstValueTest` and `SampleAccurateAutomationTest::aBlockBeforeTheFirstPointReadsThePreFirstValue`
failed; the new relative test (correctly) kept passing. Restored from a copy;
the rebuild and `test-4.log` pass.

## Self-check

- **Each new test fails with the product change reverted, then passes restored.**
  Yes: RED evidence 1 and 2 above, restored builds EXIT=0 and ctest EXIT=0.
- **Every acceptance criterion is proven by a test I ran.** Yes: the earlier three
  criteria plus the multi-target and relative rows, `test-1.log` EXIT=0 and the
  13-test neighbour sweep green.
- **One undo step per edit, honest reversibility class.** No command added or
  changed; nothing to classify.
- **Realtime.** The round-3 change adds only an atomic-bool read (`isRelative()`,
  relaxed load, already read on the audio path by `applyRelativeClips`) inside the
  existing clip lock. No allocation, lock, wait, I/O or signal. Local TSan does not
  run on this box; reasoned by design.
- **Old projects / round trip.** No serialized field; relative clips now read
  exactly as before the unit; absolute clips round-trip as before
  (`saveAndReloadKeepsThePreFirstContract`, EXIT=0).
- **Portability.** No new API; one `isRelative()` call.
- **GUI and agent surface.** Unit adds no action or command; the existing
  automation GUI and `automation.*` commands are unchanged.
- **Pre-submit check.** See the result recorded in the commit that follows this
  report (`submit-check-*.log`).
