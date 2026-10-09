# P0 GUI Fix Verification

Branch: `p0fix/gui-q032-q011`, based on train 2 tip `888934e1b`.

The mixer strip event filter in `src/gui/MixerChannelView.cpp:231` selects its owner on descendant mouse presses before each child handles the event. `Knob::updateFixedSize()` in `src/gui/widgets/Knob.cpp:175` clamps both sizing paths to the accessible target while leaving ordinary sizing unchanged.

| Queue unit | Acceptance criterion | Verdict | Regression proof | Red test exit | Green test exit | Build exit | Fix commit | Gates (complexity / file length / fork sources / no upstream regression) |
|---|---|---|---|---:|---:|---:|---|---|
| Q050-032 | A fader, knob, or other child control selects its owning strip before the control action. | FIXED | `MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment` presses the fader, pan knob, and mute button; each press selects the target strip. | 8 | 0 | 0 | `be81b3925` | 0 / 0 / 0 / 0 |
| Q050-032 | The regression test checks selection for each control class and routes a subsequent keyboard action to that strip. | FIXED | The same test confirms Up changes the target fader and leaves Master unchanged after each control press. | 8 | 0 | 0 | `be81b3925` | 0 / 0 / 0 / 0 |
| Q050-032 | The test passes headlessly without weakening existing selection assertions. | FIXED | `MixerSelectionIdentityTest` runs with `QT_QPA_PLATFORM=offscreen`; all four existing identity tests and the new press test pass. | 8 | 0 | 0 | `be81b3925` | 0 / 0 / 0 / 0 |
| Q050-011 | Accessible mode keeps the configured minimum target after a knob fixed-size update. | FIXED | `KnobAccessibleTargetSizingTest` starts in accessible mode and checks both minimum dimensions against the 32px target after the knob sizing reset. | 8 | 0 | 0 | `f367e90ed` | 0 / 0 / 0 / 0 |
| Q050-011 | A regression test covers the resize/reset sequence that lost the target. | FIXED | The test shows a `Small17` knob, calls `setFixedFontSizeLabelRendering()` (which recalculates its fixed size), and verifies the target remains. | 8 | 0 | 0 | `f367e90ed` | 0 / 0 / 0 / 0 |

Both red runs failed on the unmodified product (`ctest` exit 8). Both green runs passed after their fix (`ctest` exit 0). Builds used `-j4` and exited 0. The added accessible-mode CTest case reuses `KnobKeyboardTest`; no new executable or plug-in dependency was added. Inherited source and test registration changes are recorded in `tests/upstream-modifications.txt`; no new source file required a scope-manifest entry.

No audio-thread path, agent command, undo contract, or project format changed. The work is GUI-only and adds no Qt-version-specific API.
