# P0 GUI Fix Verification

Branch: `p0fix/gui-q032-q011`, based on train 2 tip `888934e1b`.

The mixer strip event filter in `src/gui/MixerChannelView.cpp` selects its owner before descendant controls act, and installs itself on descendants added after construction (including rebuilt macro knobs). `Knob::updateFixedSize()` and `Knob::onKnobNumUpdated()` in `src/gui/widgets/Knob.cpp` preserve the accessible target through label, type, and enabled-state size changes while leaving ordinary sizing unchanged.

## Round 1 — initial fixes

The initial regression cases covered controls present at strip construction and the label-size reset path. The independent review found that dynamic macro knobs and the knob pixmap update path were still uncovered; round 2 below records the rework and supersedes these first-pass verdicts.

| Queue unit | Acceptance criterion | Verdict | Regression proof | Red test exit | Green test exit | Build exit | Fix commit | Gates (complexity / file length / fork sources / no upstream regression) |
|---|---|---|---|---:|---:|---:|---|---|
| Q050-032 | A fader, knob, or other child control selects its owning strip before the control action. | FIXED | `MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment` presses the fader, pan knob, and mute button; each press selects the target strip. | 8 | 0 | 0 | `be81b3925` | 0 / 0 / 0 / 0 |
| Q050-032 | The regression test checks selection for each control class and routes a subsequent keyboard action to that strip. | FIXED | The same test confirms Up changes the target fader and leaves Master unchanged after each control press. | 8 | 0 | 0 | `be81b3925` | 0 / 0 / 0 / 0 |
| Q050-032 | The test passes headlessly without weakening existing selection assertions. | FIXED | `MixerSelectionIdentityTest` runs with `QT_QPA_PLATFORM=offscreen`; all four existing identity tests and the new press test pass. | 8 | 0 | 0 | `be81b3925` | 0 / 0 / 0 / 0 |
| Q050-011 | Accessible mode keeps the configured minimum target after a knob fixed-size update. | FIXED | `KnobAccessibleTargetSizingTest` starts in accessible mode and checks both minimum dimensions against the 32px target after the knob sizing reset. | 8 | 0 | 0 | `f367e90ed` | 0 / 0 / 0 / 0 |
| Q050-011 | A regression test covers the resize/reset sequence that lost the target. | FIXED | The test shows a `Small17` knob, calls `setFixedFontSizeLabelRendering()` (which recalculates its fixed size), and verifies the target remains. | 8 | 0 | 0 | `f367e90ed` | 0 / 0 / 0 / 0 |

Both first-pass red runs failed on the product before their respective fixes (`ctest` exit 8). Both green runs passed after those fixes (`ctest` exit 0). The initial builds used `-j4` and exited 0. The added accessible-mode CTest case reuses `KnobKeyboardTest`; no new executable or plug-in dependency was added. Inherited source and test registration changes are recorded in `tests/upstream-modifications.txt`; no new source file required a scope-manifest entry.

No audio-thread path, agent command, undo contract, or project format changed. The work is GUI-only and adds no Qt-version-specific API.

## Round 2 — independent-review rework (2026-10-09)

For each row, only the product change was temporarily reversed after its fix commit; the targeted executable was rebuilt and its focused CTest run failed with exit 8. Restoring the product change, rebuilding, and rerunning produced exit 0. Every build used `-j6`; the four pre-commit gates ran before each fix commit and all exited 0.

| Item | Fix commit | Regression test | Red / green CTest exits | Red / green build exits | Gate exits (complexity / file length / fork sources / no upstream regression) |
|---|---|---|---:|---:|---|
| Q050-032 | `b0951438a` | `MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment` — adds a macro after strip construction and checks selection inside its `sliderPressed` action; also checks fader, pan knob, mute action, and keyboard routing. | 8 / 0 | 0 / 0 | 0 / 0 / 0 / 0 |
| Q050-011 | `714b96922` | `KnobAccessibleTargetSizingTest` — checks the accessible target after fixed-size reset, knob type change, and disable refresh. | 8 / 0 | 0 / 0 | 0 / 0 / 0 / 0 |
